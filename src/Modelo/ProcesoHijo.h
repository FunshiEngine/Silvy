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
#ifndef SILVY_MODELO_PROCESO_HIJO_H
#define SILVY_MODELO_PROCESO_HIJO_H

#include <filesystem>
#include <string>
#include <vector>

namespace silvy {

// Proceso hijo con su salida combinada (stdout y stderr) dirigida a un
// archivo de log. El archivo nunca se llena, así que el hijo no se bloquea
// escribiendo, y de ahí se lee el tail cuando hace falta.
//
// En Windows el apagado mata el árbol de procesos (taskkill /T /F) porque
// los nietos de llama-server quedan colgados si solo se termina el padre.
class ProcesoHijo final {
public:
    struct Opciones final {
        // Argumentos del proceso; el primero es el ejecutable.
        std::vector<std::string> argumentos;
        // Archivo donde se vuelca stdout y stderr.
        std::filesystem::path log;
        // Directorio de trabajo del proceso.
        std::filesystem::path directorioTrabajo;
    };

    explicit ProcesoHijo(Opciones opciones);
    ~ProcesoHijo();

    ProcesoHijo(const ProcesoHijo&) = delete;
    ProcesoHijo& operator=(const ProcesoHijo&) = delete;

    // Arranca el proceso. Lanza ErrorSilvy (NO_CONFIGURADO) si no se puede
    // ejecutar.
    void arrancar();

    // true si el proceso sigue en pie.
    bool vivo() const;

    // Código de salida del proceso; -1 si todavía no terminó.
    int codigoSalida() const;

    // Últimas líneas del log (las que quepan en `maxBytes`, como máximo
    // `maxLineas`).
    std::string tail(std::size_t maxBytes, std::size_t maxLineas) const;

    // Apagado escalonado: terminar, esperar, matar el árbol, esperar y
    // forzar. No hace nada si el proceso ya terminó.
    void detener();

private:
    void terminarProceso();
    void matarArbol();
    void matarForzado();
    bool esperarSalida(int milisegundos);

    Opciones opciones_;
    bool arrancado_ = false;
    mutable bool terminado_ = false;
    mutable int codigoSalida_ = -1;

#ifdef _WIN32
    void* proceso_ = nullptr;   // HANDLE
    void* procesoHijo_ = nullptr;  // HANDLE
#else
    int pid_ = -1;
#endif
};

}  // namespace silvy

#endif  // SILVY_MODELO_PROCESO_HIJO_H
