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
#ifndef SILVY_MODELO_ESTADO_LLM_H
#define SILVY_MODELO_ESTADO_LLM_H

#include <optional>
#include <string>
#include <vector>

namespace silvy {

// Estado del LLM local. Es un tipo cerrado y no un conjunto de banderas:
// el supervisor del servidor y el descargador son los únicos que lo
// transicionan, y el estado se lee desde `llm.status`.
enum class EstadoLlm {
    // No hay modelo en disco: hay que descargarlo.
    SIN_MODELO,
    // Descarga del GGUF en curso (el `.part` conserva lo avanzado).
    DESCARGANDO,
    // El GGUF está completo en disco; el servidor local está detenido.
    LISTO,
    // `llama-server` arrancó y se espera a que responda el health-check.
    CARGANDO,
    // El servidor local responde y atiende inferencia.
    SERVIRVIENDO,
    // El proceso local murió o no llegó a levantar: hay un error persistido.
    CAIDO,
};

// Texto de protocolo de un estado ("DESCARGANDO", "LISTO", ...).
std::string textoDeEstadoLlm(EstadoLlm estado);

// Lectura inversa; sin valor si el texto no corresponde a un estado conocido.
std::optional<EstadoLlm> estadoLlmDesdeTexto(const std::string& texto);

// Todos los estados, en el orden del enum. Lo usan las pruebas y el listado
// del catálogo de comandos.
std::vector<EstadoLlm> todosLosEstadosLlm();

}  // namespace silvy

#endif  // SILVY_MODELO_ESTADO_LLM_H
