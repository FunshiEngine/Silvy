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
#include "Modelo/ProcesoHijo.h"

#include "Errores/Errores.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace silvy {

namespace {

// Espera del apagado escalonado (ver DISENO §6): terminar, esperar, matar el
// árbol, esperar y forzar.
constexpr int kEsperaTerminarMs = 3000;
constexpr int kEsperaMatarArbolMs = 2000;
constexpr int kEsperaForzadoMs = 2000;

std::string unirArgumentos(const std::vector<std::string>& argumentos) {
    std::string linea;
    for (const std::string& argumento : argumentos) {
        if (!linea.empty()) {
            linea += ' ';
        }
        linea += '"';
        linea += argumento;
        linea += '"';
    }
    return linea;
}

}  // namespace

ProcesoHijo::ProcesoHijo(Opciones opciones)
    : opciones_(std::move(opciones)) {}

ProcesoHijo::~ProcesoHijo() {
    if (arrancado_ && !terminado_) {
        detener();
    }
}

void ProcesoHijo::arrancar() {
    if (arrancado_) {
        return;
    }
    if (opciones_.argumentos.empty()) {
        throw ErrorSilvy(CodigoError::NO_CONFIGURADO, false,
                         "No se puede arrancar un proceso sin argumentos");
    }

    std::error_code error;
    if (!opciones_.log.empty()) {
        std::filesystem::create_directories(opciones_.log.parent_path(),
                                            error);
    }

#ifdef _WIN32
    SECURITY_ATTRIBUTES atributos;
    atributos.nLength = sizeof(atributos);
    atributos.lpSecurityDescriptor = nullptr;
    atributos.bInheritHandle = TRUE;

    const HANDLE archivoLog = CreateFileA(
        opciones_.log.string().c_str(), GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &atributos, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);

    STARTUPINFOA inicio;
    ZeroMemory(&inicio, sizeof(inicio));
    inicio.cb = sizeof(inicio);
    inicio.dwFlags = STARTF_USESTDHANDLES;
    inicio.hStdOutput = archivoLog;
    inicio.hStdError = archivoLog;
    // El hijo no lee de la entrada de Silvy: en modo local el proceso lee
    // solicitudes de stdin y el servidor no puede quedarse con ellas.
    const HANDLE entradaNula =
        CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    &atributos, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    inicio.hStdInput = entradaNula == INVALID_HANDLE_VALUE
                           ? nullptr
                           : entradaNula;

    PROCESS_INFORMATION informacion;
    ZeroMemory(&informacion, sizeof(informacion));

    std::string linea = unirArgumentos(opciones_.argumentos);
    std::vector<char> mutableLinea(linea.begin(), linea.end());
    mutableLinea.push_back('\0');

    const BOOL creado = CreateProcessA(
        nullptr, mutableLinea.data(), nullptr, nullptr, TRUE, 0, nullptr,
        opciones_.directorioTrabajo.empty()
            ? nullptr
            : opciones_.directorioTrabajo.string().c_str(),
        &inicio, &informacion);

    if (archivoLog != INVALID_HANDLE_VALUE) {
        CloseHandle(archivoLog);
    }
    if (entradaNula != INVALID_HANDLE_VALUE) {
        CloseHandle(entradaNula);
    }
    if (!creado) {
        throw ErrorSilvy(
            CodigoError::NO_CONFIGURADO, false,
            "No se pudo ejecutar '" + opciones_.argumentos.front() +
                "': " + std::to_string(GetLastError()));
    }
    proceso_ = informacion.hProcess;
    procesoHijo_ = informacion.hThread;
    arrancado_ = true;
#else
    const int descriptor =
        open(opciones_.log.string().c_str(),
             O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (descriptor < 0) {
        throw ErrorSilvy(
            CodigoError::NO_CONFIGURADO, false,
            "No se pudo abrir el log del proceso: " + opciones_.log.string());
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(descriptor);
        throw ErrorSilvy(CodigoError::NO_CONFIGURADO, false,
                         "No se pudo crear el proceso hijo");
    }
    if (pid == 0) {
        // Grupo de procesos propio: así el apagado puede matar al árbol.
        setpgid(0, 0);
        // El hijo no lee de la entrada de Silvy: el proceso lee solicitudes
        // de stdin y el servidor no puede quedarse con ellas.
        const int descriptorNulo = open("/dev/null", O_RDONLY);
        if (descriptorNulo >= 0) {
            dup2(descriptorNulo, STDIN_FILENO);
            close(descriptorNulo);
        }
        dup2(descriptor, STDOUT_FILENO);
        dup2(descriptor, STDERR_FILENO);
        close(descriptor);

        std::vector<char*> argumentos;
        argumentos.reserve(opciones_.argumentos.size() + 1);
        for (const std::string& argumento : opciones_.argumentos) {
            argumentos.push_back(const_cast<char*>(argumento.c_str()));
        }
        argumentos.push_back(nullptr);
        execvp(argumentos.front(), argumentos.data());
        _exit(127);
    }
    close(descriptor);
    pid_ = pid;
    arrancado_ = true;
#endif
}

bool ProcesoHijo::vivo() const {
    if (!arrancado_ || terminado_) {
        return false;
    }
#ifdef _WIN32
    if (proceso_ == nullptr) {
        return false;
    }
    if (WaitForSingleObject(static_cast<HANDLE>(proceso_), 0) == WAIT_TIMEOUT) {
        return true;
    }
    terminado_ = true;
    DWORD codigo = 0;
    if (GetExitCodeProcess(static_cast<HANDLE>(proceso_), &codigo)) {
        codigoSalida_ = static_cast<int>(codigo);
    }
    return false;
#else
    int estado = 0;
    const pid_t resultado = waitpid(pid_, &estado, WNOHANG);
    if (resultado == pid_) {
        terminado_ = true;
        codigoSalida_ = WIFEXITED(estado) ? WEXITSTATUS(estado) : -1;
        return false;
    }
    return true;
#endif
}

int ProcesoHijo::codigoSalida() const {
    if (!arrancado_) {
        return -1;
    }
    if (!terminado_) {
        vivo();
    }
    if (!terminado_) {
        return -1;
    }
#ifdef _WIN32
    if (proceso_ != nullptr && codigoSalida_ < 0) {
        DWORD codigo = 0;
        if (GetExitCodeProcess(static_cast<HANDLE>(proceso_), &codigo)) {
            codigoSalida_ = static_cast<int>(codigo);
        }
    }
#endif
    return codigoSalida_;
}

std::string ProcesoHijo::tail(std::size_t maxBytes, std::size_t maxLineas) const {
    std::error_code error;
    if (!std::filesystem::exists(opciones_.log, error)) {
        return "(log vacío)";
    }
    const auto tamaño = std::filesystem::file_size(opciones_.log, error);
    if (error) {
        return "(no se pudo leer el log)";
    }

    std::ifstream entrada(opciones_.log, std::ios::binary);
    if (!entrada) {
        return "(no se pudo leer el log)";
    }

    std::size_t salto = 0;
    if (tamaño > maxBytes) {
        salto = static_cast<std::size_t>(tamaño) - maxBytes;
        entrada.seekg(static_cast<std::streamoff>(salto));
    }
    std::ostringstream buffer;
    buffer << entrada.rdbuf();
    std::string texto = buffer.str();

    // Se dejan las últimas líneas completas.
    std::size_t lineas = 0;
    std::size_t corte = texto.size();
    for (std::size_t i = texto.size(); i > 0; --i) {
        if (texto[i - 1] == '\n') {
            ++lineas;
            if (lineas > maxLineas) {
                corte = i;
                break;
            }
        }
    }
    if (corte < texto.size()) {
        texto = texto.substr(corte);
    }
    if (texto.empty()) {
        return "(log vacío)";
    }
    return texto;
}

bool ProcesoHijo::esperarSalida(int milisegundos) {
    const auto fin = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(milisegundos);
    while (std::chrono::steady_clock::now() < fin) {
        if (!vivo()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return !vivo();
}

void ProcesoHijo::terminarProceso() {
#ifdef _WIN32
    if (proceso_ != nullptr) {
        TerminateProcess(static_cast<HANDLE>(proceso_), 0);
    }
#else
    if (pid_ > 0) {
        kill(pid_, SIGTERM);
    }
#endif
}

void ProcesoHijo::matarArbol() {
#ifdef _WIN32
    if (proceso_ == nullptr) {
        return;
    }
    const DWORD identificador =
        GetProcessId(static_cast<HANDLE>(proceso_));
    if (identificador == 0) {
        return;
    }
    // En Windows terminar el padre no alcanza: los nietos quedan colgados.
    const std::string comando = "taskkill /PID " +
                                std::to_string(identificador) +
                                " /T /F >nul 2>&1";
    std::system(comando.c_str());
#else
    if (pid_ > 0) {
        // El hijo quedó en su propio grupo: matar al grupo mata al árbol.
        kill(-pid_, SIGKILL);
        kill(pid_, SIGKILL);
    }
#endif
}

void ProcesoHijo::matarForzado() {
#ifdef _WIN32
    if (proceso_ != nullptr) {
        TerminateProcess(static_cast<HANDLE>(proceso_), 1);
    }
#else
    if (pid_ > 0) {
        kill(pid_, SIGKILL);
    }
#endif
}

void ProcesoHijo::detener() {
    if (!arrancado_ || terminado_) {
        return;
    }

    terminarProceso();
    if (esperarSalida(kEsperaTerminarMs)) {
        terminado_ = true;
        return;
    }

    matarArbol();
    if (esperarSalida(kEsperaMatarArbolMs)) {
        terminado_ = true;
        return;
    }

    matarForzado();
    esperarSalida(kEsperaForzadoMs);
    terminado_ = true;
}

}  // namespace silvy
