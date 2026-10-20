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
#ifndef SILVY_CONFIG_CONFIG_LLM_H
#define SILVY_CONFIG_CONFIG_LLM_H

#include "Errores/Errores.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace silvy {

// Modo de inferencia de Silvy. LOCAL usa llama-server con un GGUF propio;
// REMOTE habla con una API compatible OpenAI.
enum class ModoLlm {
    LOCAL,
    REMOTE,
};

std::string textoDeModo(ModoLlm modo);

// Lectura inversa; sin valor si el texto no corresponde a un modo conocido.
std::optional<ModoLlm> modoDesdeTexto(const std::string& texto);

// Configuración del LLM. Los campos del request pisan a los de la casa; lo
// ausente conserva el valor base. Las distancias y esperas se expresan en
// milisegundos.
struct ConfigLlm final {
    ModoLlm modo = ModoLlm::LOCAL;
    // Base de la API compatible OpenAI, sin la ruta del endpoint
    // (p. ej. "https://api.openai.com/v1").
    std::string baseUrl;
    std::string apiKey;
    std::string modelName;
    double temperatura = 0.2;
    // Tiempo máximo de la operación completa, incluido el streaming.
    int timeoutTotalMs = 180000;
    // Tiempo máximo para establecer la conexión HTTP.
    int timeoutConexionMs = 10000;
    // Reintentos ante errores reintentables (2 = hasta 3 intentos).
    int maxReintentos = 2;
    // Espera base del backoff exponencial: base, 2·base, 4·base...
    int esperaBaseMs = 300;

    // Config efectiva: aplica los campos de `override` (config del request).
    // Un campo con tipo incorrecto lanza SOLICITUD_INVALIDA.
    ConfigLlm conOverride(const nlohmann::json& override) const;

    // Valida la combinación completa y lanza ErrorConfig con TODOS los
    // problemas encontrados, no solo el primero.
    void validar() const;
};

// Carga <casa>/config.json si existe. Sin archivo devuelve los defaults; un
// JSON malformado o con campos de tipo incorrecto lanza ErrorConfig.
ConfigLlm cargarConfigLlm(const std::filesystem::path& casa);

// true si la base apunta a la propia máquina (localhost, 127.0.0.1, ::1):
// ahí la API key es opcional.
bool esDestinoLocal(const std::string& baseUrl);

}  // namespace silvy

#endif  // SILVY_CONFIG_CONFIG_LLM_H
