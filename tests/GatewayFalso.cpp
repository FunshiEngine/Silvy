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
#include "GatewayFalso.h"

#include <stdexcept>
#include <utility>

namespace silvy {
namespace pruebas {

GatewayFalso::GatewayFalso(std::vector<RespuestaLlm> guion)
    : guion_(std::move(guion)) {}

GatewayFalso GatewayFalso::queFalla(ErrorSilvy error) {
    GatewayFalso gateway(std::vector<RespuestaLlm>{});
    gateway.error_ = std::move(error);
    return gateway;
}

RespuestaLlm GatewayFalso::paso(const PeticionLlm& peticion) {
    peticiones_.push_back(peticion);
    if (error_) {
        throw *error_;
    }
    if (guion_.empty()) {
        throw ErrorSilvy(CodigoError::DESCONOCIDO, false,
                         "El gateway falso se quedó sin respuestas guionizadas");
    }
    RespuestaLlm respuesta = std::move(guion_.front());
    guion_.erase(guion_.begin());
    return respuesta;
}

const std::vector<PeticionLlm>& GatewayFalso::peticiones() const {
    return peticiones_;
}

}  // namespace pruebas
}  // namespace silvy
