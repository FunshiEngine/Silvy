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
#include "Nucleo/NucleoSilvy.h"

#include "Comandos/ErroresComandos.h"
#include "Comandos/PasoAgente.h"
#include "Nucleo/CatalogoComandos.h"
#include "Nucleo/Protocolo.h"

#include <optional>
#include <string>
#include <utility>

namespace silvy {

namespace {

int codigoSalidaDe(CodigoError codigo) {
    switch (codigo) {
        case CodigoError::SOLICITUD_INVALIDA:
            return codigosSalida::kSolicitudInvalida;
        default:
            return codigosSalida::kFalloGeneral;
    }
}

// Extrae un campo string de un JSON posiblemente mal formado (una solicitud
// inválida puede traer cualquier cosa): nunca lanza.
std::string campoSeguro(const nlohmann::json& objeto, const char* clave) {
    if (!objeto.is_object()) {
        return {};
    }
    const auto campo = objeto.find(clave);
    if (campo == objeto.end() || !campo->is_string()) {
        return {};
    }
    return campo->get<std::string>();
}

// El id de eco solo tiene sentido si es un valor JSON válido; para una
// solicitud mal formada se busca en el objeto crudo.
std::optional<nlohmann::json> idSeguro(const nlohmann::json& objeto) {
    if (!objeto.is_object()) {
        return std::nullopt;
    }
    const auto id = objeto.find("id");
    if (id == objeto.end()) {
        return std::nullopt;
    }
    return *id;
}

// Respuesta única de error: persiste, arma ok:false con exactamente el mismo
// objeto que quedó guardado y elige el exit code.
ResultadoComando respuestaDeError(const ErrorSilvy& error,
                                  const std::string& comando,
                                  const std::optional<nlohmann::json>& id,
                                  DependenciasNucleo& dependencias,
                                  const std::string& modo,
                                  const std::string& modelo) {
    ContextoError contexto;
    contexto.comando = comando;
    contexto.modo = modo;
    contexto.modelo = modelo;

    const nlohmann::json registro =
        dependencias.registro.registrar(error, contexto);
    ResultadoComando resultado =
        ResultadoComando::fallo(registro, codigoSalidaDe(error.codigo()));
    resultado.respuesta.id = id;
    return resultado;
}

std::string comandosValidos() {
    std::string texto;
    for (const ComandoSilvy comando : todosLosComandos()) {
        if (!texto.empty()) {
            texto += ", ";
        }
        texto += textoDeComando(comando);
    }
    return texto;
}

}  // namespace

ResultadoComando ejecutarSolicitudJson(const nlohmann::json& solicitud,
                                       DependenciasNucleo& dependencias) {
    try {
        const Solicitud parseada = solicitudDesdeJson(solicitud);
        return ejecutarSolicitud(parseada, dependencias);
    } catch (const std::exception& error) {
        // Aquí solo llegan fallos de parseo/validación: los errores de los
        // comandos ya se capturan en ejecutarSolicitud.
        const ErrorSilvy tipado = envolver(error);
        const nlohmann::json configuracion =
            solicitud.is_object() && solicitud.contains("config")
                ? solicitud["config"]
                : nlohmann::json::object();
        return respuestaDeError(tipado, campoSeguro(solicitud, "command"),
                                idSeguro(solicitud), dependencias,
                                campoSeguro(configuracion, "mode"),
                                campoSeguro(configuracion, "modelName"));
    }
}

ResultadoComando ejecutarSolicitud(const Solicitud& solicitud,
                                   DependenciasNucleo& dependencias) {
    try {
        const std::optional<ComandoSilvy> comando =
            comandoDesdeTexto(solicitud.comando);
        if (!comando) {
            lanzarSolicitudInvalida("Comando desconocido: " + solicitud.comando +
                                    ". Comandos válidos: " + comandosValidos());
        }

        ResultadoComando resultado;
        switch (*comando) {
            case ComandoSilvy::PASO_AGENTE:
                resultado = ejecutarPasoAgente(solicitud, dependencias);
                break;
            case ComandoSilvy::ERRORES_LIST:
                resultado = ejecutarErroresList(solicitud, dependencias);
                break;
            case ComandoSilvy::ERRORES_GET:
                resultado = ejecutarErroresGet(solicitud, dependencias);
                break;
        }
        resultado.respuesta.id = solicitud.id;
        return resultado;
    } catch (const std::exception& error) {
        const ErrorSilvy tipado = envolver(error);
        return respuestaDeError(tipado, solicitud.comando, solicitud.id,
                                dependencias,
                                campoSeguro(solicitud.config, "mode"),
                                campoSeguro(solicitud.config, "modelName"));
    }
}

}  // namespace silvy
