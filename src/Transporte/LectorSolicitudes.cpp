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
#include "Transporte/LectorSolicitudes.h"

#include "Errores/Errores.h"

#include <string>

namespace silvy {

namespace {

// Posición justo después del cierre del primer objeto JSON completo del
// texto. Sin valor si el objeto todavía no cerró. Cuenta la profundidad de
// llaves y corchetes, respetando cadenas y escapes.
std::optional<std::size_t> finDeJson(const std::string& texto) {
    int profundidad = 0;
    bool enCadena = false;
    bool escapado = false;

    for (std::size_t indice = 0; indice < texto.size(); ++indice) {
        const char caracter = texto[indice];
        if (enCadena) {
            if (escapado) {
                escapado = false;
            } else if (caracter == '\\') {
                escapado = true;
            } else if (caracter == '"') {
                enCadena = false;
            }
            continue;
        }
        if (caracter == '"') {
            enCadena = true;
            continue;
        }
        if (caracter == '{' || caracter == '[') {
            ++profundidad;
            continue;
        }
        if (caracter == '}' || caracter == ']') {
            --profundidad;
            if (profundidad <= 0) {
                return indice + 1;
            }
        }
    }
    return std::nullopt;
}

// true si el texto solo tiene espacios (nada que entregar).
bool soloEspacios(const std::string& texto) {
    for (const char caracter : texto) {
        if (caracter != ' ' && caracter != '\t' && caracter != '\r' &&
            caracter != '\n') {
            return false;
        }
    }
    return true;
}

}  // namespace

LectorSolicitudes::LectorSolicitudes(std::istream& entrada)
    : entrada_(entrada) {}

std::optional<std::string> LectorSolicitudes::siguiente() {
    while (true) {
        const std::optional<std::size_t> fin = finDeJson(acumulado_);
        if (fin) {
            // La solicitud termina donde cierra el JSON; lo que sobra queda
            // acumulado para la siguiente.
            std::string solicitud = acumulado_.substr(0, *fin);
            acumulado_.erase(0, *fin);
            return solicitud;
        }

        if (fin_) {
            if (soloEspacios(acumulado_)) {
                acumulado_.clear();
                return std::nullopt;
            }
            throw ErrorSilvy(CodigoError::SOLICITUD_INVALIDA, false,
                             "JSON inválido: " + acumulado_);
        }

        std::string linea;
        if (!std::getline(entrada_, linea)) {
            fin_ = true;
            continue;
        }
        acumulado_ += linea;
        acumulado_ += '\n';
    }
}

}  // namespace silvy
