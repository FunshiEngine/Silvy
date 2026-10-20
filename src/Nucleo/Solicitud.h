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
#ifndef SILVY_NUCLEO_SOLICITUD_H
#define SILVY_NUCLEO_SOLICITUD_H

#include "Nucleo/Protocolo.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace silvy {

// Una solicitud = un paso. Llega de la CLI con el historial completo y el
// catálogo de herramientas; Silvy no guarda estado entre solicitudes.
struct Solicitud final {
    std::string comando;
    // Argumentos de los comandos de control (errores.list, errores.get, ...).
    nlohmann::json argumentos = nlohmann::json::object();
    // Override por request de la configuración del LLM; lo ausente usa el
    // config de Silvy.
    nlohmann::json config = nlohmann::json::object();
    // Override del prompt de sistema; si falta se usa el de la casa.
    std::optional<std::string> promptSistema;
    std::vector<Mensaje> mensajes;
    std::vector<Herramienta> herramientas;
    // Límite de auto-corrección, referencia para el loop de la CLI.
    int maxIteraciones = 3;
    // Eco opcional del identificador de la solicitud.
    std::optional<nlohmann::json> id;
};

// Parsea y valida la solicitud completa. Lanza SOLICITUD_INVALIDA con un
// mensaje accionable ante cualquier campo con forma incorrecta.
Solicitud solicitudDesdeJson(const nlohmann::json& objeto);

}  // namespace silvy

#endif  // SILVY_NUCLEO_SOLICITUD_H
