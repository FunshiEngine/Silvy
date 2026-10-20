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
#include "Llm/GatewayOpenAi.h"

#include "Errores/ErroresHttp.h"
#include "Llm/ProtocoloOpenAi.h"

#include <httplib.h>

#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace silvy {

namespace {

// Espera la cantidad de milisegundos indicada, cortando en seco si llega
// una cancelación: el backoff no puede ignorar al humano.
bool esperarCancelable(int milisegundos, const std::atomic<bool>& cancelado) {
    const auto fin = std::chrono::steady_clock::now() +
                     std::chrono::milliseconds(milisegundos);
    while (std::chrono::steady_clock::now() < fin) {
        if (cancelado.load()) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return !cancelado.load();
}

// Margen que se le da al cliente HTTP por encima del presupuesto total. Así
// es Silvy quien decide si el paso se agotó, y no el redondeo del sistema al
// fijar los tiempos de socket.
constexpr int kMargenTransporteMs = 250;

// Traduce un fallo de transporte. Si el presupuesto total ya se agotó, el
// error es un timeout de Silvy; si no, es un fallo de red (conexión
// rechazada, DNS, TLS, corte de lectura) y lo clasifica desdeRed.
ErrorSilvy desdeFalloDeTransporte(httplib::Error error, bool esLocal,
                                  bool presupuestoAgotado, int presupuestoMs) {
    if (presupuestoAgotado) {
        return ErrorTimeout("Se agotó el tiempo total del paso del LLM (" +
                            std::to_string(presupuestoMs) + " ms)");
    }
    switch (error) {
        case httplib::Error::SSLConnection:
        case httplib::Error::SSLLoadingCerts:
        case httplib::Error::SSLServerVerification:
        case httplib::Error::SSLServerHostnameVerification:
            return desdeRed("fallo de TLS: " + httplib::to_string(error), esLocal);
        default:
            return desdeRed(httplib::to_string(error), esLocal);
    }
}

}  // namespace

GatewayOpenAi::GatewayOpenAi(ConfigLlm config) : config_(std::move(config)) {}

void GatewayOpenAi::cancelar() {
    cancelado_.store(true);
}

RespuestaLlm GatewayOpenAi::paso(const PeticionLlm& peticion) {
    cancelado_.store(false);

    const DestinoApi destino = interpretarBaseUrl(config_.baseUrl);
    const std::string cuerpo = armarCuerpoPeticion(peticion, config_).dump();
    const std::string ruta = destino.prefijo + "/chat/completions";

    ErrorSilvy ultimoError(CodigoError::DESCONOCIDO, false,
                            "El paso del LLM no llegó a ejecutarse");
    for (int intento = 0; intento <= config_.maxReintentos; ++intento) {
        if (intento > 0) {
            const int espera =
                config_.esperaBaseMs * (1 << (intento - 1));
            if (!esperarCancelable(espera, cancelado_)) {
                throw ErrorCancelado(
                    "El paso del LLM fue cancelado durante la espera del "
                    "reintento");
            }
        }
        try {
            return intentar(destino, ruta, cuerpo);
        } catch (const ErrorSilvy& error) {
            ultimoError = error;
            if (!error.reintentable() || intento >= config_.maxReintentos) {
                throw;
            }
        }
    }
    throw ultimoError;
}

RespuestaLlm GatewayOpenAi::intentar(const DestinoApi& destino,
                                      const std::string& ruta,
                                      const std::string& cuerpo) {
    const bool esLocal = esDestinoLocal(config_.baseUrl);

    httplib::Client cliente(destino.esquema + "://" + destino.host + ":" +
                            std::to_string(destino.puerto));
    cliente.set_connection_timeout(std::chrono::milliseconds(
        config_.timeoutConexionMs));
    cliente.set_read_timeout(std::chrono::milliseconds(
        config_.timeoutTotalMs + kMargenTransporteMs));
    cliente.set_write_timeout(std::chrono::milliseconds(
        config_.timeoutTotalMs + kMargenTransporteMs));
    cliente.set_follow_location(true);

    httplib::Headers cabeceras;
    cabeceras.emplace("Content-Type", "application/json");
    cabeceras.emplace("Accept", "text/event-stream");
    if (!config_.apiKey.empty()) {
        cabeceras.emplace("Authorization", "Bearer " + config_.apiKey);
    }

    const auto inicio = std::chrono::steady_clock::now();
    AcumuladorSse acumulador;
    std::string buffer;
    bool vioSse = false;
    bool cancelado = false;
    bool expirado = false;
    std::optional<ErrorSilvy> errorParseo;

    const auto receptor = [&](const char* datos, std::size_t longitud) -> bool {
        if (cancelado_.load()) {
            cancelado = true;
            return false;
        }
        if (std::chrono::steady_clock::now() - inicio >
            std::chrono::milliseconds(config_.timeoutTotalMs)) {
            expirado = true;
            return false;
        }

        buffer.append(datos, longitud);
        std::size_t fin = 0;
        while ((fin = buffer.find('\n')) != std::string::npos) {
            const std::string linea = buffer.substr(0, fin);
            buffer.erase(0, fin + 1);
            try {
                if (linea.rfind("data:", 0) == 0) {
                    vioSse = true;
                }
                aplicarLineaSse(acumulador, linea);
            } catch (const ErrorSilvy& error) {
                errorParseo = error;
                return false;
            } catch (const nlohmann::json::exception&) {
                errorParseo = ErrorSilvy(
                    CodigoError::DESCONOCIDO, false,
                    "El proveedor devolvió un fragmento del stream que no es "
                    "JSON válido");
                return false;
            }
        }
        return true;
    };

    const auto resultado =
        cliente.Post(ruta, cabeceras, cuerpo, "application/json", receptor);

    if (cancelado) {
        throw ErrorCancelado("El paso del LLM fue cancelado");
    }
    if (expirado) {
        throw ErrorTimeout("Se agotó el tiempo total del paso del LLM (" +
                           std::to_string(config_.timeoutTotalMs) + " ms)");
    }
    if (errorParseo) {
        throw *errorParseo;
    }

    if (!resultado) {
        const auto transcurrido = std::chrono::steady_clock::now() - inicio;
        const bool presupuestoAgotado =
            transcurrido >= std::chrono::milliseconds(config_.timeoutTotalMs);
        throw desdeFalloDeTransporte(resultado.error(), esLocal,
                                     presupuestoAgotado, config_.timeoutTotalMs);
    }
    if (resultado->status != 200) {
        throw desdeHttp(resultado->status, resultado->body, esLocal);
    }

    if (vioSse) {
        return acumulador.turno();
    }

    // El servidor ignoró el streaming y devolvió el turno completo.
    try {
        return turnoDesdeCuerpo(nlohmann::json::parse(resultado->body));
    } catch (const nlohmann::json::exception& error) {
        throw ErrorSilvy(CodigoError::DESCONOCIDO, false,
                         std::string("La respuesta del LLM no es JSON válido: ") +
                             error.what());
    }
}

}  // namespace silvy
