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
#ifndef SILVY_LLM_LLM_GATEWAY_H
#define SILVY_LLM_LLM_GATEWAY_H

#include "Config/ConfigLlm.h"
#include "Nucleo/Protocolo.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace silvy {

// Petición de un paso del agente, ya compuesta en tipos del dominio: el
// gateway no interpreta JSON de protocolo, recibe esto y devuelve el turno.
struct PeticionLlm final {
    // Conversación completa: el prompt de sistema de Silvy es el primer
    // mensaje; detrás viaja el historial de la CLI tal cual.
    std::vector<Mensaje> mensajes;
    std::vector<Herramienta> herramientas;
    // Configuración efectiva del LLM: la de la casa con el override del
    // request encima. El gateway la consume ya resuelta.
    ConfigLlm config;
};

// Turno devuelto por el LLM, independiente del transporte y del proveedor.
struct RespuestaLlm final {
    std::string contenido;
    std::vector<LlamadaHerramienta> llamadas;
    RazonFin razonFin = RazonFin::DESCONOCIDA;
};

// Interfaz del LLM: el núcleo no sabe si detrás hay llama-server local o una
// API remota. Los tests inyectan un gateway falso determinista.
class LlmGateway {
public:
    virtual ~LlmGateway() = default;

    // Ejecuta un paso completo (petición → turno). Los fallos se reportan con
    // ErrorSilvy tipado; el núcleo los captura en su borde.
    virtual RespuestaLlm paso(const PeticionLlm& peticion) = 0;

    // Cancela el paso en curso. Es seguro llamarlo desde otro hilo: el
    // transporte aborta la transferencia y el paso termina con CANCELADO,
    // que nunca es reintentable.
    virtual void cancelar() = 0;
};

}  // namespace silvy

#endif  // SILVY_LLM_LLM_GATEWAY_H
