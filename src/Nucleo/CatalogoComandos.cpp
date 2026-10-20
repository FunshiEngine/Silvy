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
#include "Nucleo/CatalogoComandos.h"

#include <stdexcept>

namespace silvy {

std::string textoDeComando(ComandoSilvy comando) {
    switch (comando) {
        case ComandoSilvy::PASO_AGENTE:
            return "agent.step";
        case ComandoSilvy::ERRORES_LIST:
            return "errores.list";
        case ComandoSilvy::ERRORES_GET:
            return "errores.get";
        case ComandoSilvy::LLM_STATUS:
            return "llm.status";
        case ComandoSilvy::LLM_DOWNLOAD:
            return "llm.download";
        case ComandoSilvy::LLM_CANCEL:
            return "llm.cancel";
        case ComandoSilvy::LLM_MODE:
            return "llm.mode";
    }
    throw std::logic_error("Comando sin texto de protocolo");
}

std::optional<ComandoSilvy> comandoDesdeTexto(const std::string& texto) {
    for (const ComandoSilvy comando : todosLosComandos()) {
        if (textoDeComando(comando) == texto) {
            return comando;
        }
    }
    return std::nullopt;
}

std::vector<ComandoSilvy> todosLosComandos() {
    return {ComandoSilvy::PASO_AGENTE,  ComandoSilvy::ERRORES_LIST,
            ComandoSilvy::ERRORES_GET,   ComandoSilvy::LLM_STATUS,
            ComandoSilvy::LLM_DOWNLOAD,  ComandoSilvy::LLM_CANCEL,
            ComandoSilvy::LLM_MODE};
}

}  // namespace silvy
