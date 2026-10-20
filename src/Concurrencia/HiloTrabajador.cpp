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
#include "Concurrencia/HiloTrabajador.h"

namespace silvy {

HiloTrabajador::HiloTrabajador(std::string nombre)
    : nombre_(std::move(nombre)), hilo_(&HiloTrabajador::bucle, this) {}

HiloTrabajador::~HiloTrabajador() {
    if (estaCorriendo()) {
        detener();
    }
}

bool HiloTrabajador::encolar(std::function<void()> trabajo) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (detener_) return false;
    cola_.push(std::move(trabajo));
    cv_.notify_one();
    return true;
}

void HiloTrabajador::detener() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        detener_ = true;
    }
    cv_.notify_all();
    if (hilo_.joinable()) {
        hilo_.join();
    }
}

bool HiloTrabajador::estaCorriendo() const noexcept { return hilo_.joinable(); }

void HiloTrabajador::bucle() {
    while (true) {
        std::function<void()> trabajo;
        {
            std::unique_lock<std::mutex> lk(mutex_);
            cv_.wait(lk, [this] { return detener_ || !cola_.empty(); });
            if (detener_ && cola_.empty()) break;
            if (cola_.empty()) continue;
            trabajo = std::move(cola_.front());
            cola_.pop();
        }
        if (trabajo) {
            try {
                trabajo();
            } catch (...) {
                // El trabajo debe manejar sus excepciones; aquí no se propagan.
            }
        }
    }
}

}  // namespace silvy