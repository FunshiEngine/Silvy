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
#include "Comandos/LlmComandos.h"
#include "Comandos/PasoAgente.h"
#include "Config/ConfigLlm.h"
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

// Contexto del error persistido: el request manda (puede pisar la casa) y,
// si no lo dice, se usa la configuración de la casa.
std::string modoDeContexto(const std::string& modoDelRequest,
                           const DependenciasNucleo& dependencias) {
    if (!modoDelRequest.empty()) {
        return modoDelRequest;
    }
    return textoDeModo(dependencias.configLlm.modo);
}

std::string modeloDeContexto(const std::string& modeloDelRequest,
                              const DependenciasNucleo& dependencias) {
    if (!modeloDelRequest.empty()) {
        return modeloDelRequest;
    }
    return dependencias.configLlm.modelName;
}

// Error de parseo/validación de una solicitud cruda: conserva el comando y el
// id que se hayan podido leer para que la respuesta sea accionable.
ResultadoComando respuestaDeSolicitudInvalida(
    const nlohmann::json& solicitud, const ErrorSilvy& error,
    DependenciasNucleo& dependencias) {
    const nlohmann::json configuracion =
        solicitud.is_object() && solicitud.contains("config")
            ? solicitud["config"]
            : nlohmann::json::object();
    return respuestaDeError(error, campoSeguro(solicitud, "command"),
                            idSeguro(solicitud), dependencias,
                            modoDeContexto(
                                campoSeguro(configuracion, "mode"), dependencias),
                            modeloDeContexto(
                                campoSeguro(configuracion, "modelName"),
                                dependencias));
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
    } catch (const nlohmann::json::exception& error) {
        // Un valor fuera de rango o con forma inesperada que se escape de la
        // validación sigue siendo una solicitud inválida, no un fallo interno.
        const ErrorSilvy tipado(
            CodigoError::SOLICITUD_INVALIDA, false,
            std::string("Solicitud inválida: ") + error.what());
        return respuestaDeSolicitudInvalida(solicitud, tipado, dependencias);
    } catch (const std::exception& error) {
        // Aquí solo llegan fallos de parseo/validación: los errores de los
        // comandos ya se capturan en ejecutarSolicitud.
        return respuestaDeSolicitudInvalida(solicitud, envolver(error),
                                            dependencias);
    }
}

ResultadoComando ejecutarSolicitudTexto(const std::string& texto,
                                        DependenciasNucleo& dependencias) {
    try {
        return ejecutarSolicitudJson(nlohmann::json::parse(texto), dependencias);
    } catch (const nlohmann::json::exception& error) {
        return reportarErrorDeTransporte(
            std::string("JSON inválido: ") + error.what(), dependencias);
    }
}

ResultadoComando reportarErrorDeTransporte(
    const std::string& mensaje, DependenciasNucleo& dependencias) {
    const ErrorSilvy error(CodigoError::SOLICITUD_INVALIDA, false, mensaje);
    return respuestaDeError(error, {}, std::nullopt, dependencias, {}, {});
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
            case ComandoSilvy::LLM_STATUS:
                resultado = ejecutarLlmStatus(solicitud, dependencias);
                break;
            case ComandoSilvy::LLM_DOWNLOAD:
                resultado = ejecutarLlmDownload(solicitud, dependencias);
                break;
            case ComandoSilvy::LLM_CANCEL:
                resultado = ejecutarLlmCancel(solicitud, dependencias);
                break;
            case ComandoSilvy::LLM_MODE:
                resultado = ejecutarLlmMode(solicitud, dependencias);
                break;
        }
        resultado.respuesta.id = solicitud.id;
        return resultado;
    } catch (const std::exception& error) {
        const ErrorSilvy tipado = envolver(error);
        return respuestaDeError(tipado, solicitud.comando, solicitud.id,
                                dependencias,
                                modoDeContexto(
                                    campoSeguro(solicitud.config, "mode"),
                                    dependencias),
                                modeloDeContexto(
                                    campoSeguro(solicitud.config, "modelName"),
                                    dependencias));
    }
}

ResultadoComando reportarErrorDeConfiguracion(
    const ErrorSilvy& error, DependenciasNucleo& dependencias) {
    return respuestaDeError(error, {}, std::nullopt, dependencias,
                             modoDeContexto({}, dependencias),
                             modeloDeContexto({}, dependencias));
}

}  // namespace silvy
