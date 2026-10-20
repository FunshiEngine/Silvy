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
#ifndef SILVY_MODELO_SUPERVISOR_LLAMA_H
#define SILVY_MODELO_SUPERVISOR_LLAMA_H

#include "Modelo/ProcesoHijo.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace silvy {

// Datos del servidor local que hay que levantar.
struct OpcionesServidorLocal final {
    // Ruta o nombre del ejecutable llama-server.
    std::string rutaEjecutable = "llama-server";
    // GGUF que se sirve.
    std::filesystem::path modelo;
    // Host donde escucha (siempre loopback).
    std::string host = "127.0.0.1";
    // Puerto preferido; si está ocupado se prueban los siguientes.
    int puerto = 8081;
    // Cuántos puertos consecutivos se prueban.
    int puertoScan = 20;
    // Contexto pedido al servidor (-c).
    int contextSize = 8192;
    // Capas de GPU (-ngl).
    int gpuLayers = 0;
    // Log del servidor.
    std::filesystem::path log;
    // Tiempo máximo de espera a que el modelo esté cargado.
    int timeoutCargaMs = 180000;
};

// Argumentos con los que se lanza llama-server. Es una función pura: el
// supervisor la usa y las pruebas la comparan.
std::vector<std::string> argumentosLlamaServer(
    const OpcionesServidorLocal& opciones);

// Busca el primer puerto libre en [inicio, inicio + cantidad). Sin valor si
// no hay ninguno.
std::optional<int> buscarPuertoLibre(int inicio, int cantidad);

// Porcentaje de carga del modelo que aparece en el log ("45.2%"); sin valor
// si el log todavía no lo dice.
std::optional<int> porcentajeDeCarga(const std::string& log);

// Resuelve el ejecutable de llama-server: la ruta explícita si existe (con
// .exe en Windows) o el que está en el PATH. Sin valor si no se encuentra.
std::optional<std::filesystem::path> resolverEjecutableLlama(
    const std::string& rutaConfigurada);

// Levanta y vigila llama-server: arranca el proceso, espera a que responda
// el health-check, detecta si se cae y lo apaga de forma escalonada.
class SupervisorLlama final {
public:
    explicit SupervisorLlama(OpcionesServidorLocal opciones);
    ~SupervisorLlama();

    SupervisorLlama(const SupervisorLlama&) = delete;
    SupervisorLlama& operator=(const SupervisorLlama&) = delete;

    // Arranca el servidor (si no está vivo) y espera a que esté sano.
    // Devuelve el puerto efectivo. Lanza ErrorSilvy si el proceso muere o si
    // el modelo no carga a tiempo.
    int asegurarListo();

    // true si el proceso está en pie.
    bool vivo() const;

    // Puerto donde escucha el servidor (0 si no arrancó).
    int puerto() const;

    // Últimas líneas del log del servidor.
    std::string tailLog() const;

    // Porcentaje de carga que dice el log.
    std::optional<int> porcentajeCarga() const;

    // Apagado escalonado del servidor.
    void detener();

private:
    bool sano() const;
    void arrancarProceso();

    OpcionesServidorLocal opciones_;
    std::unique_ptr<ProcesoHijo> proceso_;
    int puerto_ = 0;
};

}  // namespace silvy

#endif  // SILVY_MODELO_SUPERVISOR_LLAMA_H
