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
#ifndef SILVY_CONCURRENCIA_HILO_TRABAJADOR_H
#define SILVY_CONCURRENCIA_HILO_TRABAJADOR_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <utility>

namespace silvy {

// Hilo dedicado con cola de trabajos y parada con vaciado.
// Un job = std::function<void()>. El hilo lo ejecuta y, si se pide
// detener, vacía la cola antes de terminar (best-effort con timeout).
class HiloTrabajador final {
public:
    explicit HiloTrabajador(std::string nombre = "trabajador");
    ~HiloTrabajador();

    HiloTrabajador(const HiloTrabajador&) = delete;
    HiloTrabajador& operator=(const HiloTrabajador&) = delete;

    // Encola un trabajo. No bloquea. Si el hilo ya se está deteniendo,
    // devuelve false y no encola.
    bool encolar(std::function<void()> trabajo);

    // Pide detención y espera hasta que el hilo termine (vacía la cola).
    void detener();

    // True mientras el hilo dedicado siga unido al proceso (corriendo o
    // terminando).
    bool estaCorriendo() const noexcept;

private:
    std::string nombre_;
    std::atomic<bool> detener_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<std::function<void()>> cola_;
    std::thread hilo_;

    void bucle();
};

}  // namespace silvy

#endif  // SILVY_CONCURRENCIA_HILO_TRABAJADOR_H