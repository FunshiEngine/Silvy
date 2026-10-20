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
#ifndef SILVY_TESTS_SERVIDOR_FALSO_H
#define SILVY_TESTS_SERVIDOR_FALSO_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace silvy {
namespace pruebas {

// Respuesta guionizada del servidor falso.
struct RespuestaFalsa final {
    // Estado HTTP de la respuesta.
    int estado = 200;
    // Cuerpo completo (respuesta sin streaming).
    std::string cuerpo;
    // Trozos a enviar por streaming SSE; si hay alguno, el servidor responde
    // con un proveedor por bloques.
    std::vector<std::string> trozos;
    // Espera entre trozos (para simular un stream lento).
    int esperaPorTrozoMs = 0;
    // Espera antes de responder (para simular un servidor que no contesta).
    int esperaAntesDeResponderMs = 0;
    // Tipo de contenido de la respuesta.
    std::string tipoContenido = "application/json";
};

// Servidor HTTP local que habla como una API compatible OpenAI. Cada
// petición consume la siguiente respuesta guionizada.
class ServidorFalso final {
public:
    // Petición registrada por el servidor.
    struct PeticionRecibida final {
        std::string ruta;
        std::string cuerpo;
        std::string autorizacion;
    };

    ServidorFalso();
    ~ServidorFalso();

    ServidorFalso(const ServidorFalso&) = delete;
    ServidorFalso& operator=(const ServidorFalso&) = delete;

    // Encola una respuesta para la próxima petición.
    void encolar(RespuestaFalsa respuesta);

    // Arranca el servidor en un puerto libre de la máquina local.
    void arrancar();

    // Puerto asignado y base URL con el prefijo "/v1".
    int puerto() const;
    std::string baseUrl() const;

    // Peticiones recibidas, en orden.
    const std::vector<PeticionRecibida>& peticiones() const;

    // Detiene el servidor y espera al hilo.
    void detener();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace pruebas
}  // namespace silvy

#endif  // SILVY_TESTS_SERVIDOR_FALSO_H
