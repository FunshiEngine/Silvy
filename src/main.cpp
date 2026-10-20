/*
    Silvy - Agente de inteligencia artificial de código para FunshiEngine
    Copyright 2026 Gianfranco Ivan Enrique

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.

    SPDX-License-Identifier: Apache-2.0
*/
// Transporte del binario: parsea argv, elige la fuente de la solicitud (stdin
// o archivo), delega el parseo/despacho en el núcleo y escribe la respuesta a
// stdout con su código de salida. Ninguna lógica de comandos vive aquí.
#include "Config/ConfigLlm.h"
#include "Config/ConfigSilvy.h"
#include "Errores/RegistroErrores.h"
#include "Nucleo/DependenciasNucleo.h"
#include "Nucleo/NucleoSilvy.h"
#include "Nucleo/Respuesta.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>

#ifndef SILVY_VERSION
#define SILVY_VERSION "0.0.0"
#endif

namespace {

namespace fs = std::filesystem;

std::string textoAyuda() {
    return
        "Silvy - agente de inteligencia artificial de código para FunshiEngine\n"
        "\n"
        "Uso:\n"
        "  Silvy --request < solicitud.json\n"
        "  Silvy --request-file <ruta> [--home <ruta>]\n"
        "\n"
        "Opciones:\n"
        "  --request         Lee la solicitud JSON de stdin.\n"
        "  --request-file    Lee la solicitud JSON de un archivo.\n"
        "  --home            Casa de Silvy (config, modelo, errores y logs).\n"
        "                    Precedencia: --home > SILVY_HOME > junto al binario.\n"
        "  --help, -h        Muestra esta ayuda.\n"
        "  --version         Muestra la versión.\n"
        "\n"
        "La respuesta JSON se escribe en stdout. Códigos de salida:\n"
        "  0 = éxito, 1 = fallo operacional, 2 = solicitud inválida.\n";
}

struct Argumentos final {
    std::optional<fs::path> casa;
    bool leerStdin = false;
    std::optional<fs::path> archivoSolicitud;
};

// Parsea argv. Ante uso incorrecto deja el mensaje en `error` y devuelve true
// para que main lo reporte como solicitud inválida.
bool parsearArgumentos(int argc, char* argv[], Argumentos& argumentos,
                       std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string actual(argv[i]);
        if (actual == "--request") {
            if (argumentos.leerStdin) {
                error = "Argumento repetido: --request";
                return true;
            }
            argumentos.leerStdin = true;
        } else if (actual == "--request-file") {
            if (i + 1 >= argc) {
                error = "--request-file requiere una ruta";
                return true;
            }
            if (argumentos.archivoSolicitud) {
                error = "Argumento repetido: --request-file";
                return true;
            }
            argumentos.archivoSolicitud = fs::path(argv[++i]);
            if (argumentos.archivoSolicitud->empty()) {
                error = "--request-file requiere una ruta no vacía";
                return true;
            }
        } else if (actual == "--home") {
            if (i + 1 >= argc) {
                error = "--home requiere una ruta";
                return true;
            }
            if (argumentos.casa) {
                error = "Argumento repetido: --home";
                return true;
            }
            argumentos.casa = fs::path(argv[++i]);
            if (argumentos.casa->empty()) {
                error = "--home requiere una ruta no vacía";
                return true;
            }
        } else {
            error = "Argumento no reconocido: " + actual;
            return true;
        }
    }

    if (argumentos.leerStdin == argumentos.archivoSolicitud.has_value()) {
        error = "Indica exactamente una fuente: --request o --request-file";
        return true;
    }
    return false;
}

std::string leerTodo(std::istream& entrada) {
    return std::string(std::istreambuf_iterator<char>(entrada),
                       std::istreambuf_iterator<char>());
}

void imprimir(const silvy::ResultadoComando& resultado) {
    std::cout << resultado.respuesta.aTexto() << '\n' << std::flush;
}

}  // namespace

int main(int argc, char* argv[]) {
    // Ayuda y versión en prosa (no son solicitudes del protocolo).
    if (argc == 2) {
        const std::string actual(argv[1]);
        if (actual == "--help" || actual == "-h") {
            std::cout << textoAyuda() << std::flush;
            return 0;
        }
        if (actual == "--version") {
            std::cout << "Silvy " SILVY_VERSION << '\n' << std::flush;
            return 0;
        }
    }

    Argumentos argumentos;
    std::string errorArgumentos;
    const bool usoInvalido =
        parsearArgumentos(argc, argv, argumentos, errorArgumentos);

    // La casa se resuelve aunque argv sea inválido: el error de uso también
    // queda registrado en errores.jsonl, como cualquier otra excepción.
    const fs::path directorioBinario = silvy::directorioDelBinario(
        argc > 0 ? argv[0] : nullptr);
    silvy::FuentesCasa fuentes;
    fuentes.argumento = argumentos.casa;
    if (const char* entorno = std::getenv("SILVY_HOME");
        entorno != nullptr && *entorno != '\0') {
        fuentes.variableEntorno = std::string(entorno);
    }
    fuentes.directorioBinario = directorioBinario;
    const fs::path casa = silvy::resolverCasa(fuentes);

    silvy::ConfigRegistroErrores configuracion;
    configuracion.directorio = casa / "errores";
    silvy::RegistroErrores registro(configuracion);
    silvy::DependenciasNucleo dependencias{nullptr, registro, casa,
                                           directorioBinario};

    if (usoInvalido) {
        const silvy::ResultadoComando resultado =
            silvy::reportarErrorDeTransporte(errorArgumentos, dependencias);
        imprimir(resultado);
        return resultado.codigoSalida;
    }

    // La configuración de la casa se lee antes de despachar: si es
    // inválida, el proceso no arranca con un LLM mal configurado (fail-fast)
    // y el error queda persistido como cualquier otro.
    try {
        dependencias.configLlm = silvy::cargarConfigLlm(casa);
    } catch (const std::exception& error) {
        const silvy::ResultadoComando resultado =
            silvy::reportarErrorDeConfiguracion(silvy::envolver(error),
                                                dependencias);
        imprimir(resultado);
        return resultado.codigoSalida;
    }

    std::string textoSolicitud;
    if (argumentos.leerStdin) {
        textoSolicitud = leerTodo(std::cin);
    } else {
        std::ifstream entrada(*argumentos.archivoSolicitud, std::ios::binary);
        if (!entrada) {
            const silvy::ResultadoComando resultado =
                silvy::reportarErrorDeTransporte(
                    "No se pudo leer el archivo de solicitud: " +
                        argumentos.archivoSolicitud->string(),
                    dependencias);
            imprimir(resultado);
            return resultado.codigoSalida;
        }
        textoSolicitud = leerTodo(entrada);
    }

    const silvy::ResultadoComando resultado =
        silvy::ejecutarSolicitudTexto(textoSolicitud, dependencias);
    imprimir(resultado);
    return resultado.codigoSalida;
}
