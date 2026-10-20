# AGENTS.md — Convenciones para agentes de código

Guía rápida para agentes (y humanos) que trabajen en este repositorio.

Silvy es el agente de inteligencia artificial de código del motor FunshiEngine:
un **binario autónomo** que atiende solicitudes JSON por stdin/stdout
(1 request = 1 paso, stateless). El loop del agente, el historial de
conversación y las herramientas viven en FunshiEngineCLI; Silvy solo habla con
el LLM y responde. No tiene UI, no guarda sesiones, no ejecuta herramientas,
no compila y no trae RAG ni skills propios. El diseño completo —decisiones,
protocolo, patrones, errores, concurrencia— está en `DISENO.md`: leerlo antes
de tocar código.

## Comandos esenciales

```bash
# Configurar y compilar (CMake es el único build soportado)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Ejecutar la suite de tests
cd build && ctest --output-on-failure

# Solicitud al binario: por archivo o por stdin
build/Silvy --request-file solicitud.json
build/Silvy --request < solicitud.json
```

- **`cmake` puede no estar en el PATH**: en Windows/MSYS2 usar
  `C:/msys64/mingw64/bin/cmake.exe` con `PATH=/mingw64/bin:$PATH`, y el resto
  del toolchain de `mingw64` (`ninja`, `g++`). Igual que la CLI.
- **Dependencias por FetchContent con tag fijo**: `nlohmann/json` (protocolo) y
  `cpp-httplib` (cliente HTTP hacia el LLM y servidor de pruebas). La primera
  configuración necesita red. `cpp-httplib` se usa con `SOURCE_SUBDIR` a una
  carpeta inexistente para no ejecutar el CMake de la dependencia, y con
  `CPPHTTPLIB_OPENSSL_SUPPORT` si el sistema tiene OpenSSL (https); sin él, el
  cliente solo habla `http://`.
- **Build y tests como parte del cambio**: `silvy_core` lista sus fuentes
  explícitamente (no hay glob). Un `.cpp` nuevo se agrega al target en el mismo
  commit que lo introduce.
- **La casa de Silvy** (`silvy-home/` junto al binario; precedencia
  `--home` > `SILVY_HOME` > junto al binario) es la única zona de datos de
  runtime: config, modelo descargado, `errores/` y logs. Ningún dato de runtime
  se escribe fuera de ella.

## Pruebas

- **Test headless único**: `tests/SilvyTests.cpp` define la macro `CHECK(cond,
  msg)` y su propio `CarpetaPrueba` (RAII para carpetas temporales) y cierra
  con `std::cout << (fallos == 0 ? "OK" : "FALLOS") << …`, saliendo con 0 o 1.
  El patrón es propio, inspirado en el de los repos hermanos: este repo no
  tiene submódulo del motor.
- **Gateway LLM falso**: `tests/GatewayFalso.*` implementa la interfaz
  `LlmGateway` con respuestas guionizadas y deterministas; es lo que prueba
  `agent.step` sin red ni modelo.
- **Servidor HTTP falso**: `tests/ServidorFalso.*` levanta un servidor local
  que habla como una API compatible OpenAI (respuestas guionizadas, SSE por
  bloques, errores HTTP y demoras); es lo que prueba el gateway remoto sin
  red ni modelo.
- **Código de salida 77 = *skipped*, no fallo**. La prueba que lanza el binario
  `Silvy` como subproceso lo busca junto a `silvy-tests`; si no está (p. ej.
  build parcial), se salta en lugar de fallar.
- **Criterio de aceptación**: invocar la solicitud JSON y comparar respuesta y
  exit code. `ctest` en verde antes de dar por cerrado un paso; un request de
  ejemplo produce una respuesta determinista y una excepción forzada queda en
  `errores.jsonl` y sale en la respuesta `ok:false`.

## Arquitectura

```
src/
├── main.cpp                # Solo transporte: argv, lectura de la solicitud
│                           #   (stdin/--request-file), escritura a stdout con
│                           #   su exit code. Sin lógica.
├── Nucleo/                 # Protocolo: Solicitud/Respuesta, catálogo de
│                           #   comandos, dispatch (switch exhaustivo) y
│                           #   captura única de errores en el borde.
├── Errores/                # Jerarquía ErrorSilvy + CodigoError, clasificador
│                           #   envolver() y registro persistente (errores.jsonl
│                           #   con rotación y retención).
├── Llm/                    # LlmGateway (interfaz), tipos de la petición,
│                           #   armado del paso del agente, ProtocoloOpenAi
│                           #   (cuerpo OpenAI + stream SSE + tool-calling),
│                           #   GatewayOpenAi (transporte HTTP, reintentos,
│                           #   cancelación) y FabricaGateway.
├── Config/                 # Casa de Silvy, ajustes y prompt de sistema
│                           #   (default en config/system_prompt.md).
├── Concurrencia/           # Hilo trabajador con cola (mutex/cv) y parada con
│                           #   vaciado, para trabajos de fondo.
└── Comandos/               # Un módulo por dominio de comando.
```

Reglas que sostienen esta arquitectura:

- **`main.cpp` no contiene lógica.** Solo parsea argv, lee la solicitud y
  escribe la respuesta. Todo lo demás vive en módulos del núcleo, para que la
  CLI pueda consumir Silvy igual por stdin/stdout que por llamada directa.
- **El núcleo no toca stdout.** Devuelve un `ResultadoComando` (respuesta +
  código de salida) sin imprimir nada. Las escrituras a stdout son del
  transporte; si hubiera operaciones de fondo, responden por cola al principal.
- **Stateless (1 request = 1 paso).** La CLI manda el historial completo y el
  catálogo de herramientas; Silvy no guarda sesiones entre solicitudes.
- **Silvy no ejecuta herramientas ni compila.** Pide tool calls y la CLI los
  ejecuta; los compiladores de los proyectos son de la CLI, no de Silvy.
- **Comandos en catálogo.** Cada comando nuevo se agrega al `enum class`, al
  mapa de texto en `CatalogoComandos.cpp` y a su caso en el `switch` del
  dispatch. El switch es exhaustivo: el compilador avisa si falta un caso.
- **Inyección por dependencias.** El núcleo recibe un `DependenciasNucleo`
  (gateway, registro de errores, configuración): los tests inyectan el gateway
  falso y una casa temporal sin tocar red ni disco real.
- **Errores que viajan normalizados.** Todo error crudo se envuelve con los
  clasificadores de `Errores/`; en el borde del comando se captura una sola
  vez, se persiste y se devuelve `ok:false` con el objeto de error tipado
  (exactamente el que quedó guardado). Timeout ≠ cancelación.
- **`apiVersion` en las respuestas.** Se incrementa al agregar funcionalidad;
  los campos nuevos van al final.

## Convenciones del código

- **Idioma**: C++17. Identificadores, comentarios y logs en español
  (`ejecutarSolicitud`, `resolverCasa`, `armarPeticionLlm`). Tipos en
  PascalCase, métodos en camelCase, espacios de nombres en minúsculas
  (`silvy`).
- **Copyright**: todo archivo fuente nuevo lleva el header Apache 2.0 (usar la
  cabecera de cualquier `.h`/`.cpp` existente como modelo) o al menos
  `SPDX-License-Identifier: Apache-2.0`.
- **Estilo**: 4 espacios, llaves en la línea siguiente para clases/funciones,
  UTF-8 sin BOM, fin de línea LF.
- **Sin referencias de gestión en los textos del código.** Comentarios y
  documentación técnica describen piezas, comportamiento, contratos y causas
  técnicas, nada más: sin identificadores de tareas, planes, fases, hitos,
  fechas ni estados ("pendiente", "por hacer"). Ese contexto pertenece a los
  documentos de gestión, issues y mensajes de commit.
- **Enums y switch-case**: `enum class` + `switch` exhaustivo (con `default`
  que lance o loguee) para todo conjunto cerrado de valores (comandos, modos,
  códigos de error, finishReason). El compilador es el primer linter.
- **Config validada fail-fast**: `validar()` devuelve **todos** los problemas
  en un solo error, no el primero. Defaults razonables y tolerancia a campos
  desconocidos al leer.
- **Sin código muerto**: lo diseñado pero no implementado se documenta en
  `DISENO.md`, no se deja código sin usar en el árbol.

## Memoria de trabajo: documentos locales ignorados por git

Todo el estado local que un agente necesita para retomar el trabajo sin
re-investigarlo vive bajo **una única carpeta ignorada por git**, `.agentes/`,
en la raíz del repo.

| Ruta | Para qué sirve |
|---|---|
| `.agentes/MEMORIA.md` | Memoria de contexto entre sesiones: estado del trabajo en curso, decisiones recientes, comandos de build/test que funcionan en el entorno y archivos que no se deben tocar. |

Reglas:

- **Crear lo que haga falta, manteniendo la raíz ordenada**: no crear archivos
  sueltos en la raíz del repo; la raíz es del proyecto, `.agentes/` del agente.
- **Leerlos al arrancar** una tarea que toque los temas que cubren y
  **escribir en ellos** lo nuevo relevante de forma incremental.
- Al commitear, **nunca** incluir `.agentes/`: un `git add <ruta>` explícito de
  los archivos propios de la tarea es la única forma correcta de agregar.

## Flujo de trabajo

- **Lista de tareas antes de escribir código**: si la tarea requiere más de un
  paso, armar primero una lista de pendientes explícita y trabajar contra ella,
  marcando cada tarea como completada en cuanto esté hecha y verificada.
- **Una tarea, un commit (o varios si es grande), atómicos y convencionales**
  (`feat:`, `fix:`, `docs:`, `refactor:`, `chore:`), con mensaje descriptivo.
  Nunca mezclar temas distintos en un mismo commit.
- **Documentación sincronizada**: `DISENO.md` es documento vivo y se actualiza
  en el mismo commit del cambio que lo motive; `README.md` y `AGENTS.md` se
  mantienen al día cuando el cambio los afecta.
- **Sin atribuciones ajenas al cambio**: los mensajes de commit describen
  únicamente la implementación; nunca trailers `Co-authored-by`.
- **Commits solo con archivos propios**: `git add <archivos>` explícito de la
  tarea; nunca `git add -A` ni `git add .` (arrastrarían cambios ajenos).
- **Comandos destructivos requieren permiso y justificación**: antes de
  `git checkout`, `reset`, `restore`, `clean` o cualquier comando que descarte
  cambios, pedir permiso explícito al usuario, argumentar en español qué se
  perderá y esperar confirmación.
- **Validar antes de commitear**: build completo + `ctest` en verde. Si el
  cambio toca un comando, además invocar el binario con una solicitud real y
  verificar respuesta y exit code.
- **NO confirmar fixes hasta validación del usuario**: nunca declarar un cambio
  como "resuelto" o "listo" hasta que el usuario lo pruebe y lo confirme
  explícitamente.
