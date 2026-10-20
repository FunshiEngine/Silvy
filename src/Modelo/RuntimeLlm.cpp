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
#include "Modelo/RuntimeLlm.h"

#include "Errores/Errores.h"
#include "Modelo/DescargaGguf.h"
#include "Modelo/RutasLocales.h"

#include <chrono>
#include <utility>

namespace silvy {

namespace {

// Tiempo máximo de la descarga del modelo (presupuesto total). ElGGUF son
// unos 2,4 GB: con una conexión decente entra de sobra.
constexpr int kTimeoutDescargaMs = 7200000;

// Espera máxima por el lock de descarga de otro proceso.
constexpr int kEsperaLockMs = 30000;

// Frecuencia de los reportes de progreso.
constexpr int kIntervaloProgresoMs = 300;

std::uint64_t tamañoDe(const std::filesystem::path& ruta) {
    std::error_code error;
    const auto tamaño = std::filesystem::file_size(ruta, error);
    if (error) {
        return 0;
    }
    return static_cast<std::uint64_t>(tamaño);
}

}  // namespace

RuntimeLlm::RuntimeLlm(std::filesystem::path casa, ConfigLlm config)
    : casa_(std::move(casa)), config_(std::move(config)) {}

RuntimeLlm::~RuntimeLlm() {
    cancelado_.store(true);
    if (hilo_.joinable()) {
        hilo_.join();
    }
    if (supervisor_) {
        supervisor_->detener();
    }
}

const ConfigLlm& RuntimeLlm::config() const {
    return config_;
}

void RuntimeLlm::aplicarConfig(ConfigLlm config) {
    std::lock_guard<std::mutex> guardia(mutex_);
    config_ = std::move(config);
}

std::filesystem::path RuntimeLlm::rutaModelo() const {
    return rutas::modelo(casa_, config_.hfFile);
}

EstadoLocal RuntimeLlm::estado() const {
    EstadoLocal estado;

    {
        std::lock_guard<std::mutex> guardia(mutex_);
        estado.descargando = descargando_;
        estado.bytesDescargados = bytesDescargados_;
        estado.bytesTotales = bytesTotales_;
        estado.codigoError = codigoError_;
        estado.mensajeError = mensajeError_;
        estado.modelo = config_.hfFile;
    }

    const std::filesystem::path modelo = rutaModelo();
    const bool hayModelo = tamañoDe(modelo) > 0;
    const bool hayParcial = tamañoDe(rutas::modeloParcial(casa_, config_.hfFile)) > 0;

    const bool vivo = supervisor_ && supervisor_->vivo();
    estado.servidorVivo = vivo;
    estado.puerto = supervisor_ ? supervisor_->puerto() : 0;
    if (vivo) {
        estado.porcentajeCarga = supervisor_->porcentajeCarga();
    }

    if (estado.descargando) {
        estado.estado = EstadoLlm::DESCARGANDO;
    } else if (hayModelo && vivo) {
        estado.estado = EstadoLlm::SERVIRVIENDO;
    } else if (hayModelo) {
        estado.estado = EstadoLlm::LISTO;
    } else if (!estado.codigoError.empty()) {
        estado.estado = EstadoLlm::CAIDO;
    } else if (hayParcial) {
        // Hay una descarga parcial: el modelo está a medio bajar.
        estado.estado = EstadoLlm::DESCARGANDO;
        estado.bytesDescargados = tamañoDe(rutas::modeloParcial(casa_, config_.hfFile));
    } else {
        estado.estado = EstadoLlm::SIN_MODELO;
    }

    return estado;
}

bool RuntimeLlm::descargaEnCurso() const {
    std::lock_guard<std::mutex> guardia(mutex_);
    return descargando_;
}

void RuntimeLlm::iniciarDescarga() {
    {
        std::lock_guard<std::mutex> guardia(mutex_);
        if (descargando_) {
            return;
        }
        descargando_ = true;
        bytesDescargados_ = 0;
        bytesTotales_ = 0;
        codigoError_.clear();
        mensajeError_.clear();
    }

    if (hilo_.joinable()) {
        hilo_.join();
    }
    cancelado_.store(false);
    hilo_ = std::thread([this]() { ejecutarDescarga(); });
}

void RuntimeLlm::cancelarDescarga() {
    cancelado_.store(true);
}

void RuntimeLlm::ejecutarDescarga() {
    const std::filesystem::path modelo = rutaModelo();
    const std::filesystem::path parcial =
        rutas::modeloParcial(casa_, config_.hfFile);
    const std::filesystem::path lock = rutas::lockDescarga(casa_, config_.hfFile);

    OpcionesDescarga opciones;
    opciones.url = urlDeHuggingFace(config_.hfRepo, config_.hfFile,
                                    config_.hfBaseUrl);
    opciones.destino = modelo;
    opciones.parcial = parcial;
    opciones.lock = lock;
    opciones.timeoutConexionMs = config_.timeoutConexionMs;
    opciones.timeoutTotalMs = kTimeoutDescargaMs;
    opciones.intervaloProgresoMs = kIntervaloProgresoMs;
    opciones.esperaLockMs = kEsperaLockMs;
    opciones.alProgresar = [this](const ProgresoDescarga& progreso) {
        std::lock_guard<std::mutex> guardia(mutex_);
        bytesDescargados_ = progreso.bytesRecibidos;
        bytesTotales_ = progreso.bytesTotales;
    };

    std::string codigoError;
    std::string mensajeError;
    try {
        DescargadorGguf descargador(opciones);
        const ResultadoDescarga resultado = descargador.descargar(cancelado_);
        std::lock_guard<std::mutex> guardia(mutex_);
        bytesDescargados_ = resultado.bytes;
        codigoError.clear();
        mensajeError.clear();
    } catch (const ErrorCancelado& error) {
        codigoError = textoDeCodigo(CodigoError::CANCELADO);
        mensajeError = error.what();
    } catch (const ErrorSilvy& error) {
        codigoError = textoDeCodigo(error.codigo());
        mensajeError = error.what();
    } catch (const std::exception& error) {
        codigoError = textoDeCodigo(CodigoError::DESCONOCIDO);
        mensajeError = error.what();
    }

    {
        std::lock_guard<std::mutex> guardia(mutex_);
        descargando_ = false;
        codigoError_ = codigoError;
        mensajeError_ = mensajeError;
        if (codigoError.empty()) {
            bytesDescargados_ = tamañoDe(modelo);
        }
    }
}

int RuntimeLlm::asegurarServidor() {
    const std::filesystem::path modelo = rutaModelo();
    if (tamañoDe(modelo) == 0) {
        throw ErrorSilvy(
            CodigoError::MODELO_NO_DISPONIBLE, true,
            "El modelo local no está descargado (" + config_.hfFile +
                "). Bajalo con llm.download antes de pedir una inferencia.");
    }

    if (supervisor_ && supervisor_->vivo()) {
        const int puerto = supervisor_->puerto();
        if (puerto > 0) {
            return puerto;
        }
    }

    if (!supervisor_) {
        OpcionesServidorLocal opciones;
        opciones.rutaEjecutable = config_.llamaServerPath;
        opciones.modelo = modelo;
        opciones.puerto = config_.serverPort;
        opciones.puertoScan = config_.portScan;
        opciones.contextSize = config_.contextSize;
        opciones.gpuLayers = config_.gpuLayers;
        opciones.log = rutas::logServidor(casa_);
        opciones.timeoutCargaMs = config_.timeoutCargaMs;
        supervisor_ = std::make_unique<SupervisorLlama>(std::move(opciones));
    }

    try {
        return supervisor_->asegurarListo();
    } catch (const ErrorSilvy& error) {
        std::lock_guard<std::mutex> guardia(mutex_);
        codigoError_ = textoDeCodigo(error.codigo());
        mensajeError_ = error.what();
        throw;
    }
}

void RuntimeLlm::detenerServidor() {
    if (supervisor_) {
        supervisor_->detener();
    }
}

}  // namespace silvy
