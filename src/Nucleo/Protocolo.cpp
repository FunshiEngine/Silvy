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
#include "Nucleo/Protocolo.h"

#include <stdexcept>
#include <utility>

namespace silvy {

void lanzarSolicitudInvalida(const std::string& mensaje) {
    throw ErrorSilvy(CodigoError::SOLICITUD_INVALIDA, false, mensaje);
}

std::string textoRequerido(const nlohmann::json& objeto, const char* campo,
                           const std::string& ruta) {
    if (!objeto.is_object() || !objeto.contains(campo)) {
        lanzarSolicitudInvalida(ruta + ": falta el campo " + campo);
    }
    if (!objeto[campo].is_string()) {
        lanzarSolicitudInvalida(ruta + "." + campo + " debe ser un string");
    }
    return objeto[campo].get<std::string>();
}

std::string textoDeRol(RolMensaje rol) {
    switch (rol) {
        case RolMensaje::SISTEMA:
            return "system";
        case RolMensaje::USUARIO:
            return "user";
        case RolMensaje::ASISTENTE:
            return "assistant";
        case RolMensaje::HERRAMIENTA:
            return "tool";
    }
    throw std::logic_error("Rol de mensaje sin texto de protocolo");
}

std::optional<RolMensaje> rolDesdeTexto(const std::string& texto) {
    if (texto == "system") {
        return RolMensaje::SISTEMA;
    }
    if (texto == "user") {
        return RolMensaje::USUARIO;
    }
    if (texto == "assistant") {
        return RolMensaje::ASISTENTE;
    }
    if (texto == "tool") {
        return RolMensaje::HERRAMIENTA;
    }
    return std::nullopt;
}

std::string textoDeRazonFin(RazonFin razon) {
    switch (razon) {
        case RazonFin::DETENCION:
            return "stop";
        case RazonFin::LLAMADAS_HERRAMIENTA:
            return "tool_calls";
        case RazonFin::LONGITUD:
            return "length";
        case RazonFin::DESCONOCIDA:
            return "desconocida";
    }
    throw std::logic_error("Razón de fin sin texto de protocolo");
}

RazonFin razonFinDesdeTexto(const std::string& texto) {
    if (texto == "stop") {
        return RazonFin::DETENCION;
    }
    if (texto == "tool_calls") {
        return RazonFin::LLAMADAS_HERRAMIENTA;
    }
    if (texto == "length") {
        return RazonFin::LONGITUD;
    }
    return RazonFin::DESCONOCIDA;
}

namespace {

LlamadaHerramienta llamadaDesdeJson(const nlohmann::json& objeto,
                                    const std::string& ruta) {
    if (!objeto.is_object()) {
        lanzarSolicitudInvalida(ruta + " debe ser un objeto JSON");
    }
    LlamadaHerramienta llamada;
    llamada.id = textoRequerido(objeto, "id", ruta);
    llamada.nombre = textoRequerido(objeto, "name", ruta);
    if (llamada.id.empty()) {
        lanzarSolicitudInvalida(ruta + ".id no puede estar vacío");
    }
    if (llamada.nombre.empty()) {
        lanzarSolicitudInvalida(ruta + ".name no puede estar vacío");
    }
    if (objeto.contains("arguments")) {
        if (!objeto["arguments"].is_object()) {
            lanzarSolicitudInvalida(ruta + ".arguments debe ser un objeto JSON");
        }
        llamada.argumentos = objeto["arguments"];
    }
    return llamada;
}

}  // namespace

Mensaje mensajeDesdeJson(const nlohmann::json& objeto) {
    if (!objeto.is_object()) {
        lanzarSolicitudInvalida("Cada mensaje debe ser un objeto JSON");
    }
    Mensaje mensaje;
    const std::string textoRol = textoRequerido(objeto, "role", "mensaje");
    const auto rol = rolDesdeTexto(textoRol);
    if (!rol) {
        lanzarSolicitudInvalida("Rol de mensaje desconocido: " + textoRol);
    }
    mensaje.rol = *rol;

    // content null es válido (tool-calling estilo OpenAI); se normaliza a "".
    if (objeto.contains("content") && !objeto["content"].is_null()) {
        if (!objeto["content"].is_string()) {
            lanzarSolicitudInvalida("mensaje.content debe ser un string");
        }
        mensaje.contenido = objeto["content"].get<std::string>();
    }

    if (objeto.contains("toolCalls")) {
        if (!objeto["toolCalls"].is_array()) {
            lanzarSolicitudInvalida("mensaje.toolCalls debe ser un array");
        }
        int indice = 0;
        for (const auto& llamada : objeto["toolCalls"]) {
            mensaje.llamadas.push_back(llamadaDesdeJson(
                llamada, "mensaje.toolCalls[" + std::to_string(indice) + "]"));
            ++indice;
        }
    }

    if (objeto.contains("toolCallId")) {
        if (!objeto["toolCallId"].is_string()) {
            lanzarSolicitudInvalida("mensaje.toolCallId debe ser un string");
        }
        mensaje.idLlamada = objeto["toolCallId"].get<std::string>();
    }

    if (mensaje.rol == RolMensaje::HERRAMIENTA && mensaje.idLlamada.empty()) {
        lanzarSolicitudInvalida("Un mensaje con rol tool requiere toolCallId");
    }
    return mensaje;
}

nlohmann::json mensajeAJson(const Mensaje& mensaje) {
    nlohmann::json objeto;
    objeto["role"] = textoDeRol(mensaje.rol);
    objeto["content"] = mensaje.contenido;
    if (!mensaje.llamadas.empty()) {
        nlohmann::json llamadas = nlohmann::json::array();
        for (const auto& llamada : mensaje.llamadas) {
            llamadas.push_back(llamadaAJson(llamada));
        }
        objeto["toolCalls"] = std::move(llamadas);
    }
    if (!mensaje.idLlamada.empty()) {
        objeto["toolCallId"] = mensaje.idLlamada;
    }
    return objeto;
}

nlohmann::json llamadaAJson(const LlamadaHerramienta& llamada) {
    nlohmann::json objeto;
    objeto["id"] = llamada.id;
    objeto["name"] = llamada.nombre;
    objeto["arguments"] = llamada.argumentos;
    return objeto;
}

Herramienta herramientaDesdeJson(const nlohmann::json& objeto) {
    if (!objeto.is_object()) {
        lanzarSolicitudInvalida("Cada herramienta debe ser un objeto JSON");
    }
    Herramienta herramienta;
    herramienta.nombre = textoRequerido(objeto, "name", "herramienta");
    if (herramienta.nombre.empty()) {
        lanzarSolicitudInvalida("herramienta.name no puede estar vacío");
    }
    if (objeto.contains("description") && !objeto["description"].is_null()) {
        if (!objeto["description"].is_string()) {
            lanzarSolicitudInvalida("herramienta.description debe ser un string");
        }
        herramienta.descripcion = objeto["description"].get<std::string>();
    }
    if (objeto.contains("parameters")) {
        if (!objeto["parameters"].is_object()) {
            lanzarSolicitudInvalida("herramienta.parameters debe ser un objeto JSON");
        }
        herramienta.parametros = objeto["parameters"];
    }
    return herramienta;
}

nlohmann::json herramientaAJson(const Herramienta& herramienta) {
    nlohmann::json objeto;
    objeto["name"] = herramienta.nombre;
    objeto["description"] = herramienta.descripcion;
    objeto["parameters"] = herramienta.parametros;
    return objeto;
}

}  // namespace silvy
