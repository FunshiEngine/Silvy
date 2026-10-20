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
#include "Comandos/ErroresComandos.h"

#include "Nucleo/Protocolo.h"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace silvy {

namespace {

constexpr std::size_t kLimiteErroresPorDefecto = 50;

std::size_t limiteDe(const nlohmann::json& argumentos) {
    if (!argumentos.contains("limite")) {
        return kLimiteErroresPorDefecto;
    }
    if (!argumentos["limite"].is_number_integer() ||
        argumentos["limite"].get<long long>() < 1) {
        lanzarSolicitudInvalida(
            "args.limite debe ser un entero mayor o igual a 1");
    }
    return static_cast<std::size_t>(
        argumentos["limite"].get<long long>());
}

std::optional<CodigoError> codigoDe(const nlohmann::json& argumentos) {
    if (!argumentos.contains("codigo")) {
        return std::nullopt;
    }
    if (!argumentos["codigo"].is_string()) {
        lanzarSolicitudInvalida("args.codigo debe ser un string");
    }
    const std::string texto = argumentos["codigo"].get<std::string>();
    const auto codigo = codigoDesdeTexto(texto);
    if (!codigo) {
        lanzarSolicitudInvalida("Código de error desconocido: " + texto);
    }
    return codigo;
}

}  // namespace

ResultadoComando ejecutarErroresList(const Solicitud& solicitud,
                                     DependenciasNucleo& dependencias) {
    const std::vector<nlohmann::json> errores = dependencias.registro.listar(
        limiteDe(solicitud.argumentos), codigoDe(solicitud.argumentos));

    nlohmann::json resultado;
    resultado["errores"] = errores;
    return ResultadoComando::exito(std::move(resultado));
}

ResultadoComando ejecutarErroresGet(const Solicitud& solicitud,
                                    DependenciasNucleo& dependencias) {
    if (!solicitud.argumentos.contains("id")) {
        lanzarSolicitudInvalida("args.id es obligatorio para errores.get");
    }
    if (!solicitud.argumentos["id"].is_string()) {
        lanzarSolicitudInvalida("args.id debe ser un string");
    }
    const std::string id = solicitud.argumentos["id"].get<std::string>();
    const std::optional<nlohmann::json> encontrado =
        dependencias.registro.obtener(id);

    nlohmann::json resultado;
    resultado["encontrado"] = encontrado.has_value();
    resultado["registro"] =
        encontrado ? *encontrado : nlohmann::json(nullptr);
    return ResultadoComando::exito(std::move(resultado));
}

}  // namespace silvy
