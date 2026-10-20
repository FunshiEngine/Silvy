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
#ifndef SILVY_ERRORES_ERRORES_H
#define SILVY_ERRORES_ERRORES_H

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace silvy {

// Código semántico de un error de Silvy. Cada valor identifica la causa del
// fallo y su política de reintento, de modo que quien decide no tenga que
// interpretar el mensaje: la decisión viaja en el error. El texto UPPER_SNAKE
// es el que viaja en el protocolo y en el registro persistente.
enum class CodigoError {
    SIN_CUOTA,
    CLAVE_INVALIDA,
    TIMEOUT,
    NO_CONFIGURADO,
    MODELO_NO_DISPONIBLE,
    CONTEXTO_AGOTADO,
    DESCARGA_FALLIDA,
    SERVIDOR_CAIDO,
    CANCELADO,
    SOLICITUD_INVALIDA,
    SIN_SALUD,
    DESCONOCIDO,
};

// Texto de protocolo de un código ("SERVIDOR_CAIDO", "CANCELADO", ...).
std::string textoDeCodigo(CodigoError codigo);

// Lectura inversa del texto de protocolo; sin valor si no corresponde a
// ningún código conocido.
std::optional<CodigoError> codigoDesdeTexto(const std::string& texto);

// Todos los códigos, en el orden del enum. Lo usan los mensajes de validación
// y las pruebas.
std::vector<CodigoError> todosLosCodigos();

// Causa encadenada de un error: la excepción original o envuelta, con su
// propia causa (cadena completa hasta la raíz). Es lo que se persiste junto
// al error principal.
struct CausaError final {
    CodigoError codigo = CodigoError::DESCONOCIDO;
    std::string mensaje;
    std::shared_ptr<const CausaError> causa;
};

// Excepción tipada de Silvy: código semántico, decisión de reintento y causa
// opcional, siempre disponibles sin interpretar el mensaje.
class ErrorSilvy : public std::runtime_error {
public:
    ErrorSilvy(CodigoError codigo, bool reintentable, const std::string& mensaje);
    ErrorSilvy(CodigoError codigo, bool reintentable, const std::string& mensaje,
               CausaError causa);

    CodigoError codigo() const noexcept;
    // true si un reintento posterior puede resolver el fallo; los errores
    // deterministas (clave inválida, config, solicitud) son false.
    bool reintentable() const noexcept;
    const std::optional<CausaError>& causa() const noexcept;

private:
    CodigoError codigo_;
    bool reintentable_;
    std::optional<CausaError> causa_;
};

// Subtipos con la política de reintento por defecto donde aporta claridad.

class ErrorTimeout final : public ErrorSilvy {
public:
    explicit ErrorTimeout(const std::string& mensaje);
};

// La cancelación la pidió el humano (o la UI): nunca es reintentable ni debe
// presentarse como fallo.
class ErrorCancelado final : public ErrorSilvy {
public:
    explicit ErrorCancelado(const std::string& mensaje);
};

class ErrorServidorCaido final : public ErrorSilvy {
public:
    explicit ErrorServidorCaido(const std::string& mensaje);
};

// Error de configuración: acumula TODOS los problemas detectados en un solo
// mensaje multilínea; validar() nunca falla por el primero.
class ErrorConfig final : public ErrorSilvy {
public:
    explicit ErrorConfig(const std::string& mensaje);

    // Arma el error a partir de la lista completa de problemas.
    static ErrorConfig conProblemas(const std::vector<std::string>& problemas);
};

// Clasificadores: punto único de normalización de errores ajenos al dominio
// de Silvy.

// Causa (con su cadena) a partir de cualquier excepción.
CausaError causaDeExcepcion(const std::exception& error);

// Normaliza cualquier std::exception a ErrorSilvy conservando la cadena de
// causas: un error ya tipado se devuelve tal cual; el resto pasa a
// DESCONOCIDO con la excepción original como causa.
ErrorSilvy envolver(const std::exception& error);

}  // namespace silvy

#endif  // SILVY_ERRORES_ERRORES_H
