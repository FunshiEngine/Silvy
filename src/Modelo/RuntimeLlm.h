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
#ifndef SILVY_MODELO_RUNTIME_LLM_H
#define SILVY_MODELO_RUNTIME_LLM_H

#include "Config/ConfigLlm.h"
#include "Modelo/EstadoLlm.h"
#include "Modelo/SupervisorLlama.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace silvy {

// Instantánea del estado del LLM local. Es lo que devuelve `llm.status`.
struct EstadoLocal final {
    // Estado tipado del LLM local.
    EstadoLlm estado = EstadoLlm::SIN_MODELO;
    // Nombre del GGUF configurado.
    std::string modelo;
    // Bytes del modelo ya en disco (parcial o completo).
    std::uint64_t bytesDescargados = 0;
    // Bytes esperados; 0 si el servidor no lo informó.
    std::uint64_t bytesTotales = 0;
    // true si hay una descarga en curso.
    bool descargando = false;
    // true si el servidor local está en pie.
    bool servidorVivo = false;
    // Puerto donde escucha el servidor local (0 si no está).
    int puerto = 0;
    // Porcentaje de carga que dice el log del servidor.
    std::optional<int> porcentajeCarga;
    // Código del último error (vacío si no hubo).
    std::string codigoError;
    // Mensaje del último error (vacío si no hubo).
    std::string mensajeError;
};

// Dueño del LLM local dentro del proceso: el modelo descargado, la descarga
// en curso y el servidor local. Vive en el proceso (no en la solicitud)
// porque un trabajo en segundo plano tiene que sobrevivir entre requests:
// el hilo principal nunca bloquea en red.
class RuntimeLlm final {
public:
    RuntimeLlm(std::filesystem::path casa, ConfigLlm config);
    ~RuntimeLlm();

    RuntimeLlm(const RuntimeLlm&) = delete;
    RuntimeLlm& operator=(const RuntimeLlm&) = delete;

    // Configuración vigente del LLM local.
    const ConfigLlm& config() const;

    // Aplica una configuración nueva (por ejemplo al cambiar de modo).
    void aplicarConfig(ConfigLlm config);

    // Estado actual del LLM local.
    EstadoLocal estado() const;

    // Ruta del GGUF dentro de la casa.
    std::filesystem::path rutaModelo() const;

    // Inicia (o reanuda) la descarga del modelo en segundo plano. No hace
    // nada si ya hay una descarga en curso.
    void iniciarDescarga();

    // Cancela la descarga en curso (el parcial se conserva).
    void cancelarDescarga();

    // true si hay una descarga en curso.
    bool descargaEnCurso() const;

    // Levanta el servidor local si hace falta y devuelve su puerto. Lanza
    // ErrorSilvy si el modelo no está descargado o si el servidor no levanta.
    int asegurarServidor();

    // Apaga el servidor local.
    void detenerServidor();

private:
    void ejecutarDescarga();

    std::filesystem::path casa_;
    ConfigLlm config_;
    mutable std::mutex mutex_;
    std::thread hilo_;
    std::atomic<bool> cancelado_{false};
    bool descargando_ = false;
    std::uint64_t bytesDescargados_ = 0;
    std::uint64_t bytesTotales_ = 0;
    std::string codigoError_;
    std::string mensajeError_;
    std::unique_ptr<SupervisorLlama> supervisor_;
};

}  // namespace silvy

#endif  // SILVY_MODELO_RUNTIME_LLM_H
