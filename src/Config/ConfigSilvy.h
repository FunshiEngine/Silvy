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
#ifndef SILVY_CONFIG_CONFIG_SILVY_H
#define SILVY_CONFIG_CONFIG_SILVY_H

#include <filesystem>
#include <optional>
#include <string>

namespace silvy {

// Fuentes para resolver la casa de Silvy (datos de runtime: config, prompt de
// sistema editable, modelo GGUF, errores/ y logs).
struct FuentesCasa final {
    // Override explícito (--home).
    std::optional<std::filesystem::path> argumento;
    // Variable de entorno SILVY_HOME.
    std::optional<std::string> variableEntorno;
    // Directorio donde vive el binario de Silvy.
    std::filesystem::path directorioBinario;
};

// Precedencia: --home > SILVY_HOME > <directorio del binario>/silvy-home.
std::filesystem::path resolverCasa(const FuentesCasa& fuentes);

// Prompt de sistema efectivo: override del request > <casa>/system_prompt.md >
// <directorio del binario>/config/system_prompt.md. Sin ninguna fuente lanza
// NO_CONFIGURADO con la ubicación esperada.
std::string cargarPromptSistema(const std::optional<std::string>& override,
                                const std::filesystem::path& casa,
                                const std::filesystem::path& directorioBinario);

}  // namespace silvy

#endif  // SILVY_CONFIG_CONFIG_SILVY_H
