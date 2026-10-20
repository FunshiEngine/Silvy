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
#include "Config/ConfigLlm.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace silvy {

namespace {

constexpr int kTimeoutTotalMaxMs = 3600000;
constexpr int kTimeoutConexionMaxMs = 600000;
constexpr int kReintentosMax = 10;
constexpr int kEsperaBaseMaxMs = 60000;

// Un campo con forma incorrecta es error de solicitud cuando viene del
// request y error de configuración cuando viene del archivo de la casa.
[[noreturn]] void errorDeCampo(bool esArchivo, const std::string& mensaje) {
    if (esArchivo) {
        throw ErrorConfig("Configuración inválida: " + mensaje);
    }
    throw ErrorSilvy(CodigoError::SOLICITUD_INVALIDA, false,
                     "config del request: " + mensaje);
}

bool tieneValor(const nlohmann::json& objeto, const char* clave) {
    const auto campo = objeto.find(clave);
    return campo != objeto.end() && !campo->is_null();
}

std::string textoDeCampo(const nlohmann::json& objeto, const char* clave,
                         bool esArchivo) {
    const auto& valor = objeto.at(clave);
    if (!valor.is_string()) {
        errorDeCampo(esArchivo,
                     std::string("'") + clave + "' debe ser un string");
    }
    return valor.get<std::string>();
}

long long enteroDeCampo(const nlohmann::json& objeto, const char* clave,
                        bool esArchivo) {
    const auto& valor = objeto.at(clave);
    if (!valor.is_number_integer()) {
        errorDeCampo(esArchivo,
                     std::string("'") + clave + "' debe ser un entero");
    }
    return valor.get<long long>();
}

int enteroAcotado(const nlohmann::json& objeto, const char* clave,
                  bool esArchivo, long long minimo, long long maximo) {
    const long long valor = enteroDeCampo(objeto, clave, esArchivo);
    if (valor < minimo || valor > maximo) {
        errorDeCampo(esArchivo,
                     std::string("'") + clave + "' debe estar entre " +
                         std::to_string(minimo) + " y " +
                         std::to_string(maximo));
    }
    return static_cast<int>(valor);
}

ConfigLlm aplicarCampos(const ConfigLlm& base, const nlohmann::json& campos,
                        bool esArchivo) {
    if (!campos.is_object()) {
        errorDeCampo(esArchivo, "la configuración debe ser un objeto JSON");
    }

    ConfigLlm resultado = base;

    if (tieneValor(campos, "mode")) {
        const std::string texto = textoDeCampo(campos, "mode", esArchivo);
        const auto modo = modoDesdeTexto(texto);
        if (!modo) {
            errorDeCampo(esArchivo, "'mode' debe ser LOCAL o REMOTE");
        }
        resultado.modo = *modo;
    }
    if (tieneValor(campos, "baseUrl")) {
        resultado.baseUrl = textoDeCampo(campos, "baseUrl", esArchivo);
    }
    if (tieneValor(campos, "apiKey")) {
        resultado.apiKey = textoDeCampo(campos, "apiKey", esArchivo);
    }
    if (tieneValor(campos, "modelName")) {
        resultado.modelName = textoDeCampo(campos, "modelName", esArchivo);
    }
    if (tieneValor(campos, "temperature")) {
        const auto& valor = campos.at("temperature");
        if (!valor.is_number()) {
            errorDeCampo(esArchivo, "'temperature' debe ser un número");
        }
        resultado.temperatura = valor.get<double>();
    }
    if (tieneValor(campos, "timeoutMs")) {
        resultado.timeoutTotalMs = enteroAcotado(
            campos, "timeoutMs", esArchivo, 1, kTimeoutTotalMaxMs);
    }
    if (tieneValor(campos, "connectTimeoutMs")) {
        resultado.timeoutConexionMs = enteroAcotado(
            campos, "connectTimeoutMs", esArchivo, 1, kTimeoutConexionMaxMs);
    }
    if (tieneValor(campos, "maxRetries")) {
        resultado.maxReintentos =
            enteroAcotado(campos, "maxRetries", esArchivo, 0, kReintentosMax);
    }
    if (tieneValor(campos, "retryBaseMs")) {
        resultado.esperaBaseMs = enteroAcotado(
            campos, "retryBaseMs", esArchivo, 0, kEsperaBaseMaxMs);
    }

    return resultado;
}

bool empiezaCon(const std::string& texto, const std::string& prefijo) {
    return texto.size() >= prefijo.size() &&
           texto.compare(0, prefijo.size(), prefijo) == 0;
}

std::string enMinusculas(std::string texto) {
    std::transform(texto.begin(), texto.end(), texto.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return texto;
}

}  // namespace

std::string textoDeModo(ModoLlm modo) {
    switch (modo) {
        case ModoLlm::LOCAL:
            return "LOCAL";
        case ModoLlm::REMOTE:
            return "REMOTE";
    }
    throw std::logic_error("ModoLlm sin texto de protocolo");
}

std::optional<ModoLlm> modoDesdeTexto(const std::string& texto) {
    if (texto == "LOCAL") {
        return ModoLlm::LOCAL;
    }
    if (texto == "REMOTE") {
        return ModoLlm::REMOTE;
    }
    return std::nullopt;
}

ConfigLlm ConfigLlm::conOverride(const nlohmann::json& override) const {
    return aplicarCampos(*this, override, false);
}

void ConfigLlm::validar() const {
    std::vector<std::string> problemas;

    switch (modo) {
        case ModoLlm::LOCAL:
            // Los requisitos de descarga y servidor local llegan con ese modo.
            break;
        case ModoLlm::REMOTE: {
            if (baseUrl.empty()) {
                problemas.push_back(
                    "baseUrl es obligatorio en modo REMOTE");
            } else if (!empiezaCon(baseUrl, "http://") &&
                       !empiezaCon(baseUrl, "https://")) {
                problemas.push_back(
                    "baseUrl debe empezar con http:// o https://");
            } else if (!esDestinoLocal(baseUrl) && apiKey.empty()) {
                problemas.push_back(
                    "apiKey es obligatoria para destinos remotos (solo puede "
                    "faltar en localhost)");
            }
            if (modelName.empty()) {
                problemas.push_back(
                    "modelName es obligatorio en modo REMOTE");
            }
            break;
        }
    }

    if (temperatura < 0.0 || temperatura > 2.0) {
        problemas.push_back("temperature debe estar entre 0 y 2");
    }
    if (timeoutTotalMs < 1 || timeoutTotalMs > kTimeoutTotalMaxMs) {
        problemas.push_back(
            "timeoutMs debe estar entre 1 y " +
            std::to_string(kTimeoutTotalMaxMs));
    }
    if (timeoutConexionMs < 1 || timeoutConexionMs > kTimeoutConexionMaxMs) {
        problemas.push_back(
            "connectTimeoutMs debe estar entre 1 y " +
            std::to_string(kTimeoutConexionMaxMs));
    }
    if (maxReintentos < 0 || maxReintentos > kReintentosMax) {
        problemas.push_back("maxRetries debe estar entre 0 y " +
                            std::to_string(kReintentosMax));
    }
    if (esperaBaseMs < 0 || esperaBaseMs > kEsperaBaseMaxMs) {
        problemas.push_back("retryBaseMs debe estar entre 0 y " +
                            std::to_string(kEsperaBaseMaxMs));
    }

    if (!problemas.empty()) {
        throw ErrorConfig::conProblemas(problemas);
    }
}

ConfigLlm cargarConfigLlm(const std::filesystem::path& casa) {
    const std::filesystem::path archivo = casa / "config.json";

    std::error_code error;
    const bool existe = std::filesystem::exists(archivo, error);
    if (error) {
        throw ErrorConfig("No se pudo consultar la configuración " +
                          archivo.string() + ": " + error.message());
    }
    if (!existe) {
        return ConfigLlm{};
    }

    std::ifstream entrada(archivo, std::ios::binary);
    if (!entrada) {
        throw ErrorConfig("No se pudo leer la configuración: " +
                          archivo.string());
    }

    try {
        const nlohmann::json campos = nlohmann::json::parse(entrada);
        if (!campos.is_object()) {
            throw ErrorConfig("La configuración debe ser un objeto JSON: " +
                              archivo.string());
        }
        return aplicarCampos(ConfigLlm{}, campos, true);
    } catch (const ErrorSilvy&) {
        throw;
    } catch (const nlohmann::json::exception& excepcion) {
        throw ErrorConfig("Configuración inválida en " + archivo.string() +
                          ": " + excepcion.what());
    }
}

bool esDestinoLocal(const std::string& baseUrl) {
    std::string host = baseUrl;
    const std::size_t esquema = host.find("://");
    if (esquema != std::string::npos) {
        host = host.substr(esquema + 3);
    }
    const std::size_t barra = host.find('/');
    if (barra != std::string::npos) {
        host = host.substr(0, barra);
    }
    const std::size_t arroba = host.rfind('@');
    if (arroba != std::string::npos) {
        host = host.substr(arroba + 1);
    }

    std::string nombre;
    if (!host.empty() && host.front() == '[') {
        const std::size_t cierre = host.find(']');
        if (cierre != std::string::npos) {
            nombre = host.substr(1, cierre - 1);
        }
    } else {
        const std::size_t dosPuntos = host.find(':');
        nombre = host.substr(0, dosPuntos);
    }

    nombre = enMinusculas(nombre);
    return nombre == "localhost" || nombre == "127.0.0.1" || nombre == "::1";
}

}  // namespace silvy
