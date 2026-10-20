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
#include "Errores/Errores.h"

#include <utility>

namespace silvy {

std::string textoDeCodigo(CodigoError codigo) {
    switch (codigo) {
        case CodigoError::SIN_CUOTA:
            return "SIN_CUOTA";
        case CodigoError::CLAVE_INVALIDA:
            return "CLAVE_INVALIDA";
        case CodigoError::TIMEOUT:
            return "TIMEOUT";
        case CodigoError::NO_CONFIGURADO:
            return "NO_CONFIGURADO";
        case CodigoError::MODELO_NO_DISPONIBLE:
            return "MODELO_NO_DISPONIBLE";
        case CodigoError::CONTEXTO_AGOTADO:
            return "CONTEXTO_AGOTADO";
        case CodigoError::DESCARGA_FALLIDA:
            return "DESCARGA_FALLIDA";
        case CodigoError::SERVIDOR_CAIDO:
            return "SERVIDOR_CAIDO";
        case CodigoError::CANCELADO:
            return "CANCELADO";
        case CodigoError::SOLICITUD_INVALIDA:
            return "SOLICITUD_INVALIDA";
        case CodigoError::SIN_SALUD:
            return "SIN_SALUD";
        case CodigoError::DESCONOCIDO:
            return "DESCONOCIDO";
    }
    // Inalcanzable con el switch exhaustivo; se deja por robustez.
    throw std::logic_error("Código de error sin texto de protocolo");
}

std::optional<CodigoError> codigoDesdeTexto(const std::string& texto) {
    for (const CodigoError codigo : todosLosCodigos()) {
        if (textoDeCodigo(codigo) == texto) {
            return codigo;
        }
    }
    return std::nullopt;
}

std::vector<CodigoError> todosLosCodigos() {
    return {
        CodigoError::SIN_CUOTA,       CodigoError::CLAVE_INVALIDA,
        CodigoError::TIMEOUT,         CodigoError::NO_CONFIGURADO,
        CodigoError::MODELO_NO_DISPONIBLE, CodigoError::CONTEXTO_AGOTADO,
        CodigoError::DESCARGA_FALLIDA, CodigoError::SERVIDOR_CAIDO,
        CodigoError::CANCELADO,       CodigoError::SOLICITUD_INVALIDA,
        CodigoError::SIN_SALUD,       CodigoError::DESCONOCIDO,
    };
}

ErrorSilvy::ErrorSilvy(CodigoError codigo, bool reintentable,
                       const std::string& mensaje)
    : std::runtime_error(mensaje), codigo_(codigo), reintentable_(reintentable) {}

ErrorSilvy::ErrorSilvy(CodigoError codigo, bool reintentable,
                       const std::string& mensaje, CausaError causa)
    : std::runtime_error(mensaje),
      codigo_(codigo),
      reintentable_(reintentable),
      causa_(std::move(causa)) {}

CodigoError ErrorSilvy::codigo() const noexcept { return codigo_; }

bool ErrorSilvy::reintentable() const noexcept { return reintentable_; }

const std::optional<CausaError>& ErrorSilvy::causa() const noexcept {
    return causa_;
}

ErrorTimeout::ErrorTimeout(const std::string& mensaje)
    : ErrorSilvy(CodigoError::TIMEOUT, true, mensaje) {}

ErrorCancelado::ErrorCancelado(const std::string& mensaje)
    : ErrorSilvy(CodigoError::CANCELADO, false, mensaje) {}

ErrorServidorCaido::ErrorServidorCaido(const std::string& mensaje)
    : ErrorSilvy(CodigoError::SERVIDOR_CAIDO, true, mensaje) {}

ErrorConfig::ErrorConfig(const std::string& mensaje)
    : ErrorSilvy(CodigoError::NO_CONFIGURADO, false, mensaje) {}

ErrorConfig ErrorConfig::conProblemas(const std::vector<std::string>& problemas) {
    std::string mensaje = "Configuración inválida:";
    for (const std::string& problema : problemas) {
        mensaje += "\n - " + problema;
    }
    return ErrorConfig(mensaje);
}

CausaError causaDeExcepcion(const std::exception& error) {
    CausaError causa;
    if (const auto* tipado = dynamic_cast<const ErrorSilvy*>(&error)) {
        causa.codigo = tipado->codigo();
        causa.mensaje = tipado->what();
        if (tipado->causa()) {
            causa.causa = std::make_shared<const CausaError>(*tipado->causa());
        }
        return causa;
    }
    causa.codigo = CodigoError::DESCONOCIDO;
    causa.mensaje = error.what();
    return causa;
}

ErrorSilvy envolver(const std::exception& error) {
    if (const auto* tipado = dynamic_cast<const ErrorSilvy*>(&error)) {
        // Ya está tipado: se devuelve tal cual, conservando su cadena.
        return *tipado;
    }
    return ErrorSilvy(CodigoError::DESCONOCIDO, false,
                      std::string("Error no identificado: ") + error.what(),
                      causaDeExcepcion(error));
}

}  // namespace silvy
