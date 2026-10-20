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
#include "Llm/FabricaGateway.h"
#include "Llm/PeticionLlm.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <utility>

namespace silvy {

ResultadoComando ejecutarPasoAgente(const Solicitud& solicitud,
                                    DependenciasNucleo& dependencias) {
    // La configuración efectiva es la de la casa con el override del request
    // encima: se resuelve en el núcleo para que el gateway reciba una
    // configuración ya validable y el error persistido traiga el modo y el
    // modelo que se usaron.
    const ConfigLlm config =
        dependencias.configLlm.conOverride(solicitud.config);

    // El gateway inyectado manda (los tests usan uno falso); si no hay, se
    // construye el que corresponde a la configuración efectiva.
    std::unique_ptr<LlmGateway> gatewayPropio;
    LlmGateway* gateway = dependencias.gateway;
    if (gateway == nullptr) {
        gatewayPropio = crearGateway(config, dependencias.runtime);
        gateway = gatewayPropio.get();
    }

    const std::string promptSistema = cargarPromptSistema(
        solicitud.promptSistema, dependencias.casa,
        dependencias.directorioBinario);
    const PeticionLlm peticion =
        armarPeticionLlm(solicitud, promptSistema, config);
    const RespuestaLlm turno = gateway->paso(peticion);

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
