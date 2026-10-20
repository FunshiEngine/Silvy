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
#include "Modelo/RutasLocales.h"

namespace silvy {
namespace rutas {

std::filesystem::path directorioModelos(const std::filesystem::path& casa) {
    return casa / "modelos";
}

std::filesystem::path modelo(const std::filesystem::path& casa,
                             const std::string& archivo) {
    return directorioModelos(casa) / archivo;
}

std::filesystem::path modeloParcial(const std::filesystem::path& casa,
                                    const std::string& archivo) {
    return directorioModelos(casa) / (archivo + ".part");
}

std::filesystem::path lockDescarga(const std::filesystem::path& casa,
                                    const std::string& archivo) {
    return directorioModelos(casa) / (archivo + ".lock");
}

std::filesystem::path logServidor(const std::filesystem::path& casa) {
    return casa / "logs" / "llama-server.log";
}

}  // namespace rutas
}  // namespace silvy
