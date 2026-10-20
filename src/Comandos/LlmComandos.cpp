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
#include "Comandos/LlmComandos.h"

#include "Config/ConfigLlm.h"
#include "Errores/Errores.h"
#include "Modelo/RuntimeLlm.h"

#include <nlohmann/json.hpp>

#include <string>

namespace silvy {

namespace {

// El runtime local solo existe en el proceso que lo crea (el binario); los
// tests pueden inyectar uno propio.
RuntimeLlm& runtimeDe(DependenciasNucleo& dependencias) {
    if (dependencias.runtime == nullptr) {
        throw ErrorSilvy(
            CodigoError::NO_CONFIGURADO, false,
            "El runtime del LLM local no está disponible en este proceso: los "
            "comandos del LLM local solo funcionan en el binario de Silvy.");
    }
    return *dependencias.runtime;
}

// Estado del LLM local en JSON.
nlohmann::json estadoEnJson(const EstadoLocal& estado, const ConfigLlm& config) {
    nlohmann::json resultado;
    resultado["mode"] = textoDeModo(config.modo);
    resultado["state"] = textoDeEstadoLlm(estado.estado);
    resultado["model"] = estado.modelo;
    resultado["downloadedBytes"] = estado.bytesDescargados;
    resultado["totalBytes"] = estado.bytesTotales;
    resultado["downloading"] = estado.descargando;
    resultado["serverAlive"] = estado.servidorVivo;
    resultado["port"] = estado.puerto;
    if (estado.porcentajeCarga) {
        resultado["loadPercent"] = *estado.porcentajeCarga;
    } else {
        resultado["loadPercent"] = nullptr;
    }
    if (estado.codigoError.empty()) {
        resultado["lastError"] = nullptr;
    } else {
        nlohmann::json error;
        error["code"] = estado.codigoError;
        error["message"] = estado.mensajeError;
        resultado["lastError"] = std::move(error);
    }
    return resultado;
}

}  // namespace

ResultadoComando ejecutarLlmStatus(const Solicitud& solicitud,
                                   DependenciasNucleo& dependencias) {
    RuntimeLlm& runtime = runtimeDe(dependencias);
    return ResultadoComando::exito(
        estadoEnJson(runtime.estado(), runtime.config()));
}

ResultadoComando ejecutarLlmDownload(const Solicitud& solicitud,
                                     DependenciasNucleo& dependencias) {
    RuntimeLlm& runtime = runtimeDe(dependencias);
    runtime.iniciarDescarga();
    return ResultadoComando::exito(
        estadoEnJson(runtime.estado(), runtime.config()));
}

ResultadoComando ejecutarLlmCancel(const Solicitud& solicitud,
                                   DependenciasNucleo& dependencias) {
    RuntimeLlm& runtime = runtimeDe(dependencias);
    runtime.cancelarDescarga();
    return ResultadoComando::exito(
        estadoEnJson(runtime.estado(), runtime.config()));
}

ResultadoComando ejecutarLlmMode(const Solicitud& solicitud,
                                 DependenciasNucleo& dependencias) {
    RuntimeLlm& runtime = runtimeDe(dependencias);

    if (!solicitud.argumentos.is_object() ||
        !solicitud.argumentos.contains("mode") ||
        !solicitud.argumentos["mode"].is_string()) {
        throw ErrorSilvy(CodigoError::SOLICITUD_INVALIDA, false,
                         "llm.mode necesita el argumento 'mode' con el valor "
                         "LOCAL o REMOTE");
    }

    const std::string texto = solicitud.argumentos["mode"].get<std::string>();
    const std::optional<ModoLlm> modo = modoDesdeTexto(texto);
    if (!modo) {
        throw ErrorSilvy(CodigoError::SOLICITUD_INVALIDA, false,
                         "'mode' debe ser LOCAL o REMOTE");
    }

    // El resto de los argumentos pisan la configuración: así se puede pasar
    // al modo remoto con su base, modelo y clave en la misma solicitud.
    ConfigLlm config = runtime.config().conOverride(solicitud.argumentos);
    config.modo = *modo;
    config.validar();

    // El modo queda escrito en la casa: el próximo arranque lo toma de ahí.
    guardarConfigLlm(config, dependencias.casa);
    runtime.aplicarConfig(config);
    // La configuración del núcleo también cambia: los pasos siguientes usan
    // el modo nuevo sin pedir un reinicio.
    dependencias.configLlm = config;

    if (modo == ModoLlm::REMOTE) {
        // Al salir del modo local no queda ningún servidor levantado.
        runtime.detenerServidor();
    }

    return ResultadoComando::exito(
        estadoEnJson(runtime.estado(), runtime.config()));
}

}  // namespace silvy
