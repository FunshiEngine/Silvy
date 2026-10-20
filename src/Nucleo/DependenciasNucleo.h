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
#ifndef SILVY_NUCLEO_DEPENDENCIAS_NUCLEO_H
#define SILVY_NUCLEO_DEPENDENCIAS_NUCLEO_H

#include "Errores/RegistroErrores.h"
#include "Llm/LlmGateway.h"

#include <filesystem>

namespace silvy {

// Recursos inyectados en el núcleo. El gateway puede faltar: el producto que
// todavía no tiene modo LLM responde NO_CONFIGURADO. El registro siempre
// existe, de modo que todo error devuelto quedó persistido.
struct DependenciasNucleo final {
    LlmGateway* gateway;
    RegistroErrores& registro;
    std::filesystem::path casa;
    std::filesystem::path directorioBinario;
};

}  // namespace silvy

#endif  // SILVY_NUCLEO_DEPENDENCIAS_NUCLEO_H
