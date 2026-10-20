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
#ifndef SILVY_MODELO_RUTAS_LOCALES_H
#define SILVY_MODELO_RUTAS_LOCALES_H

#include <filesystem>
#include <string>

namespace silvy {

// Rutas de los datos del LLM local dentro de la casa de Silvy. La casa es la
// única zona donde Silvy escribe en runtime: modelo descargado, log del
// servidor local y lock de descarga.
namespace rutas {

// Directorio de los modelos descargados (<casa>/modelos).
std::filesystem::path directorioModelos(const std::filesystem::path& casa);

// Archivo final del GGUF (<casa>/modelos/<archivo>).
std::filesystem::path modelo(const std::filesystem::path& casa,
                             const std::string& archivo);

// Archivo parcial de la descarga (<casa>/modelos/<archivo>.part): lo que ya
// se bajó y que permite reanudar.
std::filesystem::path modeloParcial(const std::filesystem::path& casa,
                                    const std::string& archivo);

// Lock de descarga por ruta (<casa>/modelos/<archivo>.lock): garantiza que
// dos procesos no descargan el mismo archivo a la vez.
std::filesystem::path lockDescarga(const std::filesystem::path& casa,
                                    const std::string& archivo);

// Log de `llama-server` (<casa>/logs/llama-server.log).
std::filesystem::path logServidor(const std::filesystem::path& casa);

}  // namespace rutas
}  // namespace silvy

#endif  // SILVY_MODELO_RUTAS_LOCALES_H
