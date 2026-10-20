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
#ifndef SILVY_LLM_PETICION_LLM_H
#define SILVY_LLM_PETICION_LLM_H

#include "Llm/LlmGateway.h"
#include "Nucleo/Solicitud.h"

#include <string>

namespace silvy {

// Compone el paso del agente: el prompt de sistema efectivo encabeza la
// conversación y el historial de la CLI viaja tal cual detrás. Los mensajes
// system del historial son contexto adicional de la CLI; no reemplazan al
// prompt de Silvy.
PeticionLlm armarPeticionLlm(const Solicitud& solicitud, std::string promptSistema);

}  // namespace silvy

#endif  // SILVY_LLM_PETICION_LLM_H
