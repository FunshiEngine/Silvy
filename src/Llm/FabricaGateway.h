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
#ifndef SILVY_LLM_FABRICA_GATEWAY_H
#define SILVY_LLM_FABRICA_GATEWAY_H

#include "Config/ConfigLlm.h"
#include "Llm/LlmGateway.h"

#include <memory>

namespace silvy {

class RuntimeLlm;

// Construye el gateway que corresponde a la configuración efectiva: valida
// la configuración (fail-fast, con todos los problemas juntos) y elige la
// implementación según el modo. Lanza ErrorConfig si la configuración no
// permite hablar con un LLM.
//
// En modo LOCAL el runtime local es el que levanta el servidor y devuelve el
// puerto; sin runtime no hay gateway local posible y se lanza ErrorConfig.
std::unique_ptr<LlmGateway> crearGateway(const ConfigLlm& config,
                                          RuntimeLlm* runtime = nullptr);

}  // namespace silvy

#endif  // SILVY_LLM_FABRICA_GATEWAY_H
