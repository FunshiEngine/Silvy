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
#ifndef SILVY_ERRORES_REGISTRO_ERRORES_H
#define SILVY_ERRORES_REGISTRO_ERRORES_H

#include "Errores/Errores.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace silvy {

// Configuración del registro persistente de excepciones. Todos los campos
// tienen defaults razonables; validar() devuelve TODOS los problemas juntos.
struct ConfigRegistroErrores final {
    // Directorio donde se guardan errores.jsonl y sus rotados.
    std::filesystem::path directorio;
    // Tamaño máximo de errores.jsonl antes de rotar (KB).
    std::size_t tamMaximoKb = 256;
    // Cantidad máxima de archivos rotados que se conservan.
    std::size_t maximoArchivos = 10;
    // Antigüedad máxima de un archivo rotado (días). 0 = sin límite.
    std::size_t retencionDias = 30;

    std::vector<std::string> validar() const;
};

// Contexto del error que se persiste junto a él: qué comando lo originó, en
// qué modo/modelo y datos accionables opcionales (exitCode, logTail, etc.).
struct ContextoError final {
    std::string comando;
    std::string modo;
    std::string modelo;
    nlohmann::json detalle = nlohmann::json::object();
};

// Registro persistente de excepciones: append-only en errores.jsonl con
// rotación por tamaño y retención por cantidad/tiempo. La escritura va por
// una cola con su propio hilo; si el disco falla, degrada a log de consola
// sin romper la respuesta principal.
class RegistroErrores final {
public:
    explicit RegistroErrores(ConfigRegistroErrores config);
    ~RegistroErrores();

    RegistroErrores(const RegistroErrores&) = delete;
    RegistroErrores& operator=(const RegistroErrores&) = delete;

    // Encola la excepción para persistencia. Devuelve el objeto exacto (con
    // id y ts) que se escribe: es el que la respuesta ok:false muestra al
    // usuario.
    nlohmann::json registrar(const ErrorSilvy& error, const ContextoError& contexto);

    // Espera a que la cola de escritura se vacíe. Las consultas lo hacen
    // antes de leer para ver todo lo registrado.
    void vaciar();

    // Últimos `limite` registros, más reciente primero, con filtro opcional
    // por código.
    std::vector<nlohmann::json> listar(std::size_t limite = 100,
                                       std::optional<CodigoError> codigo = std::nullopt);

    // Registro por id, si existe en los archivos vigentes.
    std::optional<nlohmann::json> obtener(const std::string& id);

private:
    struct Impl;
    std::unique_ptr<Impl> pimpl_;
};

}  // namespace silvy

#endif  // SILVY_ERRORES_REGISTRO_ERRORES_H
