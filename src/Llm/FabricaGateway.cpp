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
#include "Llm/FabricaGateway.h"

#include "Llm/GatewayOpenAi.h"
#include "Modelo/RuntimeLlm.h"

#include <memory>
#include <string>

namespace silvy {

std::unique_ptr<LlmGateway> crearGateway(const ConfigLlm& config,
                                          RuntimeLlm* runtime) {
    config.validar();

    switch (config.modo) {
        case ModoLlm::REMOTE:
            return std::make_unique<GatewayOpenAi>(config);
        case ModoLlm::LOCAL: {
            if (runtime == nullptr) {
                throw ErrorConfig(
                    "El modo LOCAL necesita el runtime del LLM local, que no "
                    "está disponible en este proceso.");
            }
            // El servidor local se levanta (o se reutiliza) y su puerto es la
            // base de la API compatible OpenAI.
            const int puerto = runtime->asegurarServidor();
            ConfigLlm local = config;
            local.baseUrl = "http://127.0.0.1:" + std::to_string(puerto) + "/v1";
            local.apiKey.clear();
            if (local.modelName.empty()) {
                local.modelName = config.hfFile;
            }
            return std::make_unique<GatewayOpenAi>(local);
        }
        default:
            throw ErrorConfig(
                "El modo de inferencia no corresponde a ningún gateway");
    }
}

}  // namespace silvy
