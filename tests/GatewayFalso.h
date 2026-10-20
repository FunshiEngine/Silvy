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
#ifndef SILVY_TESTS_GATEWAY_FALSO_H
#define SILVY_TESTS_GATEWAY_FALSO_H

#include "Errores/Errores.h"
#include "Llm/LlmGateway.h"

#include <atomic>
#include <memory>
#include <optional>
#include <vector>

namespace silvy {
namespace pruebas {

// Gateway determinista para tests: devuelve las respuestas guionizadas en
// orden, registra cada petición recibida y puede fallar con un error tipado.
class GatewayFalso final : public LlmGateway {
public:
    explicit GatewayFalso(std::vector<RespuestaLlm> guion);

    // Gateway que lanza el error indicado en cada paso.
    static GatewayFalso queFalla(ErrorSilvy error);

    RespuestaLlm paso(const PeticionLlm& peticion) override;

    // El gateway falso es instantáneo: solo registra que se pidió cancelar.
    void cancelar() override;

    // true si se pidió cancelar.
    bool cancelado() const;

    // Peticiones recibidas, en orden.
    const std::vector<PeticionLlm>& peticiones() const;

private:
    std::vector<RespuestaLlm> guion_;
    std::vector<PeticionLlm> peticiones_;
    std::optional<ErrorSilvy> error_;
    // Compartirlo por puntero mantiene al gateway copiable, que es lo que
    // necesitan las pruebas que lo construyen y devuelven por valor.
    std::shared_ptr<std::atomic<bool>> cancelado_ =
        std::make_shared<std::atomic<bool>>(false);
};

}  // namespace pruebas
}  // namespace silvy

#endif  // SILVY_TESTS_GATEWAY_FALSO_H
