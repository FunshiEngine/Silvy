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
#ifndef SILVY_COMANDOS_PASO_AGENTE_H
#define SILVY_COMANDOS_PASO_AGENTE_H

#include "Nucleo/DependenciasNucleo.h"
#include "Nucleo/Respuesta.h"
#include "Nucleo/Solicitud.h"

namespace silvy {

// Ejecuta un paso del agente: resuelve el prompt de sistema, compone la
// petición y delega el turno en el gateway. Devuelve el resultado en el
// formato de respuesta del protocolo.
ResultadoComando ejecutarPasoAgente(const Solicitud& solicitud,
                                    DependenciasNucleo& dependencias);

}  // namespace silvy

#endif  // SILVY_COMANDOS_PASO_AGENTE_H
