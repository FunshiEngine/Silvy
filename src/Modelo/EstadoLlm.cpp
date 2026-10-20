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
#include "Modelo/EstadoLlm.h"

#include <stdexcept>

namespace silvy {

std::string textoDeEstadoLlm(EstadoLlm estado) {
    switch (estado) {
        case EstadoLlm::SIN_MODELO:
            return "SIN_MODELO";
        case EstadoLlm::DESCARGANDO:
            return "DESCARGANDO";
        case EstadoLlm::LISTO:
            return "LISTO";
        case EstadoLlm::CARGANDO:
            return "CARGANDO";
        case EstadoLlm::SERVIRVIENDO:
            return "SERVIRVIENDO";
        case EstadoLlm::CAIDO:
            return "CAIDO";
    }
    throw std::logic_error("EstadoLlm sin texto de protocolo");
}

std::optional<EstadoLlm> estadoLlmDesdeTexto(const std::string& texto) {
    for (const EstadoLlm estado : todosLosEstadosLlm()) {
        if (textoDeEstadoLlm(estado) == texto) {
            return estado;
        }
    }
    return std::nullopt;
}

std::vector<EstadoLlm> todosLosEstadosLlm() {
    return {EstadoLlm::SIN_MODELO, EstadoLlm::DESCARGANDO, EstadoLlm::LISTO,
            EstadoLlm::CARGANDO, EstadoLlm::SERVIRVIENDO, EstadoLlm::CAIDO};
}

}  // namespace silvy
