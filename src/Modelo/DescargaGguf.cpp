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
#include "Modelo/DescargaGguf.h"

#include "Errores/Errores.h"
#include "Llm/ProtocoloOpenAi.h"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace silvy {

namespace {

// Un lock más viejo que esto se considera abandonedado: el proceso que lo
// dejó murió sin poder borrarlo.
constexpr int kLockAbandonadoMinutos = 30;

// Intervalo de espera al lock que tiene otro proceso.
constexpr int kEsperaLockMs = 200;

// Tiempo máximo sin recibir datos antes de dar la descarga por caída.
constexpr int kTimeoutInactividadMs = 60000;

std::uint64_t tamañoDe(const std::filesystem::path& ruta) {
    std::error_code error;
    const auto tamaño = std::filesystem::file_size(ruta, error);
    if (error) {
        return 0;
    }
    return static_cast<std::uint64_t>(tamaño);
}

bool existe(const std::filesystem::path& ruta) {
    std::error_code error;
    return std::filesystem::exists(ruta, error) && !error;
}

// Crea el lock en exclusiva: si otro proceso lo tiene, devuelve false.
// No hay una función estándar para crear un archivo en exclusiva, así que
// se usa la API de cada plataforma.
bool intentarCrearLock(const std::filesystem::path& lock) {
    std::error_code error;
    std::filesystem::create_directories(lock.parent_path(), error);
#ifdef _WIN32
    const HANDLE archivo = CreateFileA(
        lock.string().c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (archivo == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(archivo);
    return true;
#else
    const int descriptor = open(lock.string().c_str(),
                                O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (descriptor < 0) {
        return false;
    }
    close(descriptor);
    return true;
#endif
}

// Un lock que lleva mucho tiempo sin tocarse es de un proceso muerto: se
// puede quitar sin riesgo.
bool lockAbandonado(const std::filesystem::path& lock) {
    std::error_code error;
    const auto escritura = std::filesystem::last_write_time(lock, error);
    if (error) {
        return false;
    }
    const auto ahora = std::filesystem::file_time_type::clock::now();
    const auto antiguedad =
        std::chrono::duration_cast<std::chrono::minutes>(ahora - escritura)
            .count();
    return antiguedad >= kLockAbandonadoMinutos;
}

// Espera a que otro proceso libere el lock. Si el archivo final aparece
// mientras tanto, la descarga la hizo el otro proceso y no hay que repetirla.
bool esperarLock(const std::filesystem::path& lock,
                 const std::filesystem::path& destino, int esperaMs,
                 const std::atomic<bool>& cancelado) {
    const auto fin = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(esperaMs);
    while (std::chrono::steady_clock::now() < fin) {
        if (cancelado.load()) {
            return false;
        }
        if (existe(destino)) {
            return true;
        }
        if (!existe(lock)) {
            return true;
        }
        if (lockAbandonado(lock)) {
            std::error_code error;
            std::filesystem::remove(lock, error);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kEsperaLockMs));
    }
    return false;
}

std::string numero(std::uint64_t valor) {
    return std::to_string(valor);
}

}  // namespace

std::string urlDeHuggingFace(const std::string& repo,
                              const std::string& archivo,
                              const std::string& base) {
    std::string raiz = base;
    while (!raiz.empty() && raiz.back() == '/') {
        raiz.pop_back();
    }
    return raiz + "/" + repo + "/resolve/main/" + archivo;
}

std::string cabeceraRange(std::uint64_t desde) {
    return "bytes=" + std::to_string(desde) + "-";
}

std::uint64_t totalDesdeContentRange(const std::string& valor) {
    const std::size_t barra = valor.find('/');
    if (barra == std::string::npos) {
        return 0;
    }
    const std::string total = valor.substr(barra + 1);
    if (total.empty() || total == "*") {
        return 0;
    }
    return std::strtoull(total.c_str(), nullptr, 10);
}

std::uint64_t totalDesdeContentLength(const std::string& valor) {
    if (valor.empty()) {
        return 0;
    }
    return std::strtoull(valor.c_str(), nullptr, 10);
}

DescargadorGguf::DescargadorGguf(OpcionesDescarga opciones)
    : opciones_(std::move(opciones)) {}

ResultadoDescarga DescargadorGguf::descargar(
    const std::atomic<bool>& cancelado) {
    // Idempotencia: si el modelo ya está en disco, no se descarga otra vez.
    if (existe(opciones_.destino) && tamañoDe(opciones_.destino) > 0) {
        return ResultadoDescarga{opciones_.destino, tamañoDe(opciones_.destino),
                                  true, false};
    }

    // Lock por ruta: un solo proceso descarga un archivo a la vez.
    bool conLock = intentarCrearLock(opciones_.lock);
    if (!conLock) {
        if (!esperarLock(opciones_.lock, opciones_.destino,
                         opciones_.esperaLockMs, cancelado)) {
            if (cancelado.load()) {
                throw ErrorCancelado(
                    "La descarga del modelo fue cancelada mientras se esperaba "
                    "el lock de otra descarga");
            }
            throw ErrorSilvy(
                CodigoError::DESCARGA_FALLIDA, true,
                "Otro proceso está descargando el modelo y no se liberó el "
                "lock a tiempo: " + opciones_.lock.string());
        }
        if (existe(opciones_.destino) && tamañoDe(opciones_.destino) > 0) {
            return ResultadoDescarga{opciones_.destino,
                                      tamañoDe(opciones_.destino), true, false};
        }
        conLock = intentarCrearLock(opciones_.lock);
        if (!conLock) {
            throw ErrorSilvy(
                CodigoError::DESCARGA_FALLIDA, true,
                "No se pudo tomar el lock de descarga: " +
                    opciones_.lock.string());
        }
    }

    struct Liberador final {
        const std::filesystem::path& lock;
        ~Liberador() {
            std::error_code error;
            std::filesystem::remove(lock, error);
        }
    } liberador{opciones_.lock};

    // Reanudación: lo que quedó en el parcial es lo que se pide por Range.
    const std::uint64_t yaDescargado = tamañoDe(opciones_.parcial);
    const bool reanudada = yaDescargado > 0;

    std::error_code errorDirectorios;
    std::filesystem::create_directories(opciones_.destino.parent_path(),
                                        errorDirectorios);

    const DestinoApi destino = interpretarBaseUrl(opciones_.url);

    httplib::Client cliente(destino.esquema + "://" + destino.host + ":" +
                             std::to_string(destino.puerto));
    cliente.set_connection_timeout(
        std::chrono::milliseconds(opciones_.timeoutConexionMs));
    // Timeout de inactividad: si el servidor deja de mandar datos durante un
    // minuto, la descarga se da por caída (y el parcial queda para reanudar).
    cliente.set_read_timeout(std::chrono::milliseconds(kTimeoutInactividadMs));
    cliente.set_write_timeout(
        std::chrono::milliseconds(opciones_.timeoutConexionMs));
    cliente.set_follow_location(true);

    httplib::Headers cabeceras;
    if (reanudada) {
        cabeceras.emplace("Range", cabeceraRange(yaDescargado));
    }

    const auto inicio = std::chrono::steady_clock::now();
    std::uint64_t recibidos = yaDescargado;
    std::uint64_t total = 0;
    bool canceladoEnMano = false;
    bool expirado = false;
    bool falloDeEscritura = false;
    int estadoHttp = 0;
    std::string cuerpoError;
    std::ofstream salida;
    auto ultimoProgreso = std::chrono::steady_clock::now();
    // true si la descarga realmente continúa desde el parcial. El servidor
    // puede ignorar el Range y mandar el archivo entero: en ese caso hay que
    // rehacerlo desde cero.
    bool continuo = reanudada;

    // El manejador de respuesta ve el estado y las cabeceras antes del
    // primer bloque: ahí se decide si se reanuda (206) o si hay que rehacer
    // el archivo porque el servidor ignoró el Range (200).
    const auto manejadorRespuesta = [&](const httplib::Response& respuesta) {
        estadoHttp = respuesta.status;
        if (estadoHttp == 206) {
            continuo = reanudada;
            total = totalDesdeContentRange(
                respuesta.get_header_value("Content-Range"));
            if (total == 0) {
                total = yaDescargado +
                        totalDesdeContentLength(
                            respuesta.get_header_value("Content-Length"));
            }
        } else if (estadoHttp == 200) {
            // El servidor ignoró el Range: el archivo se rehace desde cero.
            continuo = false;
            total = totalDesdeContentLength(
                respuesta.get_header_value("Content-Length"));
        } else if (estadoHttp == 416 && yaDescargado > 0) {
            // El servidor dice que el rango pedido no existe: el parcial ya
            // tenía todo lo que había. Se da por completo tal cual.
            total = yaDescargado;
            return true;
        } else {
            // Estado de error: el cuerpo se acumula como texto, no se
            // escribe en el archivo.
            return true;
        }

        std::ofstream archivo(opciones_.parcial,
                              std::ios::binary |
                                  (continuo ? std::ios::app : std::ios::trunc));
        if (!archivo) {
            falloDeEscritura = true;
            return false;
        }
        salida.swap(archivo);
        if (!continuo) {
            recibidos = 0;
        }
        return true;
    };

    const auto receptor = [&](const char* datos, std::size_t longitud) -> bool {
        if (cancelado.load()) {
            canceladoEnMano = true;
            return false;
        }
        if (std::chrono::steady_clock::now() - inicio >
            std::chrono::milliseconds(opciones_.timeoutTotalMs)) {
            expirado = true;
            return false;
        }
        if (estadoHttp != 200 && estadoHttp != 206) {
            if (cuerpoError.size() < 300) {
                cuerpoError.append(datos, longitud);
            }
            return true;
        }
        salida.write(datos, static_cast<std::streamsize>(longitud));
        if (!salida) {
            falloDeEscritura = true;
            return false;
        }
        recibidos += longitud;

        // Progreso con throttle: no se reporta en cada bloque de 64 KB.
        const auto ahora = std::chrono::steady_clock::now();
        if (opciones_.alProgresar &&
            ahora - ultimoProgreso >=
                std::chrono::milliseconds(opciones_.intervaloProgresoMs)) {
            ultimoProgreso = ahora;
            opciones_.alProgresar(ProgresoDescarga{recibidos, total, reanudada});
        }
        return true;
    };

    const auto resultado =
        cliente.Get(destino.prefijo, cabeceras, manejadorRespuesta, receptor);

    if (salida.is_open()) {
        salida.flush();
        salida.close();
    }

    if (canceladoEnMano || cancelado.load()) {
        throw ErrorCancelado(
            "La descarga del modelo fue cancelada; se conserva el archivo "
            "parcial para reanudarla");
    }
    if (expirado) {
        throw ErrorTimeout("Se agotó el tiempo de la descarga del modelo (" +
                           std::to_string(opciones_.timeoutTotalMs) +
                           " ms); se conserva el archivo parcial para "
                           "reanudarla");
    }
    if (falloDeEscritura) {
        throw ErrorSilvy(CodigoError::DESCARGA_FALLIDA, true,
                         "No se pudo escribir el archivo parcial: " +
                             opciones_.parcial.string());
    }
    if (!resultado) {
        throw ErrorSilvy(
            CodigoError::DESCARGA_FALLIDA, true,
            "Falló la descarga del modelo (" +
                httplib::to_string(resultado.error()) + "); se conservan " +
                numero(recibidos) + " bytes en " +
                opciones_.parcial.string() + " para reanudarla");
    }
    const bool rangoSatisfecho = estadoHttp == 200 || estadoHttp == 206;
    const bool yaEstabaCompleto = estadoHttp == 416 && yaDescargado > 0;
    if (!rangoSatisfecho && !yaEstabaCompleto) {
        throw ErrorSilvy(
            CodigoError::DESCARGA_FALLIDA, true,
            "La descarga del modelo falló con estado HTTP " +
                std::to_string(estadoHttp) +
                (cuerpoError.empty() ? "" : ": " + cuerpoError));
    }

    if (total != 0 && recibidos < total) {
        throw ErrorSilvy(
            CodigoError::DESCARGA_FALLIDA, true,
            "La descarga del modelo quedó incompleta: " + numero(recibidos) +
                " de " + numero(total) + " bytes; se conserva el parcial para "
                "reanudarla");
    }

    // Progreso final: el consumidor sabe que terminó y cuántos bytes tiene.
    if (opciones_.alProgresar) {
        opciones_.alProgresar(ProgresoDescarga{recibidos, total, continuo});
    }

    // Rename atómico: el parcial pasa a ser el modelo definitivo.
    std::error_code error;
    std::filesystem::rename(opciones_.parcial, opciones_.destino, error);
    if (error) {
        throw ErrorSilvy(CodigoError::DESCARGA_FALLIDA, true,
                         "No se pudo renombrar el modelo descargado a " +
                             opciones_.destino.string() + ": " +
                             error.message());
    }

    return ResultadoDescarga{opciones_.destino, recibidos, false, continuo};
}

}  // namespace silvy
