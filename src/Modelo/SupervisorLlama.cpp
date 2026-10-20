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
#include "Modelo/SupervisorLlama.h"

#include "Errores/Errores.h"

#include <httplib.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <regex>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace silvy {

namespace {

// Intervalo del poll de salud y límites de los probes.
constexpr int kIntervaloPollMs = 300;
constexpr int kTimeoutHealthMs = 800;
constexpr int kTimeoutModelosMs = 1500;

// Tamaño del tail que se adjunta a los errores.
constexpr std::size_t kTailBytes = 4096;
constexpr std::size_t kTailLineas = 12;

// En Windows el ejecutable lleva .exe.
std::string nombreEjecutable(const std::string& nombre) {
#ifdef _WIN32
    if (nombre.size() >= 4 &&
        nombre.compare(nombre.size() - 4, 4, ".exe") == 0) {
        return nombre;
    }
    return nombre + ".exe";
#else
    return nombre;
#endif
}

// Busca un ejecutable en el PATH del sistema.
std::optional<std::filesystem::path> buscarEnPath(const std::string& nombre) {
    const char* ruta = std::getenv("PATH");
    if (ruta == nullptr) {
        return std::nullopt;
    }
#ifdef _WIN32
    const char separador = ';';
#else
    const char separador = ':';
#endif
    std::string resto(ruta);
    std::size_t inicio = 0;
    while (inicio <= resto.size()) {
        const std::size_t fin = resto.find(separador, inicio);
        const std::string carpeta =
            resto.substr(inicio, fin == std::string::npos ? std::string::npos
                                                           : fin - inicio);
        if (!carpeta.empty()) {
            const std::filesystem::path candidato =
                std::filesystem::path(carpeta) / nombre;
            std::error_code error;
            if (std::filesystem::is_regular_file(candidato, error)) {
                return candidato;
            }
        }
        if (fin == std::string::npos) {
            break;
        }
        inicio = fin + 1;
    }
    return std::nullopt;
}

// Abre un socket de loopback para ver si el puerto se puede tomar.
bool puertoSePuedeTomar(int puerto) {
#ifdef _WIN32
    WSADATA datos;
    WSAStartup(MAKEWORD(2, 2), &datos);
    const SOCKET descriptor = socket(AF_INET, SOCK_STREAM, 0);
    bool libre = false;
    if (descriptor != INVALID_SOCKET) {
        sockaddr_in direccion{};
        direccion.sin_family = AF_INET;
        direccion.sin_port = htons(static_cast<unsigned short>(puerto));
        direccion.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        libre = bind(descriptor, reinterpret_cast<sockaddr*>(&direccion),
                     sizeof(direccion)) == 0;
        closesocket(descriptor);
    }
    WSACleanup();
    return libre;
#else
    const int descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (descriptor < 0) {
        return false;
    }
    // En POSIX un socket en TIME_WAIT no bloquea el bind si se pide
    // reutilizar la dirección; en Windows esa opción tiene otras
    // consecuencias, así que no se usa.
    const int reutilizar = 1;
    setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &reutilizar,
               sizeof(reutilizar));
    sockaddr_in direccion{};
    direccion.sin_family = AF_INET;
    direccion.sin_port = htons(static_cast<unsigned short>(puerto));
    direccion.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    const bool libre =
        bind(descriptor, reinterpret_cast<sockaddr*>(&direccion),
             sizeof(direccion)) == 0;
    close(descriptor);
    return libre;
#endif
}

}  // namespace

std::vector<std::string> argumentosLlamaServer(
    const OpcionesServidorLocal& opciones) {
    return {
        opciones.rutaEjecutable,
        "-m",
        opciones.modelo.string(),
        "--host",
        opciones.host,
        "--port",
        std::to_string(opciones.puerto),
        "-c",
        std::to_string(opciones.contextSize),
        "-ngl",
        std::to_string(opciones.gpuLayers),
        "--jinja",
    };
}

std::optional<int> buscarPuertoLibre(int inicio, int cantidad) {
    for (int candidato = inicio; candidato < inicio + cantidad; ++candidato) {
        if (candidato < 1 || candidato > 65535) {
            continue;
        }
        if (puertoSePuedeTomar(candidato)) {
            return candidato;
        }
    }
    return std::nullopt;
}

std::optional<int> porcentajeDeCarga(const std::string& log) {
    const std::regex patron(R"((\d{1,3})(?:\.\d+)?%)");
    std::optional<int> mayor;
    for (std::sregex_iterator iter(log.begin(), log.end(), patron), fin;
         iter != fin; ++iter) {
        const int valor = std::atoi((*iter)[1].str().c_str());
        if (valor >= 0 && valor <= 100) {
            if (!mayor || valor > *mayor) {
                mayor = valor;
            }
        }
    }
    return mayor;
}

std::optional<std::filesystem::path> resolverEjecutableLlama(
    const std::string& rutaConfigurada) {
    if (rutaConfigurada.empty()) {
        return buscarEnPath(nombreEjecutable("llama-server"));
    }

#ifdef _WIN32
    const bool yaTieneExe = rutaConfigurada.size() >= 4 &&
                            rutaConfigurada.compare(rutaConfigurada.size() - 4,
                                                    4, ".exe") == 0;
    const std::filesystem::path comoEs(yaTieneExe ? rutaConfigurada
                                                   : rutaConfigurada + ".exe");
#else
    const std::filesystem::path comoEs(rutaConfigurada);
#endif
    std::error_code error;
    if (std::filesystem::is_regular_file(comoEs, error)) {
        return comoEs;
    }

    // Un nombre suelto (sin ruta) también se busca en el PATH.
    const bool esSoloNombre =
        rutaConfigurada.find('/') == std::string::npos &&
        rutaConfigurada.find('\\') == std::string::npos;
    if (esSoloNombre) {
        const std::optional<std::filesystem::path> enPath =
            buscarEnPath(nombreEjecutable(rutaConfigurada));
        if (enPath) {
            return enPath;
        }
        // Un nombre suelto que no está en el PATH no se puede ejecutar.
        return std::nullopt;
    }

    // Ruta explícita que no existe: se devuelve igual para que el error
    // de arranque la mencione.
    return comoEs;
}

SupervisorLlama::SupervisorLlama(OpcionesServidorLocal opciones)
    : opciones_(std::move(opciones)) {}

SupervisorLlama::~SupervisorLlama() {
    if (proceso_ && proceso_->vivo()) {
        detener();
    }
}

bool SupervisorLlama::vivo() const {
    return proceso_ && proceso_->vivo();
}

int SupervisorLlama::puerto() const {
    return puerto_;
}

std::string SupervisorLlama::tailLog() const {
    if (!proceso_) {
        return "(log vacío)";
    }
    return proceso_->tail(kTailBytes, kTailLineas);
}

std::optional<int> SupervisorLlama::porcentajeCarga() const {
    if (!proceso_) {
        return std::nullopt;
    }
    return porcentajeDeCarga(proceso_->tail(2048, 40));
}

bool SupervisorLlama::sano() const {
    if (puerto_ <= 0) {
        return false;
    }
    const std::string base = "http://" + opciones_.host + ":" +
                             std::to_string(puerto_);

    httplib::Client salud(base);
    salud.set_connection_timeout(std::chrono::milliseconds(kTimeoutHealthMs));
    salud.set_read_timeout(std::chrono::milliseconds(kTimeoutHealthMs));
    const auto respuestaSalud = salud.Get("/health");
    // Mientras carga el modelo llama-server responde 503: todavía no está.
    if (!respuestaSalud || respuestaSalud->status != 200) {
        return false;
    }

    httplib::Client modelos(base);
    modelos.set_connection_timeout(std::chrono::milliseconds(kTimeoutModelosMs));
    modelos.set_read_timeout(std::chrono::milliseconds(kTimeoutModelosMs));
    const auto respuestaModelos = modelos.Get("/v1/models");
    if (!respuestaModelos || respuestaModelos->status != 200) {
        return false;
    }
    // El servidor tiene que servir ESTE modelo: si quedó uno de otra sesión,
    // hay que levantarlo de nuevo.
    const std::string nombre = opciones_.modelo.filename().string();
    if (respuestaModelos->body.find(nombre) == std::string::npos) {
        return false;
    }
    return true;
}

void SupervisorLlama::arrancarProceso() {
    const std::optional<std::filesystem::path> ejecutable =
        resolverEjecutableLlama(opciones_.rutaEjecutable);
    if (!ejecutable) {
        throw ErrorSilvy(
            CodigoError::NO_CONFIGURADO, false,
            "No se encontró llama-server. Poné el ejecutable en el PATH o "
            "indicá su ruta en la configuración (llamaServerPath).");
    }

    ProcesoHijo::Opciones proceso;
    opciones_.rutaEjecutable = ejecutable->string();
    proceso.argumentos = argumentosLlamaServer(opciones_);
    proceso.log = opciones_.log;
    proceso_ = std::make_unique<ProcesoHijo>(std::move(proceso));

    try {
        proceso_->arrancar();
    } catch (const ErrorSilvy& error) {
        throw ErrorSilvy(
            CodigoError::NO_CONFIGURADO, false,
            std::string("No se pudo ejecutar '") + ejecutable->string() +
                "'. Verificá que el ejecutable exista y que tenga permisos "
                "de ejecución, o indicá otra ruta en la configuración "
                "(llamaServerPath). Detalle: " + error.what());
    }
}

int SupervisorLlama::asegurarListo() {
    if (vivo() && sano()) {
        return puerto_;
    }

    if (proceso_ && proceso_->vivo()) {
        // Está vivo pero no sano: se reinicia para tomar la configuración
        // vigente.
        detener();
    }
    proceso_.reset();

    const std::optional<int> puerto = buscarPuertoLibre(opciones_.puerto,
                                                          opciones_.puertoScan);
    if (!puerto) {
        throw ErrorSilvy(
            CodigoError::SERVIDOR_CAIDO, false,
            "No hay puertos libres entre " + std::to_string(opciones_.puerto) +
                " y " + std::to_string(opciones_.puerto + opciones_.puertoScan -
                                       1) +
                ": no se puede levantar el servidor local.");
    }
    opciones_.puerto = *puerto;
    puerto_ = *puerto;

    arrancarProceso();

    const auto inicio = std::chrono::steady_clock::now();
    const auto limite = inicio + std::chrono::milliseconds(opciones_.timeoutCargaMs);
    while (std::chrono::steady_clock::now() < limite) {
        if (!proceso_->vivo()) {
            const int codigo = proceso_->codigoSalida();
            throw ErrorSilvy(
                CodigoError::SERVIDOR_CAIDO, false,
                "llama-server terminó (código " + std::to_string(codigo) +
                    "). Log (" + opciones_.log.filename().string() +
                    "):\n" + tailLog());
        }
        if (sano()) {
            return puerto_;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kIntervaloPollMs));
    }

    throw ErrorSilvy(
        CodigoError::MODELO_NO_DISPONIBLE, true,
        "El modelo local no terminó de cargar en " +
            std::to_string(opciones_.timeoutCargaMs) + " ms. Log (" +
            opciones_.log.filename().string() + "):\n" + tailLog());
}

void SupervisorLlama::detener() {
    if (proceso_) {
        proceso_->detener();
    }
    puerto_ = 0;
}

}  // namespace silvy
