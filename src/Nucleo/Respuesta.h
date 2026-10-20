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
#ifndef SILVY_NUCLEO_RESPUESTA_H
#define SILVY_NUCLEO_RESPUESTA_H

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace silvy {

// Versión de la API expuesta por Silvy. Se incrementa al agregar
// funcionalidad; los campos nuevos de respuesta van al final.
inline constexpr int kVersionApiSilvy = 1;

// Códigos de salida del proceso. Cada comando devuelve un ResultadoComando que
// empaqueta la Respuesta con el código que debe devolver el proceso.
namespace codigosSalida {
// Operación completada con éxito.
inline constexpr int kExito = 0;
// Fallo operacional (LLM, registro, E/S, ...).
inline constexpr int kFalloGeneral = 1;
// Solicitud mal formada, comando desconocido o argumentos inválidos.
inline constexpr int kSolicitudInvalida = 2;
}  // namespace codigosSalida

// Respuesta pura: no sabe nada de stdout ni de la consola, así que la CLI
// puede consumirla igual que el transporte.
struct Respuesta final {
    bool correcta = false;
    nlohmann::json resultado = nullptr;
    // Objeto de error tipado (el mismo que quedó en errores.jsonl) o null.
    nlohmann::json error = nullptr;
    // Eco del identificador de la solicitud, si lo tenía.
    std::optional<nlohmann::json> id;

    static Respuesta ok(nlohmann::json resultado);
    static Respuesta fallo(nlohmann::json error);

    // Forma JSON de salida: {apiVersion, ok, error, result, id?}.
    nlohmann::json aJson() const;
    // Una línea compacta, lista para escribirse a un stream.
    std::string aTexto() const;
};

// Empareja una Respuesta con el código de salida del proceso.
struct ResultadoComando final {
    Respuesta respuesta;
    int codigoSalida = codigosSalida::kFalloGeneral;

    static ResultadoComando exito(nlohmann::json resultado);
    static ResultadoComando fallo(nlohmann::json error, int codigoSalida);
};

}  // namespace silvy

#endif  // SILVY_NUCLEO_RESPUESTA_H
