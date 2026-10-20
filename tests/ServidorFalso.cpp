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
#include "ServidorFalso.h"

#include <httplib.h>

#include <chrono>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace silvy {
namespace pruebas {

struct ServidorFalso::Impl final {
    httplib::Server servidor;
    std::thread hilo;
    std::deque<RespuestaFalsa> guion;
    std::vector<PeticionRecibida> peticiones;
    std::mutex candado;
    int puerto = 0;
};

ServidorFalso::ServidorFalso() : impl_(std::make_unique<Impl>()) {}

ServidorFalso::~ServidorFalso() {
    detener();
}

void ServidorFalso::encolar(RespuestaFalsa respuesta) {
    std::lock_guard<std::mutex> guard(impl_->candado);
    impl_->guion.push_back(std::move(respuesta));
}

void ServidorFalso::arrancar() {
    impl_->servidor.Post("/v1/chat/completions",
                         [this](const httplib::Request& peticion,
                                httplib::Response& respuesta) {
                             PeticionRecibida registrada;
                             registrada.ruta = peticion.path;
                             registrada.cuerpo = peticion.body;
                             registrada.autorizacion =
                                 peticion.get_header_value("Authorization");

                             RespuestaFalsa guionada;
                             {
                                 std::lock_guard<std::mutex> guard(
                                     impl_->candado);
                                 impl_->peticiones.push_back(registrada);
                                 if (impl_->guion.empty()) {
                                     respuesta.status = 500;
                                     respuesta.set_content(
                                         R"({"error":{"message":"sin guion"}})",
                                         "application/json");
                                     return;
                                 }
                                 guionada = impl_->guion.front();
                                 impl_->guion.pop_front();
                             }

                             if (guionada.esperaAntesDeResponderMs > 0) {
                                 std::this_thread::sleep_for(
                                     std::chrono::milliseconds(
                                         guionada.esperaAntesDeResponderMs));
                             }

                             if (guionada.trozos.empty()) {
                                 respuesta.status = guionada.estado;
                                 respuesta.set_content(guionada.cuerpo,
                                                       guionada.tipoContenido);
                                 return;
                             }

                             respuesta.status = guionada.estado;
                             respuesta.set_chunked_content_provider(
                                 guionada.tipoContenido,
                                 [trozos = guionada.trozos,
                                  espera = guionada.esperaPorTrozoMs,
                                  indice = std::make_shared<std::size_t>(0)](
                                     std::size_t, httplib::DataSink& sumidero) {
                                     if (*indice >= trozos.size()) {
                                         sumidero.done();
                                         return false;
                                     }
                                     if (espera > 0) {
                                         std::this_thread::sleep_for(
                                             std::chrono::milliseconds(espera));
                                     }
                                     const std::string& trozo =
                                         trozos[*indice];
                                     *indice += 1;
                                     if (!sumidero.write(trozo.data(),
                                                         trozo.size())) {
                                         return false;
                                     }
                                     return true;
                                 });
                         });

    impl_->puerto = impl_->servidor.bind_to_any_port("127.0.0.1");
    if (impl_->puerto <= 0) {
        throw std::runtime_error(
            "No se pudo abrir un puerto para el servidor falso");
    }
    impl_->hilo = std::thread([this] { impl_->servidor.listen_after_bind(); });
}

int ServidorFalso::puerto() const {
    return impl_->puerto;
}

std::string ServidorFalso::baseUrl() const {
    return "http://127.0.0.1:" + std::to_string(impl_->puerto) + "/v1";
}

const std::vector<ServidorFalso::PeticionRecibida>&
ServidorFalso::peticiones() const {
    return impl_->peticiones;
}

void ServidorFalso::detener() {
    if (impl_->hilo.joinable()) {
        impl_->servidor.stop();
        impl_->hilo.join();
    }
}

}  // namespace pruebas
}  // namespace silvy
