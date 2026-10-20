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
#ifndef SILVY_ERRORES_ERRORES_HTTP_H
#define SILVY_ERRORES_ERRORES_HTTP_H

#include "Errores/Errores.h"

#include <string>

namespace silvy {

// Clasifica una respuesta HTTP no-2xx de la API del LLM. El mensaje incluye
// el estado y un fragmento del cuerpo (máx. ~300 caracteres) para que el
// usuario vea la causa sin abrir logs. En HTTP 400 se inspecciona el cuerpo
// para distinguir el agotamiento de contexto de una solicitud mal formada.
ErrorSilvy desdeHttp(int estado, const std::string& cuerpo, bool esLocal);

// Clasifica un fallo de transporte sin respuesta HTTP (conexión rechazada,
// DNS, TLS, corte de lectura). Siempre reintentable: el servidor puede volver.
ErrorSilvy desdeRed(const std::string& detalle, bool esLocal);

}  // namespace silvy

#endif  // SILVY_ERRORES_ERRORES_HTTP_H
