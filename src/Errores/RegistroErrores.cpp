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
#include "Errores/RegistroErrores.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>

namespace silvy {

namespace {

using json = nlohmann::json;

// Marca de tiempo UTC ISO-8601 con milisegundos:
// "2026-10-18T12:34:56.789Z".
std::string marcaDeTiempoUtc() {
    const auto ahora = std::chrono::system_clock::now();
    const auto milisegundos =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            ahora.time_since_epoch()) %
        1000;
    const std::time_t segundos = std::chrono::system_clock::to_time_t(ahora);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &segundos);
#else
    gmtime_r(&segundos, &tm);
#endif
    char fecha[32];
    std::strftime(fecha, sizeof(fecha), "%Y-%m-%dT%H:%M:%S", &tm);
    std::ostringstream salida;
    salida << fecha << '.' << std::setw(3) << std::setfill('0')
           << milisegundos.count() << 'Z';
    return salida.str();
}

// Sufijo de fecha para el nombre de los archivos rotados: "20261018-123456".
std::string sufijoArchivoUtc() {
    const std::time_t segundos =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &segundos);
#else
    gmtime_r(&segundos, &tm);
#endif
    char fecha[32];
    std::strftime(fecha, sizeof(fecha), "%Y%m%d-%H%M%S", &tm);
    return fecha;
}

json causaAJson(const CausaError& causa) {
    json objeto;
    objeto["codigo"] = textoDeCodigo(causa.codigo);
    objeto["mensaje"] = causa.mensaje;
    if (causa.causa) {
        objeto["causa"] = causaAJson(*causa.causa);
    }
    return objeto;
}

bool esArchivoRotado(const std::filesystem::path& ruta) {
    const std::string nombre = ruta.filename().string();
    return nombre.rfind("errores-", 0) == 0 && nombre.size() > 8 &&
           nombre.compare(nombre.size() - 6, 6, ".jsonl") == 0;
}

}  // namespace

struct RegistroErrores::Impl {
    explicit Impl(ConfigRegistroErrores configuracion)
        : config(std::move(configuracion)),
          archivoActual(config.directorio / "errores.jsonl") {
        try {
            std::filesystem::create_directories(config.directorio);
            if (std::filesystem::exists(archivoActual)) {
                tamActualBytes = std::filesystem::file_size(archivoActual);
                if (tamActualBytes >= config.tamMaximoKb * 1024) {
                    rotar();
                }
            }
            limpiarRotados();
        } catch (const std::exception& error) {
            // La casa puede no ser escribible: se degrada a consola y el
            // registro sigue aceptando entradas (cada escritura fallará).
            std::cerr << "[SILVY][ERRORES] No se pudo preparar "
                      << config.directorio.string() << ": " << error.what()
                      << '\n';
        }
        hiloEscritor = std::thread(&Impl::bucleEscritor, this);
    }

    ~Impl() {
        {
            std::lock_guard<std::mutex> cerrojo(mutexCola);
            detenido = true;
        }
        cvCola.notify_all();
        if (hiloEscritor.joinable()) {
            hiloEscritor.join();
        }
    }

    json registrar(const ErrorSilvy& error, const ContextoError& contexto) {
        json registro;
        registro["id"] = generarId();
        registro["ts"] = marcaDeTiempoUtc();
        registro["codigo"] = textoDeCodigo(error.codigo());
        registro["mensaje"] = error.what();
        registro["reintentable"] = error.reintentable();
        registro["comando"] = contexto.comando;
        registro["modo"] = contexto.modo;
        registro["modelo"] = contexto.modelo;
        registro["detalle"] = contexto.detalle;
        if (error.causa()) {
            registro["causa"] = causaAJson(*error.causa());
        }
        {
            std::lock_guard<std::mutex> cerrojo(mutexCola);
            if (!detenido) {
                cola.push(registro.dump());
            }
        }
        cvCola.notify_one();
        return registro;
    }

    void vaciar() {
        std::unique_lock<std::mutex> cerrojo(mutexCola);
        cvCola.wait(cerrojo, [this] { return cola.empty() && !enProceso; });
    }

    std::vector<json> listar(std::size_t limite,
                             std::optional<CodigoError> codigo) {
        vaciar();
        std::vector<json> resultado;
        if (limite == 0) {
            return resultado;
        }
        std::lock_guard<std::mutex> cerrojo(mutexArchivo);
        agregarDeArchivo(archivoActual, limite, codigo, resultado);
        for (const auto& archivo : rotados()) {
            if (resultado.size() >= limite) {
                break;
            }
            agregarDeArchivo(archivo, limite, codigo, resultado);
        }
        return resultado;
    }

    std::optional<json> obtener(const std::string& id) {
        vaciar();
        std::lock_guard<std::mutex> cerrojo(mutexArchivo);
        if (auto encontrado = buscarEnArchivo(archivoActual, id)) {
            return encontrado;
        }
        for (const auto& archivo : rotados()) {
            if (auto encontrado = buscarEnArchivo(archivo, id)) {
                return encontrado;
            }
        }
        return std::nullopt;
    }

private:
    ConfigRegistroErrores config;
    std::filesystem::path archivoActual;
    std::size_t tamActualBytes = 0;
    std::mutex mutexArchivo;

    std::mutex mutexCola;
    std::condition_variable cvCola;
    std::queue<std::string> cola;
    bool detenido = false;
    bool enProceso = false;
    std::thread hiloEscritor;

    std::atomic<std::uint64_t> secuencia{0};

    std::string generarId() {
        const auto milisegundos = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count();
        std::ostringstream id;
        id << "err-" << milisegundos << '-'
           << std::setw(4) << std::setfill('0') << secuencia.fetch_add(1);
        return id.str();
    }

    void bucleEscritor() {
        while (true) {
            std::string linea;
            {
                std::unique_lock<std::mutex> cerrojo(mutexCola);
                cvCola.wait(cerrojo,
                            [this] { return detenido || !cola.empty(); });
                if (cola.empty()) {
                    if (detenido) {
                        break;
                    }
                    continue;
                }
                linea = std::move(cola.front());
                cola.pop();
                enProceso = true;
            }
            escribirLinea(linea);
            {
                std::lock_guard<std::mutex> cerrojo(mutexCola);
                enProceso = false;
            }
            cvCola.notify_all();
        }
    }

    // Debe llamarse con mutexArchivo tomado.
    void escribirLinea(const std::string& linea) {
        std::lock_guard<std::mutex> cerrojo(mutexArchivo);
        try {
            if (tamActualBytes + linea.size() >= config.tamMaximoKb * 1024) {
                rotar();
            }
            std::ofstream salida(archivoActual, std::ios::app | std::ios::binary);
            if (!salida) {
                std::cerr << "[SILVY][ERRORES] No se pudo abrir "
                          << archivoActual.string() << '\n';
                return;
            }
            salida << linea << '\n';
            salida.flush();
            if (!salida) {
                std::cerr << "[SILVY][ERRORES] Falló la escritura en "
                          << archivoActual.string() << '\n';
                return;
            }
            tamActualBytes += linea.size() + 1;
        } catch (const std::exception& error) {
            // Un fallo al guardar un error nunca rompe la respuesta principal.
            std::cerr << "[SILVY][ERRORES] No se pudo persistir: "
                      << error.what() << '\n';
        }
    }

    // Debe llamarse con mutexArchivo tomado.
    void rotar() {
        if (tamActualBytes == 0 && !std::filesystem::exists(archivoActual)) {
            return;
        }
        const std::string base = sufijoArchivoUtc();
        for (int intento = 0; intento < 100; ++intento) {
            std::filesystem::path destino = config.directorio / (
                "errores-" + base + (intento == 0 ? "" : "-" + std::to_string(intento)) +
                ".jsonl");
            std::error_code error;
            if (std::filesystem::exists(destino, error)) {
                continue;
            }
            std::filesystem::rename(archivoActual, destino, error);
            if (!error) {
                tamActualBytes = 0;
                limpiarRotados();
                return;
            }
            break;
        }
        // Si no se pudo rotar, se sigue escribiendo en el archivo actual.
        std::cerr << "[SILVY][ERRORES] No se pudo rotar " << archivoActual.string()
                  << '\n';
    }

    // Debe llamarse con mutexArchivo tomado (o antes de arrancar el hilo).
    void limpiarRotados() {
        try {
            std::vector<std::filesystem::path> archivos = rotados();
            // Más nuevos primero: el nombre ordena cronológicamente.
            std::sort(archivos.begin(), archivos.end(),
                      [](const auto& a, const auto& b) {
                          return a.filename().string() > b.filename().string();
                      });
            for (std::size_t i = config.maximoArchivos; i < archivos.size(); ++i) {
                std::error_code error;
                std::filesystem::remove(archivos[i], error);
            }
            if (config.retencionDias > 0) {
                const auto limite =
                    std::filesystem::file_time_type::clock::now() -
                    std::chrono::hours(24 * static_cast<long long>(config.retencionDias));
                for (const auto& archivo : archivos) {
                    std::error_code error;
                    const auto escritura = std::filesystem::last_write_time(archivo, error);
                    if (!error && escritura < limite) {
                        std::filesystem::remove(archivo, error);
                    }
                }
            }
        } catch (const std::exception& error) {
            std::cerr << "[SILVY][ERRORES] No se pudo limpiar rotados: "
                      << error.what() << '\n';
        }
    }

    // Debe llamarse con mutexArchivo tomado (o antes de arrancar el hilo).
    std::vector<std::filesystem::path> rotados() const {
        std::vector<std::filesystem::path> archivos;
        std::error_code error;
        for (const auto& entrada :
             std::filesystem::directory_iterator(config.directorio, error)) {
            if (esArchivoRotado(entrada.path())) {
                archivos.push_back(entrada.path());
            }
        }
        return archivos;
    }

    void agregarDeArchivo(const std::filesystem::path& archivo, std::size_t limite,
                          const std::optional<CodigoError>& codigo,
                          std::vector<json>& resultado) const {
        std::ifstream entrada(archivo);
        if (!entrada) {
            return;
        }
        std::vector<json> delArchivo;
        std::string linea;
        while (std::getline(entrada, linea)) {
            if (linea.empty()) {
                continue;
            }
            json registro = json::parse(linea, nullptr, false);
            if (registro.is_discarded()) {
                continue;
            }
            if (codigo && registro.value("codigo", std::string()) !=
                              textoDeCodigo(*codigo)) {
                continue;
            }
            delArchivo.push_back(std::move(registro));
        }
        // Del final al principio: más reciente primero.
        for (auto iterador = delArchivo.rbegin();
             iterador != delArchivo.rend() && resultado.size() < limite;
             ++iterador) {
            resultado.push_back(std::move(*iterador));
        }
    }

    std::optional<json> buscarEnArchivo(const std::filesystem::path& archivo,
                                        const std::string& id) const {
        std::ifstream entrada(archivo);
        if (!entrada) {
            return std::nullopt;
        }
        std::string linea;
        while (std::getline(entrada, linea)) {
            if (linea.empty()) {
                continue;
            }
            json registro = json::parse(linea, nullptr, false);
            if (registro.is_discarded()) {
                continue;
            }
            if (registro.value("id", std::string()) == id) {
                return registro;
            }
        }
        return std::nullopt;
    }
};

std::vector<std::string> ConfigRegistroErrores::validar() const {
    std::vector<std::string> problemas;
    if (directorio.empty()) {
        problemas.push_back("directorio del registro vacío");
    }
    if (tamMaximoKb == 0) {
        problemas.push_back("tamMaximoKb debe ser mayor que 0");
    }
    if (maximoArchivos == 0) {
        problemas.push_back("maximoArchivos debe ser mayor que 0");
    }
    return problemas;
}

RegistroErrores::RegistroErrores(ConfigRegistroErrores config)
    : pimpl_(std::make_unique<Impl>(std::move(config))) {}

RegistroErrores::~RegistroErrores() = default;

nlohmann::json RegistroErrores::registrar(const ErrorSilvy& error,
                                          const ContextoError& contexto) {
    return pimpl_->registrar(error, contexto);
}

void RegistroErrores::vaciar() { pimpl_->vaciar(); }

std::vector<nlohmann::json> RegistroErrores::listar(
    std::size_t limite, std::optional<CodigoError> codigo) {
    return pimpl_->listar(limite, codigo);
}

std::optional<nlohmann::json> RegistroErrores::obtener(const std::string& id) {
    return pimpl_->obtener(id);
}

}  // namespace silvy
