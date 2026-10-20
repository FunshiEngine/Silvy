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
#include "Nucleo/Respuesta.h"

#include <utility>

namespace silvy {

Respuesta Respuesta::ok(nlohmann::json resultado) {
    Respuesta respuesta;
    respuesta.correcta = true;
    respuesta.resultado = std::move(resultado);
    return respuesta;
}

Respuesta Respuesta::fallo(nlohmann::json error) {
    Respuesta respuesta;
    respuesta.correcta = false;
    respuesta.error = std::move(error);
    return respuesta;
}

nlohmann::json Respuesta::aJson() const {
    nlohmann::json objeto;
    objeto["apiVersion"] = kVersionApiSilvy;
    objeto["ok"] = correcta;
    objeto["error"] = error;
    objeto["result"] = resultado;
    if (id) {
        objeto["id"] = *id;
    }
    return objeto;
}

std::string Respuesta::aTexto() const { return aJson().dump(); }

ResultadoComando ResultadoComando::exito(nlohmann::json resultado) {
    return {Respuesta::ok(std::move(resultado)), codigosSalida::kExito};
}

ResultadoComando ResultadoComando::fallo(nlohmann::json error, int codigoSalida) {
    return {Respuesta::fallo(std::move(error)), codigoSalida};
}

}  // namespace silvy
