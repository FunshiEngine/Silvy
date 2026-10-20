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
#include "Llm/ProtocoloOpenAi.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <utility>

namespace silvy {

namespace {

std::string enMinusculas(std::string texto) {
    std::transform(texto.begin(), texto.end(), texto.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return texto;
}

void quitarEspaciosFinales(std::string& texto) {
    while (!texto.empty() &&
           (texto.back() == '\r' || texto.back() == ' ' ||
            texto.back() == '\t')) {
        texto.pop_back();
    }
}

// Los argumentos de una llamada llegan como texto JSON; un texto vacío
// significa "sin argumentos".
nlohmann::json argumentosDesdeTexto(const std::string& texto,
                                    const std::string& nombre) {
    if (texto.empty()) {
        return nlohmann::json::object();
    }
    try {
        return nlohmann::json::parse(texto);
    } catch (const nlohmann::json::exception&) {
        throw ErrorSilvy(
            CodigoError::DESCONOCIDO, false,
            "El LLM devolvió argumentos que no son JSON para la herramienta " +
                nombre + ": " + texto);
    }
}

nlohmann::json mensajeAJsonOpenAi(const Mensaje& mensaje) {
    nlohmann::json item;
    item["role"] = textoDeRol(mensaje.rol);
    item["content"] = mensaje.contenido;

    if (mensaje.rol == RolMensaje::ASISTENTE && !mensaje.llamadas.empty()) {
        nlohmann::json llamadas = nlohmann::json::array();
        for (const LlamadaHerramienta& llamada : mensaje.llamadas) {
            nlohmann::json entrada;
            entrada["id"] = llamada.id;
            entrada["type"] = "function";
            entrada["function"]["name"] = llamada.nombre;
            entrada["function"]["arguments"] = llamada.argumentos.dump();
            llamadas.push_back(std::move(entrada));
        }
        item["tool_calls"] = std::move(llamadas);
    }
    if (mensaje.rol == RolMensaje::HERRAMIENTA) {
        item["tool_call_id"] = mensaje.idLlamada;
    }
    return item;
}

nlohmann::json herramientaAJsonOpenAi(const Herramienta& herramienta) {
    nlohmann::json item;
    item["type"] = "function";
    item["function"]["name"] = herramienta.nombre;
    item["function"]["description"] = herramienta.descripcion;
    item["function"]["parameters"] = herramienta.parametros;
    return item;
}

}  // namespace

DestinoApi interpretarBaseUrl(const std::string& baseUrl) {
    DestinoApi destino;

    const std::size_t esquema = baseUrl.find("://");
    if (esquema == std::string::npos) {
        throw ErrorConfig(
            "La baseUrl debe indicar el esquema http:// o https://: " + baseUrl);
    }
    destino.esquema = enMinusculas(baseUrl.substr(0, esquema));
    if (destino.esquema != "http" && destino.esquema != "https") {
        throw ErrorConfig(
            "La baseUrl debe usar http:// o https://: " + baseUrl);
    }

    std::string resto = baseUrl.substr(esquema + 3);
    const std::size_t barra = resto.find('/');
    std::string autoridad = resto.substr(0, barra);
    destino.prefijo = barra == std::string::npos ? std::string() : resto.substr(barra);

    const std::size_t interrogacion = destino.prefijo.find('?');
    if (interrogacion != std::string::npos) {
        destino.prefijo.erase(interrogacion);
    }
    while (!destino.prefijo.empty() && destino.prefijo.back() == '/') {
        destino.prefijo.pop_back();
    }

    const std::size_t arroba = autoridad.rfind('@');
    if (arroba != std::string::npos) {
        autoridad = autoridad.substr(arroba + 1);
    }

    std::string puertoTexto;
    if (!autoridad.empty() && autoridad.front() == '[') {
        const std::size_t cierre = autoridad.find(']');
        if (cierre == std::string::npos) {
            throw ErrorConfig("La baseUrl tiene un host IPv6 incompleto: " +
                              baseUrl);
        }
        const std::string despues = autoridad.substr(cierre + 1);
        if (!despues.empty()) {
            if (despues.front() != ':') {
                throw ErrorConfig("La baseUrl tiene un puerto mal formado: " +
                                  baseUrl);
            }
            puertoTexto = despues.substr(1);
        }
    } else {
        const std::size_t dosPuntos = autoridad.rfind(':');
        if (dosPuntos != std::string::npos) {
            puertoTexto = autoridad.substr(dosPuntos + 1);
            autoridad = autoridad.substr(0, dosPuntos);
        }
    }

    if (autoridad.empty()) {
        throw ErrorConfig("La baseUrl no tiene host: " + baseUrl);
    }
    destino.host = autoridad;
    destino.puerto = destino.esquema == "https" ? 443 : 80;

    if (!puertoTexto.empty()) {
        int puerto = 0;
        try {
            puerto = std::stoi(puertoTexto);
        } catch (const std::exception&) {
            throw ErrorConfig("El puerto de la baseUrl no es numérico: " +
                              baseUrl);
        }
        if (puerto <= 0 || puerto > 65535) {
            throw ErrorConfig("El puerto de la baseUrl está fuera de rango: " +
                              baseUrl);
        }
        destino.puerto = puerto;
    }

    return destino;
}

nlohmann::json armarCuerpoPeticion(const PeticionLlm& peticion,
                                   const ConfigLlm& config) {
    nlohmann::json cuerpo;
    cuerpo["model"] = config.modelName;
    cuerpo["temperature"] = config.temperatura;
    cuerpo["stream"] = true;

    nlohmann::json mensajes = nlohmann::json::array();
    for (const Mensaje& mensaje : peticion.mensajes) {
        mensajes.push_back(mensajeAJsonOpenAi(mensaje));
    }
    cuerpo["messages"] = std::move(mensajes);

    if (!peticion.herramientas.empty()) {
        nlohmann::json herramientas = nlohmann::json::array();
        for (const Herramienta& herramienta : peticion.herramientas) {
            herramientas.push_back(herramientaAJsonOpenAi(herramienta));
        }
        cuerpo["tools"] = std::move(herramientas);
    }

    return cuerpo;
}

void AcumuladorSse::aplicar(const nlohmann::json& fragmento) {
    if (!fragmento.is_object()) {
        return;
    }

    const auto error = fragmento.find("error");
    if (error != fragmento.end() && !error->is_null()) {
        std::string mensaje = "El proveedor informó un error en el stream";
        if (error->is_object()) {
            const auto detalle = error->find("message");
            if (detalle != error->end() && detalle->is_string()) {
                mensaje += ": " + detalle->get<std::string>();
            }
        }
        throw ErrorSilvy(CodigoError::DESCONOCIDO, false, mensaje);
    }

    const auto choices = fragmento.find("choices");
    if (choices == fragmento.end() || !choices->is_array() || choices->empty()) {
        return;
    }
    const auto& eleccion = choices->at(0);
    if (!eleccion.is_object()) {
        return;
    }

    const auto delta = eleccion.find("delta");
    if (delta != eleccion.end() && delta->is_object()) {
        const auto contenido = delta->find("content");
        if (contenido != delta->end() && contenido->is_string()) {
            contenido_ += contenido->get<std::string>();
        }

        const auto llamadas = delta->find("tool_calls");
        if (llamadas != delta->end() && llamadas->is_array()) {
            for (const auto& llamada : *llamadas) {
                if (!llamada.is_object()) {
                    continue;
                }
                const int indice = llamada.value("index", 0);
                LlamadaAcumulada& acumulada = llamadas_[indice];

                const auto id = llamada.find("id");
                if (id != llamada.end() && id->is_string()) {
                    acumulada.id = id->get<std::string>();
                }
                const auto funcion = llamada.find("function");
                if (funcion == llamada.end() || !funcion->is_object()) {
                    continue;
                }
                const auto nombre = funcion->find("name");
                if (nombre != funcion->end() && nombre->is_string()) {
                    acumulada.nombre = nombre->get<std::string>();
                }
                const auto argumentos = funcion->find("arguments");
                if (argumentos != funcion->end() && argumentos->is_string()) {
                    acumulada.argumentos += argumentos->get<std::string>();
                }
            }
        }
    }

    const auto razon = eleccion.find("finish_reason");
    if (razon != eleccion.end() && razon->is_string()) {
        razon_ = razonFinDesdeTexto(razon->get<std::string>());
    }
}

RespuestaLlm AcumuladorSse::turno() const {
    RespuestaLlm turno;
    turno.contenido = contenido_;

    for (const auto& par : llamadas_) {
        LlamadaHerramienta llamada;
        llamada.id = par.second.id;
        llamada.nombre = par.second.nombre;
        llamada.argumentos =
            argumentosDesdeTexto(par.second.argumentos, par.second.nombre);
        turno.llamadas.push_back(std::move(llamada));
    }

    if (razon_) {
        turno.razonFin = *razon_;
    } else {
        turno.razonFin = turno.llamadas.empty() ? RazonFin::DETENCION
                                                 : RazonFin::LLAMADAS_HERRAMIENTA;
    }
    return turno;
}

bool aplicarLineaSse(AcumuladorSse& acumulador, const std::string& linea) {
    std::string limpia = linea;
    quitarEspaciosFinales(limpia);
    if (limpia.empty()) {
        return false;
    }

    std::string datos;
    if (limpia.rfind("data:", 0) == 0) {
        datos = limpia.substr(5);
        std::size_t inicio = 0;
        while (inicio < datos.size() && datos[inicio] == ' ') {
            ++inicio;
        }
        datos = datos.substr(inicio);
    } else if (limpia.front() == '{') {
        // Algunos servidores envían el JSON del fragmento sin el prefijo.
        datos = limpia;
    } else {
        // Comentarios (": ..."), "event:", "id:" y "retry:" no aportan datos.
        return false;
    }

    if (datos.empty() || datos == "[DONE]") {
        return false;
    }

    acumulador.aplicar(nlohmann::json::parse(datos));
    return true;
}

RespuestaLlm turnoDesdeCuerpo(const nlohmann::json& cuerpo) {
    RespuestaLlm turno;

    const auto choices = cuerpo.find("choices");
    if (choices == cuerpo.end() || !choices->is_array() || choices->empty()) {
        throw ErrorSilvy(CodigoError::DESCONOCIDO, false,
                         "La respuesta del LLM no trae choices");
    }
    const auto& eleccion = choices->at(0);
    if (!eleccion.is_object()) {
        throw ErrorSilvy(CodigoError::DESCONOCIDO, false,
                         "La respuesta del LLM trae un choice inválido");
    }

    const auto mensaje = eleccion.find("message");
    if (mensaje != eleccion.end() && mensaje->is_object()) {
        const auto contenido = mensaje->find("content");
        if (contenido != mensaje->end() && contenido->is_string()) {
            turno.contenido = contenido->get<std::string>();
        }

        const auto llamadas = mensaje->find("tool_calls");
        if (llamadas != mensaje->end() && llamadas->is_array()) {
            for (const auto& llamada : *llamadas) {
                if (!llamada.is_object()) {
                    continue;
                }
                LlamadaHerramienta salida;
                const auto id = llamada.find("id");
                if (id != llamada.end() && id->is_string()) {
                    salida.id = id->get<std::string>();
                }
                const auto funcion = llamada.find("function");
                if (funcion != llamada.end() && funcion->is_object()) {
                    const auto nombre = funcion->find("name");
                    if (nombre != funcion->end() && nombre->is_string()) {
                        salida.nombre = nombre->get<std::string>();
                    }
                    const auto argumentos = funcion->find("arguments");
                    if (argumentos != funcion->end()) {
                        if (argumentos->is_string()) {
                            salida.argumentos = argumentosDesdeTexto(
                                argumentos->get<std::string>(), salida.nombre);
                        } else if (argumentos->is_object()) {
                            salida.argumentos = *argumentos;
                        }
                    }
                }
                turno.llamadas.push_back(std::move(salida));
            }
        }
    }

    const auto razon = eleccion.find("finish_reason");
    if (razon != eleccion.end() && razon->is_string()) {
        turno.razonFin = razonFinDesdeTexto(razon->get<std::string>());
    } else {
        turno.razonFin = turno.llamadas.empty() ? RazonFin::DETENCION
                                                 : RazonFin::LLAMADAS_HERRAMIENTA;
    }

    return turno;
}

}  // namespace silvy
