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
// Prueba headless de Silvy: núcleo, errores tipados y registro persistente.
// Patrón propio (este repo no tiene submódulo del motor): macro CHECK y
// CarpetaPrueba RAII para temporales. Cierra con OK/FALLOS y exit 0/1.

#include "Config/ConfigSilvy.h"
#include "Concurrencia/HiloTrabajador.h"
#include "Errores/Errores.h"
#include "Errores/RegistroErrores.h"
#include "GatewayFalso.h"
#include "Llm/PeticionLlm.h"
#include "Nucleo/CatalogoComandos.h"
#include "Nucleo/DependenciasNucleo.h"
#include "Nucleo/NucleoSilvy.h"
#include "Nucleo/Protocolo.h"
#include "Nucleo/Respuesta.h"
#include "Nucleo/Solicitud.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

namespace fs = std::filesystem;

namespace {

int total = 0;
int fallos = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        ++total;                                                             \
        if (!(cond)) {                                                       \
            ++fallos;                                                        \
            std::cout << "FALLO: " << msg << " (linea " << __LINE__ << ")\n"; \
        }                                                                    \
    } while (0)

// Carpeta temporal que se borra sola al salir (RAII).
class CarpetaPrueba final {
public:
    explicit CarpetaPrueba(const std::string& nombre) {
        static std::atomic<int> contador{0};
        const auto marca =
            std::chrono::steady_clock::now().time_since_epoch().count();
        ruta_ = fs::temp_directory_path() /
                ("silvy-tests-" + nombre + "-" + std::to_string(marca) + "-" +
                 std::to_string(contador.fetch_add(1)));
        fs::create_directories(ruta_);
    }

    ~CarpetaPrueba() {
        std::error_code error;
        fs::remove_all(ruta_, error);
    }

    const fs::path& ruta() const { return ruta_; }

private:
    fs::path ruta_;
};

std::vector<std::string> leerLineas(const fs::path& archivo) {
    std::vector<std::string> lineas;
    std::ifstream entrada(archivo);
    std::string linea;
    while (std::getline(entrada, linea)) {
        if (!linea.empty()) {
            lineas.push_back(linea);
        }
    }
    return lineas;
}

std::size_t contarArchivosRotados(const fs::path& directorio) {
    std::size_t cantidad = 0;
    std::error_code error;
    for (const auto& entrada : fs::directory_iterator(directorio, error)) {
        const std::string nombre = entrada.path().filename().string();
        if (nombre.rfind("errores-", 0) == 0 && nombre.size() > 8 &&
            nombre.compare(nombre.size() - 6, 6, ".jsonl") == 0) {
            ++cantidad;
        }
    }
    return cantidad;
}

// Ejecuta la acción y dice si lanzó un ErrorSilvy con el código indicado.
bool lanzaConCodigo(const std::function<void()>& accion,
                    silvy::CodigoError codigo) {
    try {
        accion();
    } catch (const silvy::ErrorSilvy& error) {
        return error.codigo() == codigo;
    } catch (...) {
        return false;
    }
    return false;
}

void escribirArchivo(const fs::path& ruta, const std::string& contenido) {
    std::ofstream salida(ruta, std::ios::binary);
    salida << contenido;
}

nlohmann::json cargarFixture(const std::string& nombre) {
    std::ifstream entrada(fs::path(SILVY_DIR_FIXTURES) / nombre,
                          std::ios::binary);
    if (!entrada) {
        return nullptr;
    }
    return nlohmann::json::parse(entrada);
}

silvy::ConfigRegistroErrores configuracionDe(const fs::path& casa) {
    silvy::ConfigRegistroErrores config;
    config.directorio = casa / "errores";
    return config;
}

// Entorno de núcleo con casa temporal, binario simulado y registro aislado.
struct EntornoNucleo final {
    explicit EntornoNucleo(const std::string& nombre)
        : carpeta(nombre),
          casa(carpeta.ruta() / "home"),
          bin(carpeta.ruta() / "bin"),
          registro(configuracionDe(casa)) {
        fs::create_directories(casa);
        fs::create_directories(bin / "config");
    }

    CarpetaPrueba carpeta;
    fs::path casa;
    fs::path bin;
    silvy::RegistroErrores registro;
};

std::vector<std::string> lineasDeErrores(const EntornoNucleo& entorno) {
    return leerLineas(entorno.casa / "errores" / "errores.jsonl");
}

struct SalidaProceso final {
    int codigo = -1;
    std::string salida;
};

// Ejecuta un comando de shell y captura su stdout y su código de salida.
SalidaProceso ejecutarProceso(const std::string& comando) {
#ifdef _WIN32
    FILE* tuberia = _popen(comando.c_str(), "r");
#else
    FILE* tuberia = popen(comando.c_str(), "r");
#endif
    if (tuberia == nullptr) {
        return {};
    }
    std::array<char, 4096> buffer{};
    std::string salida;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), tuberia) !=
           nullptr) {
        salida += buffer.data();
    }
#ifdef _WIN32
    const int codigo = _pclose(tuberia);
#else
    const int estado = pclose(tuberia);
    const int codigo = WIFEXITED(estado) ? WEXITSTATUS(estado) : -1;
#endif
    return {codigo, salida};
}

std::string citar(const fs::path& ruta) {
    return "\"" + ruta.string() + "\"";
}

// Gateway que lanza una excepción ajena al dominio para probar la
// normalización del borde.
class GatewayQueRompe final : public silvy::LlmGateway {
public:
    silvy::RespuestaLlm paso(const silvy::PeticionLlm&) override {
        throw std::runtime_error("socket roto");
    }
};

void probarErrores() {
    for (const silvy::CodigoError codigo : silvy::todosLosCodigos()) {
        const std::string texto = silvy::textoDeCodigo(codigo);
        CHECK(!texto.empty(), "todo codigo tiene texto");
        const auto vuelta = silvy::codigoDesdeTexto(texto);
        CHECK(vuelta.has_value() && *vuelta == codigo,
              "texto y codigo hacen roundtrip");
    }
    CHECK(!silvy::codigoDesdeTexto("NO_EXISTE").has_value(),
          "un codigo desconocido no tiene valor");

    const silvy::ErrorSilvy base(silvy::CodigoError::SOLICITUD_INVALIDA, false,
                                 "solicitud rota");
    CHECK(base.codigo() == silvy::CodigoError::SOLICITUD_INVALIDA,
          "codigo del error");
    CHECK(!base.reintentable(), "solicitud invalida no es reintentable");
    CHECK(std::string(base.what()) == "solicitud rota", "mensaje del error");
    CHECK(!base.causa().has_value(), "sin causa por defecto");

    CHECK(silvy::ErrorTimeout("t").reintentable(), "timeout es reintentable");
    CHECK(!silvy::ErrorCancelado("c").reintentable(),
          "cancelado no es reintentable");
    CHECK(silvy::ErrorServidorCaido("s").reintentable(),
          "servidor caido es reintentable");

    const auto config =
        silvy::ErrorConfig::conProblemas({"falta baseUrl", "apiKey vacía"});
    const std::string mensaje = config.what();
    CHECK(config.codigo() == silvy::CodigoError::NO_CONFIGURADO,
          "la config invalida es NO_CONFIGURADO");
    CHECK(!config.reintentable(), "la config invalida no es reintentable");
    CHECK(mensaje.rfind("Configuración inválida:", 0) == 0,
          "el error de config tiene encabezado");
    CHECK(mensaje.find("falta baseUrl") != std::string::npos,
          "el error de config lista el primer problema");
    CHECK(mensaje.find("apiKey vacía") != std::string::npos,
          "el error de config lista el segundo problema");

    try {
        throw silvy::ErrorTimeout("tarde");
    } catch (const std::exception& error) {
        const silvy::ErrorSilvy envuelto = silvy::envolver(error);
        CHECK(envuelto.codigo() == silvy::CodigoError::TIMEOUT,
              "envolver conserva el tipo");
        CHECK(envuelto.reintentable(), "envolver conserva reintentable");
    }

    try {
        throw std::runtime_error("crudo");
    } catch (const std::exception& error) {
        const silvy::ErrorSilvy envuelto = silvy::envolver(error);
        CHECK(envuelto.codigo() == silvy::CodigoError::DESCONOCIDO,
              "envolver normaliza desconocidos");
        CHECK(envuelto.causa().has_value(), "envolver guarda la causa");
        CHECK(envuelto.causa()->mensaje == "crudo", "mensaje de la causa");
    }
}

void probarHiloTrabajador() {
    silvy::HiloTrabajador hilo("prueba");
    std::atomic<int> contador{0};
    for (int i = 0; i < 10; ++i) {
        CHECK(hilo.encolar([&contador] { contador.fetch_add(1); }),
              "encolar un trabajo");
    }
    hilo.detener();
    CHECK(contador.load() == 10, "detener vacia la cola");
    CHECK(!hilo.encolar([] {}), "encolar tras detener devuelve false");
}

void probarRegistro() {
    CarpetaPrueba casa("registro");
    silvy::ConfigRegistroErrores config;
    config.directorio = casa.ruta() / "errores";
    CHECK(config.validar().empty(), "config por defecto valida");

    silvy::RegistroErrores registro(config);

    silvy::ContextoError contexto;
    contexto.comando = "agent.step";
    contexto.modo = "LOCAL";
    contexto.modelo = "phi-4-mini";
    contexto.detalle = {{"exitCode", 1}};

    const auto primero = registro.registrar(
        silvy::ErrorServidorCaido("servidor caido"), contexto);
    const auto segundo = registro.registrar(
        silvy::ErrorSilvy(silvy::CodigoError::SOLICITUD_INVALIDA, false,
                          "solicitud invalida"),
        contexto);

    CHECK(primero["id"] != segundo["id"], "cada registro tiene id propio");
    CHECK(primero["codigo"] == "SERVIDOR_CAIDO", "codigo en el registro");
    CHECK(primero["reintentable"] == true, "reintentable en el registro");
    CHECK(primero["detalle"]["exitCode"] == 1, "detalle en el registro");
    CHECK(primero["ts"].get<std::string>().find('T') != std::string::npos,
          "marca de tiempo ISO en el registro");

    registro.vaciar();
    const auto lineas = leerLineas(config.directorio / "errores.jsonl");
    CHECK(lineas.size() == 2, "dos lineas persistidas");
    if (lineas.size() == 2) {
        const auto leido = nlohmann::json::parse(lineas[0]);
        CHECK(leido["codigo"] == "SERVIDOR_CAIDO", "primera linea persistida");
        CHECK(leido["comando"] == "agent.step", "contexto persistido");
        CHECK(leido["detalle"]["exitCode"] == 1, "detalle persistido");
    }

    const auto listado = registro.listar(10);
    CHECK(listado.size() == 2, "listar devuelve todo");
    if (listado.size() == 2) {
        CHECK(listado[0]["id"] == segundo["id"],
              "listar devuelve lo mas reciente primero");
    }

    const auto filtrado =
        registro.listar(10, silvy::CodigoError::SERVIDOR_CAIDO);
    CHECK(filtrado.size() == 1, "el filtro por codigo funciona");

    const auto encontrado =
        registro.obtener(primero["id"].get<std::string>());
    CHECK(encontrado.has_value(), "obtener por id encuentra");
    if (encontrado) {
        CHECK((*encontrado)["codigo"] == "SERVIDOR_CAIDO",
              "obtener devuelve el registro correcto");
    }
    CHECK(!registro.obtener("err-inexistente").has_value(),
          "obtener un id inexistente no encuentra");

    silvy::CausaError causa;
    causa.codigo = silvy::CodigoError::DESCONOCIDO;
    causa.mensaje = "conexion rechazada";
    const auto conCausa = registro.registrar(
        silvy::ErrorSilvy(silvy::CodigoError::SERVIDOR_CAIDO, true,
                          "sin servidor", causa),
        contexto);
    CHECK(conCausa.contains("causa"), "la causa viaja en el registro");
    CHECK(conCausa["causa"]["mensaje"] == "conexion rechazada",
          "el mensaje de la causa se conserva");
}

void probarRotacion() {
    CarpetaPrueba casa("rotacion");
    silvy::ConfigRegistroErrores config;
    config.directorio = casa.ruta() / "errores";
    config.tamMaximoKb = 1;
    config.maximoArchivos = 2;

    silvy::RegistroErrores registro(config);
    silvy::ContextoError contexto;
    contexto.comando = "prueba";
    const std::string relleno(700, 'x');
    for (int i = 0; i < 8; ++i) {
        registro.registrar(
            silvy::ErrorSilvy(silvy::CodigoError::DESCONOCIDO, false, relleno),
            contexto);
    }
    registro.vaciar();

    const std::size_t rotados = contarArchivosRotados(config.directorio);
    CHECK(rotados >= 1, "la rotacion crea archivos");
    CHECK(rotados <= 2, "la retencion limita los archivos rotados");
    CHECK(!registro.listar(100).empty(), "listar incluye los rotados");
}

void probarRegistroDegradado() {
    CarpetaPrueba casa("degradado");
    const fs::path archivo = casa.ruta() / "bloqueado";
    {
        std::ofstream salida(archivo);
        salida << "x";
    }
    silvy::ConfigRegistroErrores config;
    config.directorio = archivo / "errores";

    // Construir y escribir sobre una ruta inválida no debe lanzar ni romper.
    silvy::RegistroErrores registro(config);
    silvy::ContextoError contexto;
    contexto.comando = "prueba";
    registro.registrar(silvy::ErrorTimeout("sin disco"), contexto);
    registro.vaciar();
    CHECK(registro.listar(10).empty(), "sin persistencia no hay registros");
}

void probarValidacionConfig() {
    silvy::ConfigRegistroErrores config;
    config.tamMaximoKb = 0;
    config.maximoArchivos = 0;
    const auto problemas = config.validar();
    CHECK(problemas.size() == 3, "validar lista todos los problemas juntos");
}

void probarProtocolo() {
    CHECK(silvy::textoDeRol(silvy::RolMensaje::SISTEMA) == "system",
          "rol system");
    CHECK(silvy::textoDeRol(silvy::RolMensaje::USUARIO) == "user", "rol user");
    CHECK(silvy::textoDeRol(silvy::RolMensaje::ASISTENTE) == "assistant",
          "rol assistant");
    CHECK(silvy::textoDeRol(silvy::RolMensaje::HERRAMIENTA) == "tool",
          "rol tool");
    CHECK(!silvy::rolDesdeTexto("otro").has_value(), "rol desconocido sin valor");

    CHECK(silvy::textoDeRazonFin(silvy::RazonFin::DETENCION) == "stop",
          "razon stop");
    CHECK(silvy::textoDeRazonFin(silvy::RazonFin::LLAMADAS_HERRAMIENTA) ==
              "tool_calls",
          "razon tool_calls");
    CHECK(silvy::razonFinDesdeTexto("stop") == silvy::RazonFin::DETENCION,
          "leer razon stop");
    CHECK(silvy::razonFinDesdeTexto("tool_calls") ==
              silvy::RazonFin::LLAMADAS_HERRAMIENTA,
          "leer razon tool_calls");
    CHECK(silvy::razonFinDesdeTexto("cualquiera") ==
              silvy::RazonFin::DESCONOCIDA,
          "razon desconocida no rompe");

    nlohmann::json llamadaJson;
    llamadaJson["id"] = "call_1";
    llamadaJson["name"] = "script.compile";
    llamadaJson["arguments"] = {{"projectId", "Demo"}};

    nlohmann::json mensajeJson;
    mensajeJson["role"] = "assistant";
    mensajeJson["content"] = "Voy a compilar.";
    mensajeJson["toolCalls"] = nlohmann::json::array({llamadaJson});

    const silvy::Mensaje mensaje = silvy::mensajeDesdeJson(mensajeJson);
    CHECK(mensaje.rol == silvy::RolMensaje::ASISTENTE, "rol del mensaje");
    CHECK(mensaje.contenido == "Voy a compilar.", "contenido del mensaje");
    CHECK(mensaje.llamadas.size() == 1, "llamada del mensaje");
    if (!mensaje.llamadas.empty()) {
        CHECK(mensaje.llamadas[0].nombre == "script.compile",
              "nombre de la llamada");
        CHECK(mensaje.llamadas[0].argumentos["projectId"] == "Demo",
              "argumentos de la llamada");
    }
    const auto mensajeVuelta = silvy::mensajeAJson(mensaje);
    CHECK(mensajeVuelta["role"] == "assistant", "serializar rol");
    CHECK(mensajeVuelta["toolCalls"][0]["name"] == "script.compile",
          "serializar llamada");

    nlohmann::json herramientaJson;
    herramientaJson["name"] = "script.compile";
    herramientaJson["description"] = "Compila los scripts.";
    herramientaJson["parameters"] = {{"type", "object"}};
    const silvy::Herramienta herramienta =
        silvy::herramientaDesdeJson(herramientaJson);
    CHECK(herramienta.nombre == "script.compile", "nombre de herramienta");
    CHECK(herramienta.parametros["type"] == "object",
          "parametros de herramienta");
    CHECK(silvy::herramientaAJson(herramienta)["name"] == "script.compile",
          "serializar herramienta");

    CHECK(lanzaConCodigo(
              [] {
                  silvy::mensajeDesdeJson({{"role", "marciano"}});
              },
              silvy::CodigoError::SOLICITUD_INVALIDA),
          "rol invalido da SOLICITUD_INVALIDA");
    CHECK(lanzaConCodigo(
              [] {
                  silvy::mensajeDesdeJson(
                      {{"role", "tool"}, {"content", "resultado"}});
              },
              silvy::CodigoError::SOLICITUD_INVALIDA),
          "tool sin toolCallId da SOLICITUD_INVALIDA");
    CHECK(lanzaConCodigo(
              [] {
                  silvy::mensajeDesdeJson(
                      {{"role", "assistant"}, {"toolCalls", "no-array"}});
              },
              silvy::CodigoError::SOLICITUD_INVALIDA),
          "toolCalls no array da SOLICITUD_INVALIDA");
    CHECK(lanzaConCodigo(
              [] {
                  silvy::mensajeDesdeJson(
                      {{"role", "assistant"},
                       {"toolCalls",
                        nlohmann::json::array(
                            {{{"id", "c1"},
                              {"name", "x"},
                              {"arguments", "no-objeto"}}})}});
              },
              silvy::CodigoError::SOLICITUD_INVALIDA),
          "arguments no objeto da SOLICITUD_INVALIDA");

    // Los campos desconocidos se toleran (upgrades del protocolo).
    const auto tolerante =
        silvy::mensajeDesdeJson({{"role", "user"},
                                 {"content", "hola"},
                                 {"campoNuevo", true}});
    CHECK(tolerante.contenido == "hola", "campos desconocidos se toleran");
}

void probarSolicitud() {
    const auto minima = silvy::solicitudDesdeJson({{"command", "agent.step"}});
    CHECK(minima.comando == "agent.step", "comando de la solicitud");
    CHECK(minima.argumentos.is_object() && minima.argumentos.empty(),
          "args vacío por defecto");
    CHECK(minima.mensajes.empty(), "sin mensajes por defecto");
    CHECK(minima.herramientas.empty(), "sin herramientas por defecto");
    CHECK(minima.maxIteraciones == 3, "maxIterations por defecto");
    CHECK(!minima.promptSistema.has_value(), "sin prompt por defecto");

    const auto jsonSolicitud = cargarFixture("agente-step.json");
    CHECK(jsonSolicitud.is_object(), "existe el request JSON de ejemplo");
    if (jsonSolicitud.is_object()) {
        const auto solicitud = silvy::solicitudDesdeJson(jsonSolicitud);
        CHECK(solicitud.comando == "agent.step", "comando del ejemplo");
        CHECK(solicitud.mensajes.size() == 2, "mensajes del ejemplo");
        CHECK(solicitud.herramientas.size() == 2, "herramientas del ejemplo");
        CHECK(solicitud.maxIteraciones == 3, "maxIterations del ejemplo");
        CHECK(solicitud.promptSistema.has_value(), "prompt del ejemplo");
        CHECK(solicitud.id.has_value() && *solicitud.id == "demo-1",
              "id del ejemplo");
        if (solicitud.herramientas.size() == 2) {
            CHECK(solicitud.herramientas[0].nombre == "script.compile",
                  "primera herramienta del ejemplo");
        }
    }

    CHECK(lanzaConCodigo(
              [] {
                  silvy::solicitudDesdeJson(nlohmann::json::array());
              },
              silvy::CodigoError::SOLICITUD_INVALIDA),
          "solicitud no objeto da SOLICITUD_INVALIDA");
    CHECK(lanzaConCodigo(
              [] {
                  silvy::solicitudDesdeJson({{"command", 5}});
              },
              silvy::CodigoError::SOLICITUD_INVALIDA),
          "command no string da SOLICITUD_INVALIDA");
    CHECK(lanzaConCodigo(
              [] {
                  silvy::solicitudDesdeJson(
                      {{"command", "agent.step"}, {"maxIterations", 0}});
              },
              silvy::CodigoError::SOLICITUD_INVALIDA),
          "maxIterations 0 da SOLICITUD_INVALIDA");
    CHECK(lanzaConCodigo(
              [] {
                  silvy::solicitudDesdeJson(
                      {{"command", "agent.step"}, {"messages", "no-array"}});
              },
              silvy::CodigoError::SOLICITUD_INVALIDA),
          "messages no array da SOLICITUD_INVALIDA");
}

void probarRespuesta() {
    const auto exito = silvy::ResultadoComando::exito(
        {{"content", "hola"},
         {"toolCalls", nlohmann::json::array()},
         {"finishReason", "stop"}});
    CHECK(exito.codigoSalida == silvy::codigosSalida::kExito,
          "exito tiene codigo 0");
    const auto jsonExito = exito.respuesta.aJson();
    CHECK(jsonExito["apiVersion"] == silvy::kVersionApiSilvy,
          "apiVersion en la respuesta");
    CHECK(jsonExito["ok"] == true, "ok en la respuesta");
    CHECK(jsonExito["error"].is_null(), "error null en exito");
    CHECK(jsonExito["result"]["content"] == "hola", "result en exito");

    const auto fallo = silvy::ResultadoComando::fallo(
        {{"codigo", "SOLICITUD_INVALIDA"}, {"mensaje", "rota"}},
        silvy::codigosSalida::kSolicitudInvalida);
    CHECK(fallo.codigoSalida == silvy::codigosSalida::kSolicitudInvalida,
          "fallo tiene codigo 2");
    const auto jsonFallo = fallo.respuesta.aJson();
    CHECK(jsonFallo["ok"] == false, "ok false en fallo");
    CHECK(jsonFallo["error"]["codigo"] == "SOLICITUD_INVALIDA",
          "error tipado en fallo");
    CHECK(jsonFallo["result"].is_null(), "result null en fallo");

    silvy::Respuesta conId = silvy::Respuesta::ok({{"x", 1}});
    conId.id = 42;
    CHECK(conId.aJson()["id"] == 42, "eco del id");

    const std::string texto = conId.aTexto();
    CHECK(texto.find('\n') == std::string::npos, "aTexto es una sola linea");
    CHECK(nlohmann::json::parse(texto)["ok"] == true, "aTexto es JSON valido");
}

void probarCatalogo() {
    for (const silvy::ComandoSilvy comando : silvy::todosLosComandos()) {
        const auto vuelta = silvy::comandoDesdeTexto(
            silvy::textoDeComando(comando));
        CHECK(vuelta.has_value() && *vuelta == comando,
              "comando y texto hacen roundtrip");
    }
    CHECK(silvy::textoDeComando(silvy::ComandoSilvy::PASO_AGENTE) ==
              "agent.step",
          "texto de agent.step");
    CHECK(silvy::textoDeComando(silvy::ComandoSilvy::ERRORES_LIST) ==
              "errores.list",
          "texto de errores.list");
    CHECK(!silvy::comandoDesdeTexto("nada").has_value(),
          "comando desconocido sin valor");
}

void probarCasaYPrompt() {
    const fs::path base = fs::temp_directory_path();
    silvy::FuentesCasa fuentes;
    fuentes.directorioBinario = base / "bin";
    CHECK(silvy::resolverCasa(fuentes) == (base / "bin" / "silvy-home"),
          "casa por defecto junto al binario");

    fuentes.variableEntorno = (base / "desde-env").string();
    CHECK(silvy::resolverCasa(fuentes) == (base / "desde-env"),
          "SILVY_HOME pisa el default");

    fuentes.argumento = base / "desde-arg";
    CHECK(silvy::resolverCasa(fuentes) == (base / "desde-arg"),
          "--home pisa a SILVY_HOME");

    CarpetaPrueba casa("prompt");
    const fs::path dirCasa = casa.ruta() / "casa";
    const fs::path dirBin = casa.ruta() / "bin";
    fs::create_directories(dirCasa);
    fs::create_directories(dirBin / "config");
    escribirArchivo(dirCasa / "system_prompt.md", "prompt de casa");
    escribirArchivo(dirBin / "config" / "system_prompt.md",
                    "prompt empaquetado");

    CHECK(silvy::cargarPromptSistema(std::nullopt, dirCasa, dirBin) ==
              "prompt de casa",
          "la casa gana al prompt empaquetado");
    CHECK(silvy::cargarPromptSistema(std::string("override"), dirCasa,
                                     dirBin) == "override",
          "el override del request gana");
    fs::remove(dirCasa / "system_prompt.md");
    CHECK(silvy::cargarPromptSistema(std::nullopt, dirCasa, dirBin) ==
              "prompt empaquetado",
          "sin prompt de casa cae al empaquetado");
    fs::remove(dirBin / "config" / "system_prompt.md");
    CHECK(lanzaConCodigo(
              [&] {
                  silvy::cargarPromptSistema(std::nullopt, dirCasa, dirBin);
              },
              silvy::CodigoError::NO_CONFIGURADO),
          "sin ninguna fuente el prompt da NO_CONFIGURADO");
}

void probarPasoAgente() {
    EntornoNucleo entorno("paso-agente");
    escribirArchivo(entorno.casa / "system_prompt.md", "prompt de casa");

    silvy::RespuestaLlm turno;
    turno.contenido = "Voy a crear el script.";
    silvy::LlamadaHerramienta llamada;
    llamada.id = "call_1";
    llamada.nombre = "project.src.create";
    llamada.argumentos = {{"projectId", "Demo"},
                          {"ruta", "scripts/camara.funshi"}};
    turno.llamadas.push_back(llamada);
    turno.razonFin = silvy::RazonFin::LLAMADAS_HERRAMIENTA;

    silvy::pruebas::GatewayFalso gateway({turno});
    silvy::DependenciasNucleo dependencias{&gateway, entorno.registro,
                                           entorno.casa, entorno.bin};

    const auto solicitud = cargarFixture("agente-step.json");
    CHECK(solicitud.is_object(), "el fixture del paso existe");
    const auto resultado =
        silvy::ejecutarSolicitudJson(solicitud, dependencias);
    const auto respuesta = resultado.respuesta.aJson();

    CHECK(resultado.codigoSalida == silvy::codigosSalida::kExito,
          "agent.step responde exito");
    CHECK(respuesta["ok"] == true, "agent.step ok");
    CHECK(respuesta["error"].is_null(), "sin error en el paso");
    CHECK(respuesta["result"]["content"] == "Voy a crear el script.",
          "contenido del turno");
    CHECK(respuesta["result"]["toolCalls"].size() == 1,
          "un toolCall en la respuesta");
    CHECK(respuesta["result"]["toolCalls"][0]["name"] ==
              "project.src.create",
          "nombre del toolCall");
    CHECK(respuesta["result"]["toolCalls"][0]["arguments"]["projectId"] ==
              "Demo",
          "argumentos del toolCall");
    CHECK(respuesta["result"]["finishReason"] == "tool_calls",
          "razon de fin");
    CHECK(respuesta["id"] == "demo-1", "id ecoado en el exito");

    CHECK(gateway.peticiones().size() == 1, "el gateway recibio un paso");
    const auto& peticion = gateway.peticiones()[0];
    CHECK(peticion.mensajes.size() == 3, "prompt de sistema + historial");
    CHECK(peticion.mensajes[0].rol == silvy::RolMensaje::SISTEMA,
          "system primero");
    CHECK(peticion.mensajes[0].contenido ==
              "Sos un agente de prueba determinista.",
          "el override del request gana");
    CHECK(peticion.mensajes[1].rol == silvy::RolMensaje::SISTEMA,
          "el system de la CLI viaja como contexto");
    CHECK(peticion.mensajes[2].rol == silvy::RolMensaje::USUARIO,
          "el historial conserva el orden");
    CHECK(peticion.herramientas.size() == 2,
          "catalogo de herramientas completo");

    entorno.registro.vaciar();
    CHECK(lineasDeErrores(entorno).empty(),
          "un paso exitoso no persiste errores");
}

void probarPromptDeCasa() {
    EntornoNucleo entorno("prompt-casa");
    escribirArchivo(entorno.casa / "system_prompt.md", "prompt de casa");

    silvy::RespuestaLlm turno;
    turno.contenido = "Hola.";
    turno.razonFin = silvy::RazonFin::DETENCION;
    silvy::pruebas::GatewayFalso gateway({turno});
    silvy::DependenciasNucleo dependencias{&gateway, entorno.registro,
                                           entorno.casa, entorno.bin};

    const auto resultado = silvy::ejecutarSolicitudJson(
        {{"command", "agent.step"},
         {"messages",
          nlohmann::json::array(
              {{{"role", "user"}, {"content", "hola"}}})}},
        dependencias);
    const auto respuesta = resultado.respuesta.aJson();

    CHECK(resultado.codigoSalida == silvy::codigosSalida::kExito,
          "paso sin override exitoso");
    CHECK(respuesta["result"]["content"] == "Hola.",
          "contenido sin toolCalls");
    CHECK(respuesta["result"]["toolCalls"].is_array() &&
              respuesta["result"]["toolCalls"].empty(),
          "toolCalls vacio");
    CHECK(respuesta["result"]["finishReason"] == "stop",
          "finishReason stop");
    CHECK(!respuesta.contains("id"), "sin id si el request no lo trae");

    CHECK(gateway.peticiones().size() == 1, "el gateway recibio el paso");
    CHECK(gateway.peticiones()[0].mensajes[0].contenido == "prompt de casa",
          "el prompt de casa se usa sin override");
    CHECK(gateway.peticiones()[0].mensajes.size() == 2,
          "prompt + un mensaje");
}

void probarErroresDelPaso() {
    EntornoNucleo entorno("paso-errores");

    // Sin prompt de sistema: falla antes de tocar el gateway.
    silvy::pruebas::GatewayFalso sinUso({});
    silvy::DependenciasNucleo dependencias{&sinUso, entorno.registro,
                                           entorno.casa, entorno.bin};
    const auto sinPrompt = silvy::ejecutarSolicitudJson(
        {{"command", "agent.step"}, {"messages", nlohmann::json::array()}},
        dependencias);
    const auto respuestaSinPrompt = sinPrompt.respuesta.aJson();
    CHECK(sinPrompt.codigoSalida == silvy::codigosSalida::kFalloGeneral,
          "sin prompt sale 1");
    CHECK(respuestaSinPrompt["ok"] == false, "sin prompt ok false");
    CHECK(respuestaSinPrompt["error"]["codigo"] == "NO_CONFIGURADO",
          "sin prompt NO_CONFIGURADO");
    CHECK(respuestaSinPrompt["error"]["reintentable"] == false,
          "config no es reintentable");
    CHECK(sinUso.peticiones().empty(), "sin prompt no llega al gateway");

    entorno.registro.vaciar();
    auto lineas = lineasDeErrores(entorno);
    CHECK(lineas.size() == 1, "el error de config se persiste");
    if (!lineas.empty()) {
        CHECK(nlohmann::json::parse(lineas.back()) ==
                  respuestaSinPrompt["error"],
              "la respuesta es exactamente el registro guardado");
    }

    // El gateway falla con un error tipado.
    escribirArchivo(entorno.casa / "system_prompt.md", "prompt de casa");
    silvy::pruebas::GatewayFalso caido =
        silvy::pruebas::GatewayFalso::queFalla(silvy::ErrorTimeout(
            "el servidor no respondio"));
    silvy::DependenciasNucleo dependenciasCaido{&caido, entorno.registro,
                                                entorno.casa, entorno.bin};
    const auto conTimeout = silvy::ejecutarSolicitudJson(
        {{"command", "agent.step"},
         {"config", {{"mode", "REMOTE"}, {"modelName", "phi-4-mini"}}},
         {"messages",
          nlohmann::json::array(
              {{{"role", "user"}, {"content", "hola"}}})},
         {"id", "paso-2"}},
        dependenciasCaido);
    const auto respuestaTimeout = conTimeout.respuesta.aJson();
    CHECK(conTimeout.codigoSalida == silvy::codigosSalida::kFalloGeneral,
          "timeout sale 1");
    CHECK(respuestaTimeout["error"]["codigo"] == "TIMEOUT",
          "timeout tipado");
    CHECK(respuestaTimeout["error"]["reintentable"] == true,
          "timeout es reintentable");
    CHECK(respuestaTimeout["id"] == "paso-2", "el id viaja en el error");

    entorno.registro.vaciar();
    lineas = lineasDeErrores(entorno);
    CHECK(lineas.size() == 2, "el timeout se persiste");
    if (!lineas.empty()) {
        const auto guardado = nlohmann::json::parse(lineas.back());
        CHECK(guardado == respuestaTimeout["error"],
              "el timeout guardado es el de la respuesta");
        CHECK(guardado["comando"] == "agent.step",
              "contexto del comando");
        CHECK(guardado["modo"] == "REMOTE", "contexto del modo");
        CHECK(guardado["modelo"] == "phi-4-mini", "contexto del modelo");
    }

    // Una excepción ajena se normaliza a DESCONOCIDO con causa.
    GatewayQueRompe roto;
    silvy::DependenciasNucleo dependenciasRoto{&roto, entorno.registro,
                                               entorno.casa, entorno.bin};
    const auto conCrudo = silvy::ejecutarSolicitudJson(
        {{"command", "agent.step"},
         {"messages",
          nlohmann::json::array(
              {{{"role", "user"}, {"content", "hola"}}})}},
        dependenciasRoto);
    const auto respuestaCruda = conCrudo.respuesta.aJson();
    CHECK(conCrudo.codigoSalida == silvy::codigosSalida::kFalloGeneral,
          "excepcion cruda sale 1");
    CHECK(respuestaCruda["error"]["codigo"] == "DESCONOCIDO",
          "excepcion cruda normalizada");
    CHECK(respuestaCruda["error"]["causa"]["mensaje"] == "socket roto",
          "causa de la excepcion cruda");
}

void probarSinGateway() {
    EntornoNucleo entorno("sin-gateway");
    escribirArchivo(entorno.casa / "system_prompt.md", "prompt de casa");

    silvy::DependenciasNucleo dependencias{nullptr, entorno.registro,
                                           entorno.casa, entorno.bin};
    const auto resultado = silvy::ejecutarSolicitudJson(
        {{"command", "agent.step"}, {"messages", nlohmann::json::array()}},
        dependencias);
    const auto respuesta = resultado.respuesta.aJson();
    CHECK(resultado.codigoSalida == silvy::codigosSalida::kFalloGeneral,
          "sin gateway sale 1");
    CHECK(respuesta["error"]["codigo"] == "NO_CONFIGURADO",
          "sin gateway NO_CONFIGURADO");
    CHECK(respuesta["error"]["mensaje"]
              .get<std::string>()
              .find("gateway") != std::string::npos,
          "el mensaje menciona el gateway");
}

void probarSolicitudInvalidaEnNucleo() {
    EntornoNucleo entorno("nucleo-invalido");
    silvy::pruebas::GatewayFalso gateway({});
    silvy::DependenciasNucleo dependencias{&gateway, entorno.registro,
                                           entorno.casa, entorno.bin};

    const auto desconocido = silvy::ejecutarSolicitudJson(
        {{"command", "volar"}, {"id", 7}}, dependencias);
    const auto respuesta = desconocido.respuesta.aJson();
    CHECK(desconocido.codigoSalida ==
              silvy::codigosSalida::kSolicitudInvalida,
          "comando desconocido sale 2");
    CHECK(respuesta["error"]["codigo"] == "SOLICITUD_INVALIDA",
          "comando desconocido tipado");
    CHECK(respuesta["id"] == 7, "el id se ecoa en el error");

    entorno.registro.vaciar();
    const auto lineas = lineasDeErrores(entorno);
    CHECK(lineas.size() == 1, "el comando desconocido se persiste");
    if (!lineas.empty()) {
        CHECK(nlohmann::json::parse(lineas.back())["comando"] == "volar",
              "el comando intentado queda guardado");
    }

    const auto raizInvalida =
        silvy::ejecutarSolicitudJson(nlohmann::json::array(), dependencias);
    CHECK(raizInvalida.codigoSalida ==
              silvy::codigosSalida::kSolicitudInvalida,
          "raiz no objeto sale 2");
    CHECK(raizInvalida.respuesta.aJson()["error"]["codigo"] ==
              "SOLICITUD_INVALIDA",
          "raiz no objeto tipada");

    const auto sinComando = silvy::ejecutarSolicitudJson(
        {{"args", nlohmann::json::object()}}, dependencias);
    CHECK(sinComando.codigoSalida ==
              silvy::codigosSalida::kSolicitudInvalida,
          "sin command sale 2");

    const auto textoRoto =
        silvy::ejecutarSolicitudTexto("{ no es json", dependencias);
    CHECK(textoRoto.codigoSalida == silvy::codigosSalida::kSolicitudInvalida,
          "texto no JSON sale 2");
    CHECK(textoRoto.respuesta.aJson()["error"]["codigo"] ==
              "SOLICITUD_INVALIDA",
          "texto no JSON tipado");

    const auto transporte =
        silvy::reportarErrorDeTransporte("argv roto", dependencias);
    CHECK(transporte.codigoSalida == silvy::codigosSalida::kSolicitudInvalida,
          "error de transporte sale 2");
    CHECK(transporte.respuesta.aJson()["error"]["mensaje"] == "argv roto",
          "error de transporte conserva el mensaje");
}

void probarComandosDeErrores() {
    EntornoNucleo entorno("comandos-errores");
    escribirArchivo(entorno.casa / "system_prompt.md", "prompt de casa");
    silvy::pruebas::GatewayFalso caido =
        silvy::pruebas::GatewayFalso::queFalla(silvy::ErrorTimeout("tarde"));
    silvy::DependenciasNucleo dependencias{&caido, entorno.registro,
                                           entorno.casa, entorno.bin};

    const auto paso = silvy::ejecutarSolicitudJson(
        {{"command", "agent.step"},
         {"messages",
          nlohmann::json::array(
              {{{"role", "user"}, {"content", "hola"}}})}},
        dependencias);
    const std::string idTimeout =
        paso.respuesta.aJson()["error"]["id"].get<std::string>();

    silvy::ejecutarSolicitudJson({{"command", "volar"}}, dependencias);

    const auto lista =
        silvy::ejecutarSolicitudJson({{"command", "errores.list"}},
                                     dependencias);
    const auto respuestaLista = lista.respuesta.aJson();
    CHECK(lista.codigoSalida == silvy::codigosSalida::kExito,
          "errores.list sale 0");
    CHECK(respuestaLista["result"]["errores"].size() == 2,
          "errores.list devuelve los dos");
    CHECK(respuestaLista["result"]["errores"][0]["codigo"] ==
              "SOLICITUD_INVALIDA",
          "errores.list va de mas nuevo a mas viejo");
    CHECK(respuestaLista["result"]["errores"][1]["codigo"] == "TIMEOUT",
          "el segundo es el timeout");

    const auto filtrada = silvy::ejecutarSolicitudJson(
        {{"command", "errores.list"},
         {"args", {{"codigo", "TIMEOUT"}}}},
        dependencias);
    CHECK(filtrada.respuesta.aJson()["result"]["errores"].size() == 1,
          "filtro por codigo");
    CHECK(filtrada.respuesta.aJson()["result"]["errores"][0]["codigo"] ==
              "TIMEOUT",
          "el filtro deja el correcto");

    const auto limitada = silvy::ejecutarSolicitudJson(
        {{"command", "errores.list"}, {"args", {{"limite", 1}}}},
        dependencias);
    CHECK(limitada.respuesta.aJson()["result"]["errores"].size() == 1,
          "limite de la lista");
    CHECK(limitada.respuesta.aJson()["result"]["errores"][0]["codigo"] ==
              "SOLICITUD_INVALIDA",
          "el limite deja el mas nuevo");

    const auto porId = silvy::ejecutarSolicitudJson(
        {{"command", "errores.get"}, {"args", {{"id", idTimeout}}}},
        dependencias);
    const auto respuestaId = porId.respuesta.aJson();
    CHECK(porId.codigoSalida == silvy::codigosSalida::kExito,
          "errores.get sale 0");
    CHECK(respuestaId["result"]["encontrado"] == true,
          "errores.get encuentra");
    CHECK(respuestaId["result"]["registro"]["id"] == idTimeout,
          "errores.get devuelve el registro");
    CHECK(respuestaId["result"]["registro"] ==
              paso.respuesta.aJson()["error"],
          "el detalle coincide con la respuesta original");

    const auto ausente = silvy::ejecutarSolicitudJson(
        {{"command", "errores.get"},
         {"args", {{"id", "err-no-existe"}}}},
        dependencias);
    const auto respuestaAusente = ausente.respuesta.aJson();
    CHECK(ausente.codigoSalida == silvy::codigosSalida::kExito,
          "errores.get inexistente sale 0");
    CHECK(respuestaAusente["result"]["encontrado"] == false,
          "errores.get inexistente no encuentra");
    CHECK(respuestaAusente["result"]["registro"].is_null(),
          "registro null si no existe");

    const auto sinId = silvy::ejecutarSolicitudJson(
        {{"command", "errores.get"}}, dependencias);
    CHECK(sinId.codigoSalida == silvy::codigosSalida::kSolicitudInvalida,
          "errores.get sin id sale 2");
    CHECK(sinId.respuesta.aJson()["error"]["codigo"] ==
              "SOLICITUD_INVALIDA",
          "errores.get sin id tipado");

    const auto codigoInvalido = silvy::ejecutarSolicitudJson(
        {{"command", "errores.list"},
         {"args", {{"codigo", "NO_EXISTE"}}}},
        dependencias);
    CHECK(codigoInvalido.codigoSalida ==
              silvy::codigosSalida::kSolicitudInvalida,
          "codigo desconocido sale 2");
}

// E2E del binario real por subproceso. Devuelve false si el binario no está
// junto a las pruebas (build parcial): la suite completa se salta con 77.
bool probarSubproceso(const char* argv0) {
#ifdef _WIN32
    const fs::path nombreBinario = "Silvy.exe";
#else
    const fs::path nombreBinario = "Silvy";
#endif
    const fs::path bin =
        fs::absolute(argv0).parent_path() / nombreBinario;
    if (!fs::exists(bin)) {
        std::cout << "SALTADO: no se encontro " << bin.string()
                  << " para la prueba de subproceso\n";
        return false;
    }

    CarpetaPrueba carpeta("subproceso");
    const fs::path casa = carpeta.ruta() / "home";
#ifdef _WIN32
    // _popen pasa el comando por cmd.exe, que se come las comillas iniciales
    // de un ejecutable citado; `call` conserva la ruta con espacios.
    const std::string base =
        "call " + citar(bin) + " --home " + citar(casa) + " ";
#else
    const std::string base = citar(bin) + " --home " + citar(casa) + " ";
#endif

    // Respuesta determinista por --request-file, con eco del id.
    const fs::path listadoJson = carpeta.ruta() / "listado.json";
    escribirArchivo(listadoJson,
                    R"({"command":"errores.list","id":"eco-1"})");
    const SalidaProceso listado =
        ejecutarProceso(base + "--request-file " + citar(listadoJson));
    CHECK(listado.codigo == 0, "subproceso: errores.list sale 0");
    CHECK(listado.salida.find("\"ok\":true") != std::string::npos,
          "subproceso: respuesta ok");
    CHECK(listado.salida.find("\"id\":\"eco-1\"") != std::string::npos,
          "subproceso: eco del id");
    CHECK(fs::exists(casa / "errores"),
          "subproceso: la casa se crea al arrancar");

    // La misma solicitud por stdin.
    const SalidaProceso porStdin =
        ejecutarProceso(base + "--request < " + citar(listadoJson));
    CHECK(porStdin.codigo == 0, "subproceso: --request sale 0");
    CHECK(porStdin.salida.find("\"ok\":true") != std::string::npos,
          "subproceso: --request responde");

    // Excepción forzada: sale ok:false, exit 2 y queda en errores.jsonl.
    const fs::path errorJson = carpeta.ruta() / "error.json";
    escribirArchivo(errorJson, R"({"command":"volar","id":"malo-1"})");
    const SalidaProceso error =
        ejecutarProceso(base + "--request-file " + citar(errorJson));
    CHECK(error.codigo == 2, "subproceso: comando invalido sale 2");
    CHECK(error.salida.find("\"ok\":false") != std::string::npos,
          "subproceso: error ok:false");
    CHECK(error.salida.find("SOLICITUD_INVALIDA") != std::string::npos,
          "subproceso: error tipado");
    const auto lineas = leerLineas(casa / "errores" / "errores.jsonl");
    CHECK(lineas.size() == 1, "subproceso: un error persistido");
    if (!lineas.empty()) {
        const auto guardado = nlohmann::json::parse(lineas.front());
        CHECK(guardado["comando"] == "volar",
              "subproceso: comando guardado");
        CHECK(error.salida.find(
                  "\"id\":\"" + guardado["id"].get<std::string>() + "\"") !=
                  std::string::npos,
              "subproceso: el error guardado es el de la respuesta");
    }

    // JSON malformado: solicitud inválida y persistida.
    const fs::path rotoJson = carpeta.ruta() / "roto.json";
    escribirArchivo(rotoJson, "{ no es json");
    const SalidaProceso roto =
        ejecutarProceso(base + "--request-file " + citar(rotoJson));
    CHECK(roto.codigo == 2, "subproceso: JSON malformado sale 2");
    CHECK(roto.salida.find("SOLICITUD_INVALIDA") != std::string::npos,
          "subproceso: JSON malformado tipado");

    // Sin gateway: responde NO_CONFIGURADO con el prompt empaquetado.
    const fs::path pasoJson = carpeta.ruta() / "paso.json";
    escribirArchivo(
        pasoJson,
        R"({"command":"agent.step","messages":[{"role":"user","content":"hola"}]})");
    const SalidaProceso paso =
        ejecutarProceso(base + "--request-file " + citar(pasoJson));
    CHECK(paso.codigo == 1, "subproceso: sin gateway sale 1");
    CHECK(paso.salida.find("NO_CONFIGURADO") != std::string::npos,
          "subproceso: sin gateway NO_CONFIGURADO");
    CHECK(paso.salida.find("gateway") != std::string::npos,
          "subproceso: usa el prompt empaquetado junto al binario");

    // Uso inválido de argv: solicitud inválida con exit 2.
    const SalidaProceso ambos = ejecutarProceso(
        base + "--request --request-file " + citar(listadoJson));
    CHECK(ambos.codigo == 2, "subproceso: fuentes dobles sale 2");
    CHECK(ambos.salida.find("\"ok\":false") != std::string::npos,
          "subproceso: fuentes dobles ok:false");

    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::cout << "== errores ==\n";
    probarErrores();
    std::cout << "== hilo trabajador ==\n";
    probarHiloTrabajador();
    std::cout << "== registro ==\n";
    probarRegistro();
    std::cout << "== rotacion ==\n";
    probarRotacion();
    std::cout << "== registro degradado ==\n";
    probarRegistroDegradado();
    std::cout << "== validacion de config ==\n";
    probarValidacionConfig();
    std::cout << "== protocolo ==\n";
    probarProtocolo();
    std::cout << "== solicitud ==\n";
    probarSolicitud();
    std::cout << "== respuesta ==\n";
    probarRespuesta();
    std::cout << "== catalogo ==\n";
    probarCatalogo();
    std::cout << "== casa y prompt ==\n";
    probarCasaYPrompt();
    std::cout << "== paso agente ==\n";
    probarPasoAgente();
    std::cout << "== prompt de casa ==\n";
    probarPromptDeCasa();
    std::cout << "== errores del paso ==\n";
    probarErroresDelPaso();
    std::cout << "== sin gateway ==\n";
    probarSinGateway();
    std::cout << "== solicitud invalida en nucleo ==\n";
    probarSolicitudInvalidaEnNucleo();
    std::cout << "== comandos de errores ==\n";
    probarComandosDeErrores();
    std::cout << "== subproceso ==\n";
    const bool subproceso = probarSubproceso(argc > 0 ? argv[0] : "");

    std::cout << (fallos == 0 ? "OK" : "FALLOS") << ": " << total
              << " comprobaciones\n";
    if (fallos > 0) {
        return 1;
    }
    if (!subproceso) {
        return 77;
    }
    return 0;
}
