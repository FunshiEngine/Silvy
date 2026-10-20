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
#include "Config/ConfigSilvy.h"

#include "Errores/Errores.h"

#include <fstream>
#include <iterator>

namespace silvy {

namespace {

bool existe(const std::filesystem::path& ruta) {
    std::error_code error;
    return std::filesystem::exists(ruta, error) && !error;
}

std::string leerArchivoTexto(const std::filesystem::path& ruta) {
    std::ifstream entrada(ruta, std::ios::binary);
    if (!entrada) {
        throw ErrorConfig("No se pudo leer el prompt de sistema en " +
                          ruta.string());
    }
    return std::string(std::istreambuf_iterator<char>(entrada),
                       std::istreambuf_iterator<char>());
}

}  // namespace

std::filesystem::path resolverCasa(const FuentesCasa& fuentes) {
    if (fuentes.argumento && !fuentes.argumento->empty()) {
        return std::filesystem::absolute(*fuentes.argumento);
    }
    if (fuentes.variableEntorno && !fuentes.variableEntorno->empty()) {
        return std::filesystem::absolute(*fuentes.variableEntorno);
    }
    return std::filesystem::absolute(fuentes.directorioBinario) / "silvy-home";
}

std::string cargarPromptSistema(const std::optional<std::string>& override,
                                const std::filesystem::path& casa,
                                const std::filesystem::path& directorioBinario) {
    if (override) {
        return *override;
    }
    const std::filesystem::path enCasa = casa / "system_prompt.md";
    if (existe(enCasa)) {
        return leerArchivoTexto(enCasa);
    }
    const std::filesystem::path juntoAlBinario =
        directorioBinario / "config" / "system_prompt.md";
    if (existe(juntoAlBinario)) {
        return leerArchivoTexto(juntoAlBinario);
    }
    throw ErrorConfig(
        "No se encontró el prompt de sistema: crea " + enCasa.string() +
        " o envía systemPrompt en la solicitud");
}

}  // namespace silvy
