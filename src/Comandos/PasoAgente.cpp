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
#include "Comandos/PasoAgente.h"

#include "Config/ConfigSilvy.h"
#include "Llm/PeticionLlm.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace silvy {

ResultadoComando ejecutarPasoAgente(const Solicitud& solicitud,
                                    DependenciasNucleo& dependencias) {
    if (dependencias.gateway == nullptr) {
        throw ErrorConfig(
            "No hay gateway LLM configurado: el modo de inferencia no está "
            "disponible");
    }

    const std::string promptSistema = cargarPromptSistema(
        solicitud.promptSistema, dependencias.casa,
        dependencias.directorioBinario);
    const PeticionLlm peticion = armarPeticionLlm(solicitud, promptSistema);
    const RespuestaLlm turno = dependencias.gateway->paso(peticion);

    nlohmann::json resultado;
    resultado["content"] = turno.contenido;
    nlohmann::json llamadas = nlohmann::json::array();
    for (const LlamadaHerramienta& llamada : turno.llamadas) {
        llamadas.push_back(llamadaAJson(llamada));
    }
    resultado["toolCalls"] = std::move(llamadas);
    resultado["finishReason"] = textoDeRazonFin(turno.razonFin);

    return ResultadoComando::exito(std::move(resultado));
}

}  // namespace silvy
