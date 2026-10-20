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
#include "Llm/PeticionLlm.h"

#include <utility>

namespace silvy {

PeticionLlm armarPeticionLlm(const Solicitud& solicitud,
                             std::string promptSistema, ConfigLlm config) {
    PeticionLlm peticion;
    peticion.mensajes.reserve(solicitud.mensajes.size() + 1);

    Mensaje mensajeSistema;
    mensajeSistema.rol = RolMensaje::SISTEMA;
    mensajeSistema.contenido = std::move(promptSistema);
    peticion.mensajes.push_back(std::move(mensajeSistema));

    peticion.mensajes.insert(peticion.mensajes.end(), solicitud.mensajes.begin(),
                             solicitud.mensajes.end());
    peticion.herramientas = solicitud.herramientas;
    peticion.config = std::move(config);
    return peticion;
}

}  // namespace silvy
