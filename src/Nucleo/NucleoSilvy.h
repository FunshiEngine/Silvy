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
#ifndef SILVY_NUCLEO_NUCLEO_SILVY_H
#define SILVY_NUCLEO_NUCLEO_SILVY_H

#include "Nucleo/DependenciasNucleo.h"
#include "Nucleo/Respuesta.h"
#include "Nucleo/Solicitud.h"

#include <nlohmann/json.hpp>

namespace silvy {

// Punto de entrada del núcleo desde el transporte: recibe la solicitud JSON
// cruda, la valida, despacha el comando y captura una sola vez los errores
// (normalización + persistencia + respuesta ok:false con exit code).
ResultadoComando ejecutarSolicitudJson(const nlohmann::json& solicitud,
                                       DependenciasNucleo& dependencias);

// Núcleo tipado para consumidores embebidos (tests incluidos): valida el
// comando y despacha; los errores del comando se capturan y persisten igual
// que en el borde JSON.
ResultadoComando ejecutarSolicitud(const Solicitud& solicitud,
                                   DependenciasNucleo& dependencias);

}  // namespace silvy

#endif  // SILVY_NUCLEO_NUCLEO_SILVY_H
