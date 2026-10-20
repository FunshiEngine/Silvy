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
#ifndef SILVY_NUCLEO_CATALOGO_COMANDOS_H
#define SILVY_NUCLEO_CATALOGO_COMANDOS_H

#include <optional>
#include <string>
#include <vector>

namespace silvy {

// Comandos que acepta Silvy. Todo comando nuevo se agrega aquí, al mapa de
// texto y al switch exhaustivo del dispatch.
enum class ComandoSilvy {
    PASO_AGENTE,   // "agent.step"
    ERRORES_LIST,  // "errores.list"
    ERRORES_GET,   // "errores.get"
    LLM_STATUS,    // "llm.status"
    LLM_DOWNLOAD,  // "llm.download"
    LLM_CANCEL,    // "llm.cancel"
    LLM_MODE,      // "llm.mode"
};

// Texto de protocolo de un comando.
std::string textoDeComando(ComandoSilvy comando);

// Lectura inversa; sin valor si el texto no corresponde a un comando.
std::optional<ComandoSilvy> comandoDesdeTexto(const std::string& texto);

// Todos los comandos, en el orden del enum (para mensajes de validación y
// pruebas).
std::vector<ComandoSilvy> todosLosComandos();

}  // namespace silvy

#endif  // SILVY_NUCLEO_CATALOGO_COMANDOS_H
