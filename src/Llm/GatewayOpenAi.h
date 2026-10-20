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
#ifndef SILVY_LLM_GATEWAY_OPENAI_H
#define SILVY_LLM_GATEWAY_OPENAI_H

#include "Config/ConfigLlm.h"
#include "Llm/LlmGateway.h"
#include "Llm/ProtocoloOpenAi.h"

#include <atomic>
#include <string>

namespace silvy {

// Gateway contra una API compatible OpenAI: HTTP + SSE, tool-calling real,
// clasificación de errores por estado/red y reintentos con backoff solo
// sobre errores reintentables.
class GatewayOpenAi final : public LlmGateway {
public:
    explicit GatewayOpenAi(ConfigLlm config);

    RespuestaLlm paso(const PeticionLlm& peticion) override;

    // Cancela el paso en curso. Es seguro llamarlo desde otro hilo: la
    // transferencia se aborta y el paso termina con CANCELADO.
    void cancelar() override;

private:
    RespuestaLlm intentar(const DestinoApi& destino, const std::string& ruta,
                           const std::string& cuerpo);

    ConfigLlm config_;
    std::atomic<bool> cancelado_{false};
};

}  // namespace silvy

#endif  // SILVY_LLM_GATEWAY_OPENAI_H
