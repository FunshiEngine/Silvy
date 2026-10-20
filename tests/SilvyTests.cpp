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
#include "Nucleo/CatalogoComandos.h"
#include "Nucleo/Protocolo.h"
#include "Nucleo/Respuesta.h"
#include "Nucleo/Solicitud.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

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

    const fs::path fixture =
        fs::path(SILVY_DIR_FIXTURES) / "agente-step.json";
    std::ifstream entrada(fixture, std::ios::binary);
    CHECK(entrada.good(), "existe el request JSON de ejemplo");
    if (entrada.good()) {
        const auto jsonSolicitud = nlohmann::json::parse(entrada);
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
    auto escribir = [](const fs::path& ruta, const std::string& contenido) {
        std::ofstream salida(ruta, std::ios::binary);
        salida << contenido;
    };
    escribir(dirCasa / "system_prompt.md", "prompt de casa");
    escribir(dirBin / "config" / "system_prompt.md", "prompt empaquetado");

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

}  // namespace

int main() {
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

    std::cout << (fallos == 0 ? "OK" : "FALLOS") << ": " << total
              << " comprobaciones\n";
    return fallos == 0 ? 0 : 1;
}
