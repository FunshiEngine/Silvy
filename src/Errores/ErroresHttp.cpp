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
#include "Errores/ErroresHttp.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <string>

namespace silvy {

namespace {

constexpr std::size_t kLargoSnippet = 300;

std::string enMinusculas(std::string texto) {
    std::transform(texto.begin(), texto.end(), texto.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return texto;
}

// Fragmento legible del cuerpo: colapsa espacios y corta a kLargoSnippet.
std::string fragmentoLegible(const std::string& cuerpo) {
    std::string limpio;
    bool espacioPendiente = false;
    for (char caracter : cuerpo) {
        if (std::isspace(static_cast<unsigned char>(caracter)) != 0) {
            espacioPendiente = !limpio.empty();
            continue;
        }
        if (espacioPendiente) {
            limpio += ' ';
            espacioPendiente = false;
        }
        limpio += caracter;
        if (limpio.size() >= kLargoSnippet) {
            limpio += "...";
            break;
        }
    }
    return limpio;
}

bool contieneAlguna(const std::string& texto,
                    std::initializer_list<const char*> claves) {
    const std::string minusculas = enMinusculas(texto);
    for (const char* clave : claves) {
        if (minusculas.find(clave) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::string conSufijo(const std::string& mensaje, const std::string& cuerpo) {
    const std::string fragmento = fragmentoLegible(cuerpo);
    return fragmento.empty() ? mensaje : mensaje + ": " + fragmento;
}

}  // namespace

ErrorSilvy desdeHttp(int estado, const std::string& cuerpo, bool esLocal) {
    const std::string destino = esLocal ? "llama-server" : "el proveedor";

    if (estado == 400) {
        if (contieneAlguna(cuerpo, {"context", "kv cache", "tokens"})) {
            return ErrorSilvy(
                CodigoError::CONTEXTO_AGOTADO, false,
                conSufijo("El contexto del modelo se agotó (HTTP 400)", cuerpo));
        }
        return ErrorSilvy(
            CodigoError::SOLICITUD_INVALIDA, false,
            conSufijo("La solicitud fue rechazada (HTTP 400)", cuerpo));
    }
    if (estado == 401 || estado == 403) {
        return ErrorSilvy(
            CodigoError::CLAVE_INVALIDA, false,
            conSufijo("Clave de API inválida o sin permisos (HTTP " +
                          std::to_string(estado) + ")",
                      cuerpo));
    }
    if (estado == 404) {
        return ErrorSilvy(
            CodigoError::MODELO_NO_DISPONIBLE, false,
            conSufijo("Modelo o endpoint no disponible en " + destino +
                          " (HTTP 404)",
                      cuerpo));
    }
    if (estado == 408) {
        return ErrorTimeout(
            conSufijo("El proveedor agotó el tiempo de espera (HTTP 408)",
                      cuerpo));
    }
    if (estado == 429) {
        return ErrorSilvy(
            CodigoError::SIN_CUOTA, false,
            conSufijo("Cuota o límite de peticiones agotado (HTTP 429)",
                      cuerpo));
    }
    if (estado >= 500) {
        return ErrorServidorCaido(
            conSufijo("El servidor del LLM falló (HTTP " +
                          std::to_string(estado) + ")",
                      cuerpo));
    }
    if (estado >= 400) {
        return ErrorSilvy(
            CodigoError::SOLICITUD_INVALIDA, false,
            conSufijo("Solicitud rechazada (HTTP " + std::to_string(estado) +
                          ")",
                      cuerpo));
    }
    return ErrorSilvy(
        CodigoError::DESCONOCIDO, false,
        conSufijo("Respuesta HTTP inesperada (HTTP " + std::to_string(estado) +
                      ")",
                  cuerpo));
}

ErrorSilvy desdeRed(const std::string& detalle, bool esLocal) {
    if (esLocal) {
        return ErrorServidorCaido(
            "No se pudo hablar con llama-server: " + detalle);
    }
    return ErrorSilvy(
        CodigoError::MODELO_NO_DISPONIBLE, true,
        "No se pudo alcanzar el proveedor remoto: " + detalle);
}

}  // namespace silvy
