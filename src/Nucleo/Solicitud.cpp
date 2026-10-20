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
#include "Nucleo/Solicitud.h"

namespace silvy {

Solicitud solicitudDesdeJson(const nlohmann::json& objeto) {
    if (!objeto.is_object()) {
        lanzarSolicitudInvalida("La solicitud debe ser un objeto JSON");
    }

    Solicitud solicitud;
    solicitud.comando = textoRequerido(objeto, "command", "solicitud");
    if (solicitud.comando.empty()) {
        lanzarSolicitudInvalida("solicitud.command no puede estar vacío");
    }

    if (objeto.contains("args")) {
        if (!objeto["args"].is_object()) {
            lanzarSolicitudInvalida("solicitud.args debe ser un objeto JSON");
        }
        solicitud.argumentos = objeto["args"];
    }

    if (objeto.contains("config")) {
        if (!objeto["config"].is_object()) {
            lanzarSolicitudInvalida("solicitud.config debe ser un objeto JSON");
        }
        solicitud.config = objeto["config"];
    }

    if (objeto.contains("systemPrompt")) {
        if (!objeto["systemPrompt"].is_string()) {
            lanzarSolicitudInvalida("solicitud.systemPrompt debe ser un string");
        }
        solicitud.promptSistema = objeto["systemPrompt"].get<std::string>();
    }

    if (objeto.contains("messages")) {
        if (!objeto["messages"].is_array()) {
            lanzarSolicitudInvalida("solicitud.messages debe ser un array");
        }
        solicitud.mensajes.reserve(objeto["messages"].size());
        for (const auto& mensaje : objeto["messages"]) {
            solicitud.mensajes.push_back(mensajeDesdeJson(mensaje));
        }
    }

    if (objeto.contains("tools")) {
        if (!objeto["tools"].is_array()) {
            lanzarSolicitudInvalida("solicitud.tools debe ser un array");
        }
        solicitud.herramientas.reserve(objeto["tools"].size());
        for (const auto& herramienta : objeto["tools"]) {
            solicitud.herramientas.push_back(herramientaDesdeJson(herramienta));
        }
    }

    if (objeto.contains("maxIterations")) {
        if (!objeto["maxIterations"].is_number_integer() ||
            objeto["maxIterations"].get<long long>() < 1) {
            lanzarSolicitudInvalida(
                "solicitud.maxIterations debe ser un entero mayor o igual a 1");
        }
        solicitud.maxIteraciones = objeto["maxIterations"].get<int>();
    }

    if (objeto.contains("id")) {
        solicitud.id = objeto["id"];
    }

    return solicitud;
}

}  // namespace silvy
