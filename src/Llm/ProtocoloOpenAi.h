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
#ifndef SILVY_LLM_PROTOCOLO_OPENAI_H
#define SILVY_LLM_PROTOCOLO_OPENAI_H

#include "Config/ConfigLlm.h"
#include "Llm/LlmGateway.h"

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <string>

namespace silvy {

// Destino de una API compatible OpenAI: dónde conectar y con qué prefijo de
// ruta (típicamente "/v1").
struct DestinoApi final {
    std::string esquema;  // "http" o "https"
    std::string host;     // con corchetes si es una dirección IPv6
    int puerto = 80;
    std::string prefijo;  // "" o "/v1"
};

// Interpreta la baseUrl de la configuración. Lanza ErrorConfig si la URL no
// es http/https válida o si el puerto no es numérico o está fuera de rango.
DestinoApi interpretarBaseUrl(const std::string& baseUrl);

// Cuerpo de la petición en formato OpenAI: mensajes, herramientas,
// temperatura y streaming.
nlohmann::json armarCuerpoPeticion(const PeticionLlm& peticion,
                                   const ConfigLlm& config);

// Acumula los fragmentos de un stream SSE y arma el turno final: el
// contenido se concatena y las llamadas a herramienta se fusionan por
// índice (el id y el nombre llegan en el primer fragmento, los argumentos
// en varios).
class AcumuladorSse final {
public:
    // Aplica un fragmento del stream. Lanza ErrorSilvy si el proveedor
    // reporta un error dentro del stream.
    void aplicar(const nlohmann::json& fragmento);

    // Turno acumulado. Lanza ErrorSilvy si los argumentos de una llamada no
    // son JSON válido.
    RespuestaLlm turno() const;

private:
    struct LlamadaAcumulada final {
        std::string id;
        std::string nombre;
        std::string argumentos;
    };

    std::string contenido_;
    std::map<int, LlamadaAcumulada> llamadas_;
    std::optional<RazonFin> razon_;
};

// Aplica una línea de SSE ("data: {...}") al acumulador. Devuelve false si
// la línea no aporta datos (vacía, comentario, evento o el marcador [DONE]).
bool aplicarLineaSse(AcumuladorSse& acumulador, const std::string& linea);

// Turno a partir de una respuesta completa (servidor que ignora el stream).
RespuestaLlm turnoDesdeCuerpo(const nlohmann::json& cuerpo);

}  // namespace silvy

#endif  // SILVY_LLM_PROTOCOLO_OPENAI_H
