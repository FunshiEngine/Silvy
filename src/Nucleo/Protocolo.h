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
#ifndef SILVY_NUCLEO_PROTOCOLO_H
#define SILVY_NUCLEO_PROTOCOLO_H

#include "Errores/Errores.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace silvy {

// Tipos y traducción JSON del protocolo CLI↔Silvy. Los campos viajan en
// inglés (role, content, toolCalls, name, arguments, ...) y los valores de
// texto en español; los tipos internos usan nombres en español.

// Rol de un mensaje del historial.
enum class RolMensaje {
    SISTEMA,
    USUARIO,
    ASISTENTE,
    HERRAMIENTA,
};

// Texto de protocolo de un rol ("system", "user", "assistant", "tool").
std::string textoDeRol(RolMensaje rol);

// Lectura inversa; sin valor si el texto no corresponde a un rol conocido.
std::optional<RolMensaje> rolDesdeTexto(const std::string& texto);

// Razón de fin del turno del LLM, en valores del protocolo OpenAI-compatible.
enum class RazonFin {
    DETENCION,            // "stop"
    LLAMADAS_HERRAMIENTA, // "tool_calls"
    LONGITUD,             // "length"
    DESCONOCIDA,          // cualquier otro valor del proveedor
};

std::string textoDeRazonFin(RazonFin razon);

// Un valor desconocido no rompe el paso: se normaliza a DESCONOCIDA.
RazonFin razonFinDesdeTexto(const std::string& texto);

// Llamada a herramienta pedida por el asistente.
struct LlamadaHerramienta final {
    std::string id;
    std::string nombre;
    nlohmann::json argumentos = nlohmann::json::object();
};

// Herramienta del catálogo que arma la CLI (formato OpenAI: name, description,
// parameters con JSON Schema).
struct Herramienta final {
    std::string nombre;
    std::string descripcion;
    nlohmann::json parametros = nlohmann::json::object();
};

// Mensaje del historial de conversación.
struct Mensaje final {
    RolMensaje rol = RolMensaje::USUARIO;
    std::string contenido;
    // Solo en mensajes del asistente que piden herramientas.
    std::vector<LlamadaHerramienta> llamadas;
    // Solo en mensajes de herramienta: id de la llamada que responden.
    std::string idLlamada;
};

// Traducción JSON↔tipos. Los parsers validan y lanzan SOLICITUD_INVALIDA con
// un mensaje accionable (campo y motivo); los serializadores no fallan.
Mensaje mensajeDesdeJson(const nlohmann::json& objeto);
nlohmann::json mensajeAJson(const Mensaje& mensaje);
Herramienta herramientaDesdeJson(const nlohmann::json& objeto);
nlohmann::json herramientaAJson(const Herramienta& herramienta);
nlohmann::json llamadaAJson(const LlamadaHerramienta& llamada);

// Validación compartida por los parsers del protocolo.
[[noreturn]] void lanzarSolicitudInvalida(const std::string& mensaje);

// Lee un campo string obligatorio; lanza SOLICITUD_INVALIDA si falta o no es
// string. `ruta` es el prefijo que se muestra en el mensaje ("solicitud",
// "mensaje.toolCalls[0]", ...).
std::string textoRequerido(const nlohmann::json& objeto, const char* campo,
                           const std::string& ruta);

}  // namespace silvy

#endif  // SILVY_NUCLEO_PROTOCOLO_H
