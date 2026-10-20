# Silvy — Diseño y plan

Documento de diseño del **agente de inteligencia artificial de código para el motor
FunshiEngine**. Consolida todas las decisiones tomadas en la conversación de planificación;
es la referencia de partida antes de escribir código.

## 1. Visión

Silvy es un **agente** (no una app de escritorio ni un chatbot) que, mediante un LLM y
configuración de conocimiento/herramientas que el usuario aporta desde la CLI, **crea y
modifica código para el motor gráfico FunshiEngine**.

Los tres pilares y sus responsabilidades:

| Pieza | Rol |
|---|---|
| **Silvy (AGENTE)** | Cerebro: capa LLM (local + remoto) y protocolo de agente (pensar → pedir herramienta → observar → iterar). Stateless, sin conocimiento propio. |
| **FunshiEngineCLI** | Manos: ejecuta las herramientas contra su núcleo (`src/Nucleo`), posee el loop del agente y el historial de conversación, y en el futuro aporta la UI humana. |
| **Documentación / skills / RAG** | Conocimiento: todo lo que el agente "sabe" lo configura el usuario **desde la CLI**. Silvy no trae RAG ni skills propios. |

### Qué NO es Silvy

- **No es FunshiEngineAI**: ese es un chatbot de escritorio con RAG (asistente que responde
  preguntas citando documentación). No es agente ni tiene acceso a la CLI.
- **No es CHATIA**: en el futuro instalador, CHATIA es el producto chat (linaje de
  FunshiEngineAI). Silvy es agente puro de código. Son productos distintos.

### Horizonte: instalador de 4 componentes

Hoy son repos separados bajo `Projects/`; en el futuro un instalador empaqueta la suite:

```
MotorGrafico  |  CLI  |  AGENTE (Silvy)  |  CHATIA
```

Cada componente es una pieza instalable y actualizable por separado. Esto condiciona la
integración: **Silvy es un binario instalado aparte** (ver §3).

## 2. Decisiones tomadas

Registro de lo acordado en la planificación. Cada entrada es un cierre, no una preferencia.

| # | Tema | Decisión |
|---|---|---|
| D1 | **Loop del agente** | Vive en la CLI (opción A). La CLI arma request → Silvy responde texto/toolCalls → la CLI ejecuta las herramientas → repite. Silvy no ejecuta nada. |
| D2 | **Stack** | C++17. Mismo lenguaje que el motor y la CLI. |
| D3 | **Comunicación CLI↔Silvy** | JSON por stdin/stdout. Mismo patrón que FunshiCLI (`--request` / `--request-file` → respuesta JSON. |
| D4 | **Grano del protocolo** | 1 request = 1 paso. Stateless: la CLI manda historial completo + catálogo de tools; Silvy devuelve el siguiente turno. |
| D5 | **Estado de la sesión** | Dueño: la CLI. Silvy no guarda sesiones. |
| D6 | **Conocimiento (RAG/skills)** | Silvy **no** incluye RAG. El usuario decide si darle RAG, skills u otras extensiones, y eso se configura **desde la CLI**. |
| D7 | **System prompt** | Silvy trae un default (rol, reglas); la CLI puede mandar uno propio u override por request. Editable por el usuario. |
| D8 | **Vida del LLM** | La gestiona Silvy (descarga GGUF con reanudación, arranque/reinicio de `llama-server`, modo local↔remoto) **+ comandos de control** que la CLI puede invocar (status, descargar modelo, cambiar modo). |
| D9 | **Tool-calling** | Formato estilo OpenAI (tools + JSON Schema): lo entienden igual `llama-server` local que cualquier API remota. Un solo protocolo para ambos modos. |
| D10 | **Alcance del código generado** | **Ambos**: contenido de proyectos de juegos (scripts, shaders, assets, UI web HTML/CSS/JS) **y** código fuente del motor (C++ de FunshiEngineOpenGL, sus tests, su CMake). |
| D11 | **Rol de HTML/CSS/JS** | UI web dentro del juego (overlays, menús, HUD). |
| D12 | **Acciones sensibles** | Aprobación previa: el toolCall pendiente de aprobación llega a la UI, el humano aprueba/rechaza antes de ejecutar. |
| D13 | **Aprobación sin UI** | No aplica: la CLI tendrá UI para cuando Silvy esté listo. El protocolo igual contempla el campo de aprobación desde el día 1. |
| D14 | **Iteraciones de auto-corrección** | Límite **configurable** por el usuario. |
| D15 | **Recorte de contexto** | Lo hace la CLI (dueña del historial recorta/Resume para que quepa en la ventana). Silvy rechaza requests demasiado grandes con error tipado. |
| D16 | **Streaming** | MVP: cada paso se responde como JSON completo. El streaming de tokens Silvy→CLI se diseña después (con la UI); el SSE Silvy→LLM es obligatorio desde el día 1. |
| D17 | **Integración CLI→Silvy** | Binario independiente instalado aparte; la CLI lo lanza como subproceso y le manda JSON por stdin/stdout (ruta en su config o PATH). |
| D18 | **Configuración** | Configs separados por componente: Silvy tiene su config de LLM; la CLI la suya (ruta al agente, tools habilitadas, aprobaciones). |
| D19 | **Modelo local default** | Phi-4-mini-instruct GGUF (`unsloth/Phi-4-mini-instruct-GGUF`, ~2.4 GB), contextSize 8192, temperature 0.2 — mismo que FunshiEngineAI. |
| D20 | **Idioma** | Español: system prompt, mensajes del historial y código generado (el motor exige identificadores/comentarios en español — `AGENTS.md` del motor). |
| D21 | **Verificación** | Loop de verificación en el MVP desde el día 1: generar → `script.compile` → `diagnostics.list` → corregir. |
| D22 | **Demo end-to-end del MVP** | «Creame un script que mueva la cámara»: la instrucción humana termina en un script C++ compilado sin errores. |
| D23 | **Tests** | Gateway LLM **falso** determinista (respuestas guionizadas con toolCalls fijos): el protocolo se prueba sin red ni modelo. Patrón headless hermano (`CHECK` + ctest). |
| D24 | **Orden de construcción** | Documento de diseño primero (este documento), después código. |
| D25 | **Concurrencia** | Modelo explícito de hilos (ver §6): hilo principal de protocolo que nunca bloquea, operaciones largas (inferencia, descarga, supervisor de `llama-server`) en hilos dedicados, estado compartido con `atomic`/`mutex` mínimo y cancelación por bandera. Sin librerías de hilos externas: `std::thread` de C++17. |
| D26 | **Patrones de diseño** | Catálogo fijado en §7: Facade/Gateway, Strategy, Factory, Observer/callback, retry con backoff, single-flight, lock por recurso, cancel-first, health-check, circuit breaker (futuro), cola con coalescing (futuro). |
| D27 | **Clean Code** | Reglas en §8: módulos puros y testeables sin I/O, funciones cortas con una intención, nombres que revelan intención (español), sin dependencias ocultas, configuración inyectada, cero código muerto. |
| D28 | **Excepciones tipadas** | Jerarquía de excepciones diseñada para el dominio de Silvy (ver §9): código semántico + `reintentable` + causa; todo error crudo (HTTP, red, timeout, config) se normaliza a través de clasificadores. La config inválida falla con **todos** los problemas listados en un solo error. |
| D29 | **Registro persistente de excepciones** | Toda excepción no silenciada se guarda en disco (`errores/`) para que el usuario la consulte desde la CLI/UI, y el mensaje tipado se devuelve al llamador cuando algo sale mal (§9.4). Ningún fallo se pierde solo en consola. |
| D30 | **Inspiración documentada** | La arquitectura se apoya en lo ya probado en **FunshiEngineAI** (backend Kotlin: `llm-api`/`backend`) y **SAI** (Electron/TS: `main/llm/`): ver §10 la tabla de qué se toma de cada uno. |
| D31 | **Toolchain de desarrollo** | Windows: **MSYS2 MinGW64** (cmake, g++, ninja de `/mingw64`), igual que la CLI. Documentado en `AGENTS.md` cuando exista. Multiplataforma en CI a futuro. |
| D32 | **Git** | Vinculado al arrancar H1: repo local con rama `main` y remoto `origin` (`github.com/FunshiEngine/Silvy.git`). Los commits se empujan a pedido del usuario. |
| D33 | **Silvy no compila** | El agente **nunca compila** el código que genera: compilar es trabajo de la CLI (`script.compile`). Las dependencias de Silvy (JSON, HTTP) son para **su propio protocolo y su conexión con el LLM**, no para toolchains del usuario. |
| D34 | **Dependencias C++ (P12)** | `nlohmann/json` y `cpp-httplib` por **FetchContent con tag fijo** (header-only, sin vendorizar): `nlohmann/json` desde H1 y `cpp-httplib` cuando llegue el cliente HTTP. Mismo patrón que FTXUI en la CLI. |
| D35 | **Casa de Silvy (P13)** | Directorio de datos de runtime **`silvy-home/` junto al binario** (estilo `app-home` de FunshiEngineAI): config, prompt de sistema editable, modelo GGUF, `errores/` y logs. Precedencia: `--home` > variable `SILVY_HOME` > junto al binario. |
| D36 | **Orden de construcción de H1 (P14)** | Errores (jerarquía + registro persistente) → protocolo (tipos, parseo, respuesta, catálogo) → gateway falso con tests → primitivas de concurrencia → `main` de transporte → e2e por subproceso. |
| D37 | **Capacidades (P1)** | Sin handshake: la CLI manda el catálogo completo de herramientas en cada `agent.step`; Silvy es stateless y no guarda un paso de descubrimiento. |
| D38 | **Errores del protocolo (P2)** | `ok:false` con `error` = objeto tipado persistido (`id`, `ts`, `codigo` UPPER_SNAKE, `mensaje`, `reintentable`, `comando`, `modo`, `modelo`, `detalle`, `causa`). Exit codes: `0` éxito, `1` fallo operacional, `2` solicitud inválida. Los errores deterministas no son reintentables. |
| D39 | **Streaming en el protocolo (P8)** | Sin campo reservado en el MVP: cada paso responde el turno completo. Cuando se diseñe streaming (D16) se agrega con incremento de `apiVersion`, no como placeholder. |
| D40 | **Registro de errores (P9)** | `errores.jsonl` con rotación de 256 KB, 10 archivos rotados y TTL de 30 días por defecto (configurables); consulta por `errores.list`/`errores.get` por JSON, suficiente para la CLI; cada componente persiste sus propios errores (D18). |

### Skills: definición

**Skills** = archivos de instrucciones/plantillas que el usuario aporta desde la CLI y que
**no tocan el system prompt** (se inyectan aparte según el contexto de la tarea). Además, el
usuario **sí podrá modificar el system prompt** (D7). El mecanismo exacto de skills queda
abierto para la fase de extensibilidad (§12, P5).

## 3. Arquitectura

```
┌─────────────────────────────  FunshiEngineCLI  ─────────────────────────────┐
│  UI futura (humano) → núcleo ejecutarSolicitud()                            │
│                                                                              │
│  LOOP DEL AGENTE (vive acá — D1):                                           │
│   1. Arma request JSON: system prompt + historial + catálogo de herramientas │
│      (built-in de la CLI + las que el usuario configure: RAG, skills, MCP)  │
│   2. → stdin de Silvy (binario instalado aparte — D17)                      │
│   3. ← respuesta JSON: texto del asistente ± toolCalls ± approvalNeeded     │
│   4. Ejecuta cada toolCall contra su núcleo:                                │
│      project.create / project.src.create / script.compile /                 │
│      diagnostics.list / fs.create / dir.import / …                          │
│   5. Recorta el contexto si hace falta (D15 — la CLI es dueña del estado)   │
│   6. Devuelve resultados al historial → paso 1 hasta terminar               │
│                                                                              │
│  Verificación (MVP — D21): generar → script.compile →                       │
│  diagnostics.list → corregir (iteraciones limitadas, D14)                   │
│  Acciones sensibles: esperan aprobación en la UI (D12)                      │
└──────────────────────────────────────────────────────────────────────────────┘
                     JSON stdin/stdto (1 request = 1 paso — D4)
                     HTTP/SSE (obligatorio)
┌──────────────────────────────  Silvy  ──────────────────────────────────────┐
│  C++17 (D2), binario autónomo: `silvy --request [--request-file f.json]`    │
│                                                                              │
│  Núcleo/Protocolo  — parseo de request, respuesta, errores tipados          │
│  Capa LLM          — LOCAL (descarga GGUF + llama-server) | REMOTO (API     │
│                      compatible OpenAI) — D8, D19. Gateway con SSE.         │
│                      Tool-calling formato OpenAI (D9).                      │
│  System prompt     — default propio en español (D20) + override (D7)        │
│  Control           — comandos: status, descargar modelo, cambiar modo (D8)  │
│                                                                              │
│  Stateless (D5): sin sesiones, sin RAG (D6), sin ejecución de herramientas.  │
└──────────────────────────────────────────────────────────────────────────────┘
```

### Por qué el loop vive en la CLI (D1)

- La UI futura quiere ver el progreso paso a paso, aprobar acciones sensibles (D12) y
  controlar pausas/ramas: necesita dirigir el flujo, no delegarlo.
- Evita la circularidad CLI→Silvy→CLI (Silvy no tendría cómo ejecutar herramientas sino
  invocando a la CLI).
- El núcleo agnóstico de transporte de la CLI (`ejecutarSolicitud()` → `ResultadoComando`)
  ya está diseñado para ser llamado sin pasar por stdout: las herramientas se ejecutan en
  el mismo proceso.
- Con 1 request = 1 paso (D4), cada turno es una transacción JSON fácil de testear.

## 4. Protocolo CLI↔Silvy (borrador)

Formato base: JSON sobre stdin/stdout, espejo del patrón de FunshiCLI. Campos en inglés,
valores de texto en español. **Borrador a refinar en H1** (§12).

### 4.1 Request (`agent.step`)

```jsonc
{
  "command": "agent.step",
  "config": {                       // override por request; lo ausente usa el config de Silvy (D18)
    "mode": "LOCAL",                // LOCAL | REMOTE
    "baseUrl": "",                  // API compatible OpenAI en modo REMOTE
    "apiKey": "",
    "modelName": ""
  },
  "systemPrompt": "…",              // opcional: override de la CLI (D7); si falta, default de Silvy
  "messages": [                     // historial completo, recortado por la CLI (D15)
    { "role": "system",    "content": "…" },
    { "role": "user",      "content": "…" },
    { "role": "assistant", "content": "…", "toolCalls": [ … ] },
    { "role": "tool",      "toolCallId": "…", "content": "…" }
  ],
  "tools": [                        // catálogo que arma la CLI (built-in + RAG/skills/MCP del usuario)
    {
      "name": "script.compile",
      "description": "Compila los scripts del proyecto…",
      "parameters": { "type": "object", "properties": { … }, "required": [ … ] }
    }
  ],
  "maxIterations": 3,               // límite de auto-corrección (D14) — referencia para la CLI
  "id": "paso-1"                    // opcional: eco en la respuesta
}
```

`args` es el objeto de argumentos de los comandos de control (`errores.list`,
`errores.get`); `agent.step` no lo usa. El prompt de sistema efectivo (override
del request, `<casa>/system_prompt.md` o `config/system_prompt.md` junto al
binario) encabeza la conversación; los mensajes `system` del historial viajan
detrás como contexto de la CLI.

### 4.2 Response

```jsonc
{
  "apiVersion": 1,
  "ok": true,
  "error": null,                    // errores tipados: LLM_DOWN, MODEL_MISSING, REQUEST_TOO_LARGE…
  "result": {
    "content": "texto del asistente en español…",
    "toolCalls": [                  // vacío si el agente solo habla
      { "id": "call_abc", "name": "script.compile", "arguments": { … } }
    ],
    "finishReason": "tool_calls"    // stop | tool_calls | length | …
  },
  "id": "paso-1"                    // opcional: eco de la solicitud
}
```

La aprobación (D12) la resuelve la CLI al ejecutar: si el toolCall está en su lista de
acciones sensibles, va a la UI antes de correr y su resultado (o rechazo) vuelve al
historial como mensaje `tool`.

### 4.3 Comandos de control (D8)

Además de `agent.step`, la CLI puede invocar:

- `errores.list` — últimos errores persistidos: `args` admite `limite` (50 por
  defecto) y `codigo` (filtro por `CodigoError`); devuelve
  `{ "errores": [ … ] }`, más reciente primero.
- `errores.get` — detalle por id: `args: { "id": "…" }`; devuelve
  `{ "encontrado": true|false, "registro": { … } | null }` (un id inexistente
  no es un fallo).
- `llm.status` — modo, modelo, si `llama-server` está vivo, progreso de descarga.
- `llm.download` — inicia/reanuda la descarga del GGUF.
- `llm.mode` — cambia LOCAL↔REMOTO.

## 5. Convenciones hermanadas (de los repos vecinos)

Se adoptan las mismas reglas que `FunshiEngineOpenGL` y `FunshiEngineCLI`:

- **C++17**, identificadores/comentarios/logs **en español** (D20), tipos PascalCase,
  métodos camelCase, espacios de nombres en minúsculas.
- **Estilo**: 4 espacios, llaves en la línea siguiente para clases/funciones.
- **Copyright**: header Apache 2.0 o `SPDX-License-Identifier: Apache-2.0` en cada archivo
  fuente nuevo.
- **Sin referencias de gestión** en comentarios ni textos del código (sin ids de tarea,
  planes, fases, estados): solo piezas, comportamiento, contratos y causas técnicas.
- **`enum class` + `switch` exhaustivo** (con `default` que lance/loguee) para todo conjunto
  cerrado de valores (comandos, modos, finishReason, errores).
- **Tests headless**: patrón `CHECK(cond, msg)` definido en el propio archivo,
  `TempPruebas::CarpetaPrueba` (RAII) para temporales, cierre con
  `std::cout << (fallos == 0 ? "OK" : "FALLOS") …` y exit 0/1. Exit **77 = skipped**, no
  fallo. Correr con `ctest --output-on-failure`.
- **Gateway falso en tests (D23)**: un `LlmGateway` determinista con respuestas guionizadas
  para probar el protocolo sin red ni modelo.
- **Memoria de trabajo** en `.agentes/` (carpeta ignorada por git): `MEMORIA.md` y lo que
  haga falta; no se commitea jamás.
- **Commits atómicos por tarea**, convencionales (`feat:`, `fix:`, `docs:`, `refactor:`,
  `chore:`), solo archivos propios, sin coautorías ni atribuciones ajenas.
- **Build + tests en verde antes de dar por cerrado** un paso; **sin confirmar avances sin
  validación del usuario**.

## 6. Concurrencia: hilos y sincronización (D25)

Silvy es un binario que atiende la stdin de la CLI **y** a la vez puede estar generando con
el LLM, descargando un modelo y vigilando un `llama-server`. El modelo de hilos se diseña
desde el día 1, no se improvisa:

### Hileras (roles)

| Hilo | Rol | Notas |
|---|---|---|
| **Principal** | Bucle de lectura de stdin: recibe una solicitud JSON, la despacha, escribe la respuesta a stdout. | Nunca bloquea en red ni en descargas: si la operación es larga, la delega. Único dueño del stdout de respuesta (evita respuestas interleaved). |
| **Inferencia** | Un hilo por operación larga `agent.step`: HTTP + streaming SSE contra el LLM. | Devuelve el resultado al principal mediante cola; el principal sigue atendiendo `llm.status`/`llm.cancel` mientras tanto. |
| **Descarga** | Descarga del GGUF con reanudación, progreso, cancelación. | **Single-flight**: un solo job por archivo de destino (lock por ruta, ver §7); un `llm.cancel` lo detiene sin perder el `.part`. |
| **Supervisor** | Health-check periódico de `llama-server` (poll `/health`), detección de crash, reinicio bajo demanda. | Marca estado atómico `servidorVivo`; los errores de crash generan excepción persistida (§9). |
| **Pump de stderr** | Lee el stderr del proceso hijo en streaming (línea a línea, tail ~4 KB en buffer circular). | Imprescindible para adjuntar el `logTail` a los errores de arranque; sin él, el pipe se llena y el hijo bloquea. |

### Reglas de sincronización

- **Estado compartido mínimo**: `std::atomic<bool>`/`std::atomic<State>` para banderas
  (cancelado, servidor vivo, descargando); `std::mutex` + `std::condition_variable` solo
  para lo que requiere espera. El mutex **nunca** rodea la operación larga (la inferencia
  no corre "dentro del lock"): protege solo la contabilidad, patrón heredado de SAI.
- **Cola de resultados**: los hilos de trabajo encolan `(idSolicitud, resultado)` y el
  principal los serializa a stdout. Escrituras a stdout exclusivamente desde el principal.
- **Cancelación por bandera**: `std::atomic<bool> cancelado_` revisada **entre chunks** del
  SSE y entre bloques de descarga (cada 64 KB). Se distingue siempre
  **timeout (reintentable) ≠ cancelación (no reintentable)** — lección clave de
  FunshiEngineAI: jamás convertir una cancelación en un error fatal.
- **Timeouts en doble capa**: timeout del cliente HTTP (connect) + timeout global de la
  operación completa (incluido el streaming), para que un LLM colgado no deje pasos zombi.
- **Procesos hijo**: `llama-server` como proceso hijo con pipes; apagado **escalonado**
  (terminar → espera corta → kill del árbol de procesos → espera → kill forzado). En
  Windows matar el árbol (`taskkill /T /F` o equivalente), en POSIX equivalente; un hook de
  cierre garantiza que no quede `llama-server` huérfano al morir Silvy.
- **Sin pools genéricos**: el modelo es "hilo dedicado por rol" (son 4-5 hilos fijos).
  Si en el futuro hubo cola de inferencia con coalescing (§7), se diseña ahí.
- **Estados tipados, no banderas sueltas**: `enum class EstadoLlm` (DESCARGANDO, LISTO,
  CARGANDO, CAIDO, …) con `switch` exhaustivo; el supervisor es el único que lo transiciona.

## 7. Patrones de diseño (D26)

Catálogo cerrado de patrones que Silvy aplica y sus lugares concretos:

| Patrón | Dónde vive en Silvy | Por qué / origen |
|---|---|---|
| **Interface + Gateway/Facade** | `LlmGateway` (interfaz): `paso()` / `cancelar()` / `estado()`. Un solo implementador `OpenAiCompatibleGateway`. | El núcleo no sabe si detrás hay `llama-server` local o una API remota. Origen: `LlmGateway.kt` de FunshiEngineAI. |
| **Strategy (convergente)** | Modos `LOCAL` y `REMOTE`: misma clase gateway, cambia cómo se arma la `baseUrl` y el ciclo de vida. `enum class ModoLlm` + `switch` exhaustivo. | Evita duplicar lógica de streaming/tool-calling por modo. Origen: `LlmService.start()` de FunshiEngineAI. |
| **Factory** | `crearGateway(config, modo)` decide servidor local vs API remota y arma la URL efectiva. | Punto único de construcción; testeable inyectando config. |
| **Observer / callback tipado** | Progresos (descarga, arranque) por `std::function<void(const Progreso&)>`, con throttle (solo si cambia, o cada ~300 ms). | No saturar al consumidor futuro (CLI/UI). Origen: callbacks de FunshiEngineAI y `progressBridge` de SAI. |
| **Retry con backoff exponencial** | Solo sobre errores con `reintentable == true` (2 reintentos, 300→600 ms). Nunca sobre errores deterministas (clave inválida, cuota, config). | Origen: `retryTransient` (FunshiEngineAI) / `ApiEngine` (SAI). |
| **Single-flight** | Descarga del GGUF: lock por ruta absoluta; si otro hilo ya está descargando ese archivo, espera y reusa el resultado. Igual para arranque del servidor (`ensureReady`). | Dos solicitudes concurrentes no arrancan dos `llama-server` ni descargan dos veces. Origen: `modelDownloadLock.ts` (SAI) / `HuggingFaceDownloader.kt`. |
| **Lock por recurso con doble chequeo** | `if (existe) return; lock(ruta) { if (existe) return; … }`. | Idempotencia de descarga/arranque. |
| **Cancel-first** | Una operación larga nueva cancela la anterior (job único por tipo); el `finally` limpia solo si el job vigente sigue siendo el propio. | Evita carreras de cancelación. Origen: `ModelManager.download` de FunshiEngineAI. |
| **Health-check + detección de crash** | Poll de `/health` (200 = sano, 503 = cargando) con timeout de carga; si el hijo murió → error inmediato con código de salida + tail del log. | El error es accionable, no "falló el servidor". Origen: `LocalServerManager.kt` / `LlamaCppServerManager.ts`. |
| **Reserva de puerto anti-TOCTOU + escaneo** | Reservar el puerto con un bind real hasta justo antes del spawn; si está ocupado, escanear ~20 puertos consecutivos. | Origen: tickets B-11 (`portPolicy.ts` de SAI; escaneo también en FunshiEngineAI). |
| **Apagado escalonado (graceful shutdown)** | Hook de cierre: terminar → 3 s → kill de árbol → 2 s → kill forzado. | Cero procesos huérfanos. Origen: ambos proyectos. |
| **Registro de operaciones con TTL** | Historial reciente de pasos/peticiones con estado terminal único y expiración, consultable por id. | Para que la UI futura pueda re-hidratarse. Origen: `chatRequestRegistry.ts` de SAI. |
| **Degradación tolerante** | Una fuente/operación secundaria que falla genera *warning* persistido, no aborta el paso completo. | Origen: `KnowledgeBase.kt` de FunshiEngineAI. |
| **Circuit breaker + fallback local↔remoto** | **Diseñado pero no en el MVP**: contrato del gateway ya permite añadir un segundo backend después; reservar el enum de códigos `SIN_SALUD`. | Registro de fallos 2→abierto 10 s→half-open, EWMA de latencia. Origen: `routing/circuitBreaker.ts` de SAI. Decisión análoga tomada en FunshiEngineAI (documentado, no adoptado aún). |
| **Cola con coalescing ("última tecla gana")** | **Futuro**: si la UI manda peticiones de streaming mientras otra corre, coalescer por tipo y abortar la anterior. | Solo cuando exista streaming (D16). Origen: `predictionQueue.ts` de SAI. |

Además, la arquitectura se sostiene en los patrones estructurales ya elegidos y no se
reabren: **Facade** (el núcleo no ve HTTP/procesos), **DI por constructor** (todo recurso
inyectable → testeable con el gateway falso, D23), y **capas sin I/O escondido** (módulos
puros para lógica de negocio).

## 8. Clean Code (D27)

Reglas concretas, más allá de las convenciones hermanadas de estilo (§5):

- **Módulos puros donde sea posible**: toda lógica de decisión (armado del payload para el
  LLM, parseo del SSE, clasificación de errores, validación de config, selección de
  candidatos, cálculo de backoff) vive en funciones **sin I/O**, que reciben datos y
  devuelven datos → testeables sin red, sin procesos, sin disco.
- **Una función, una intención**: funciones cortas con nombres que la dicen
  (`ensayarPaso`, `parsearSse`, `clasificarErrorHttp`, `reservarPuerto`). Si un comentario
  tiene que explicar *qué hace* la función, el nombre está mal.
- **Nombres revelan intención, en español** (D20): tipos PascalCase, métodos camelCase,
  constantes `UPPER_SNAKE` **con unidades** (`TIMEOUT_LLM_MS`, `TAM_BLOQUE_DESCARGA`,
  `UMBRAL_BACKOFF_MS`), estados en `enum class`, códigos de error `UPPER_SNAKE`.
- **Sin dependencias ocultas**: nada de globals mutables compartidos; la configuración y
  los recursos (ruta del modelo, gateway, logger) se **inyectan por constructor**. Un módulo
  no "lee el config" por su cuenta: recibe lo que necesita.
- **Fronteras delgadas y puras**: el parseo de la solicitud JSON y la escritura de la
  respuesta viven en `main`/capa de transporte; el núcleo trabaja con tipos C++.
  Traducción JSON↔tipos en un solo lugar (`Nucleo/`), no dispersa.
- **Errores que viajan, no que se tragan**: `catch` solo para normalizar (→ §9); ningún
  `catch (...) {}` vacío; toda excepción que llega al main termina en respuesta `ok:false`
  **y** en el registro persistente (D29).
- **`switch` exhaustivo con `default` que loguee/lance** (§5) — el compilador es el
  primer linter.
- **Sin código muerto**: si un patrón queda "diseñado pero no implementado" (circuit
  breaker), se documenta en este documento, **no** se deja código sin usar en el árbol.
- **Funciones cortas > herencia**: composición; las interfaces existen solo donde hay más
  de una implementación real o testeable (gateway, transporte).
- **Comentarios que explican el porqué**, citando el patrón/proyecto de origen cuando
  corresponda (`// patrón de descarga de SAI: .part + rename atómico`), nunca el qué —
  respetando "sin referencias de gestión" de §5 (referencias técnicas permitidas,
  de gestión no).
- **Config validada fail-fast**: `validar()` devuelve **todos** los problemas en un solo
  error (no el primero); reglas por modo (LOCAL exige `hfRepo`/`hfFile`; REMOTE exige
  `baseUrl`, con `apiKey` opcional solo para localhost). Origen: `LlmConfig.validate()` de
  FunshiEngineAI + `validate.ts` de SAI.
- **Defaults razonables y config autocurativa**: campos con defaults, tolerancia a campos
  desconocidos al leer (upgrades), reescritura completa al guardar.
- **Estado que se puede inspeccionar**: todo estado relevante (modo, modelo, vivo/no,
  progreso, últimas excepciones) es legible desde `llm.status`/`errores.list` — la
  observabilidad es parte del diseño, no un afterthought.

## 9. Sistema de excepciones (D28, D29)

### 9.1 Jerarquía

Excepciones **tipadas para el dominio de Silvy**, en español, con código semántico:

```cpp
// src/Nucleo/Errores.h
enum class CodigoError {   // UPPER_SNAKE, con KDoc de cuándo aplica
    SIN_CUOTA, CLAVE_INVALIDA, TIMEOUT, NO_CONFIGURADO, MODELO_NO_DISPONIBLE,
    CONTEXTO_AGOTADO, DESCARGA_FALLIDA, SERVIDOR_CAIDO, CANCELADO,
    SOLICITUD_INVALIDA,  // JSON malformado, campos faltantes, apiVersion desconocida
    SIN_SALUD,           // reservado para el circuit breaker futuro
    DESCONOCIDO
};

class ErrorSilvy : public std::runtime_error {
    CodigoError codigo_;
    bool reintentable_;          // la decisión viaja en el error, no se parsea texto
    std::optional<ErrorSilvy> causa_;   // cadena de causa (envoltura de otra excepción)
    // + timestamp, contexto (comando, modo, modelo) para el registro persistente
};
```

- **`ErrorConfig : ErrorSilvy`** — acumula **todos** los problemas de validación en un
  solo mensaje multilínea (`"Configuración inválida:\n - …"`), nunca falla por el primero.
- Subtipos con su `reintentable` por defecto donde aporta claridad
  (`ErrorTimeout`, `ErrorCancelado`, `ErrorServidorCaido`); lo esencial es que **todo
  error sabe si es reintentable sin interpretar el mensaje** (herencia directa de SAI y
  FunshiEngineAI).

### 9.2 Normalización: todo error crudo pasa por clasificadores

Punto único de entrada para errores ajenos (no controlados por nosotros):

| Clasificador | Entrada | Sale como |
|---|---|---|
| `desdeHttp(status, cuerpo, esLocal)` | respuesta HTTP no-2xx | código mapeado por status; en el 400, *keyword-sniffing* (`context`, `kv cache`, `tokens`) para distinguir `CONTEXTO_AGOTADO`; snippet de ~300 chars del cuerpo en el mensaje |
| `desdeRed(excepcion, esLocal)` | fallo de transporte | `SERVIDOR_CAIDO` (local) / `MODELO_NO_DISPONIBLE` (remoto), siempre reintentable |
| `envolver(excepcion)` | cualquier `std::exception` no identificada | `DESCONOCIDO` con causa; `std::timeout`→`TIMEOUT`, cancelación→`CANCELADO` |

Regla dura: **timeout ≠ cancelación**. `CANCELADO` nunca es reintentable ni debe mostrarse
como fallo (la canceló el humano/UI).

### 9.3 Errores accionables

Todo mensaje de error se redacta para quien lo reciba **ya pensando en la acción**:
- Fallas de arranque del servidor incluyen el **tail del log** del hijo (últimas ~12 líneas
  / 4 KB) y las opciones concretas (`llamaServerPath`, `buildFromSource`, PATH).
- Fallas de descarga indican bytes recibidos/total y que se reanudará desde el `.part`.
- Errores de config listan todos los campos con problema y su rango esperado.

### 9.4 Registro persistente de excepciones (D29) — la parte nueva

Ni SAI ni FunshiEngineAI persisten errores a disco (solo logs de proceso y estado de UI);
Silvy sí, porque el usuario pide **consultar lo que salió mal** y **verlo cuando algo
falle**:

- **Dónde**: `<casa de Silvy>/errores/` — archivo `errores.jsonl` (append-only, una
  excepción por línea JSON) más un `errores-<fecha>.jsonl` por rotación.
- **Qué se guarda** (objeto por excepción):
  ```jsonc
  { "id": "err-…", "ts": "2026-10-18T12:34:56.789Z", "codigo": "SERVIDOR_CAIDO",
    "mensaje": "…", "reintentable": true,
    "comando": "agent.step", "modo": "LOCAL", "modelo": "phi-4-mini",
    "detalle": { "exitCode": 1, "logTail": "…" },
    "causa": { "codigo": "DESCONOCIDO", "mensaje": "…" } }   // cadena completa
  ```
- **Rotación**: al superar N KB (configurable) se rota con sufijo de fecha; se conservan
  los últimos K archivos; total acotado (la casa de Silvy no crece sin límite).
- **Escritura asíncrona y a prueba de fallos**: la persistencia va por una cola con su
  propio mutex/hilo; si el disco falla, el error de persistencia degrada a log de consola
  **sin** romper la respuesta principal (nunca que un fallo al guardar un error cause otro
  error visible).
- **Consulta desde la CLI/UI**: comando `errores.list` (últimos N, filtro por código/rango
  de fechas) y `errores.get <id>` (detalle con causa) en el protocolo — pensados para la
  UI futura de la CLI. Además, toda respuesta `ok:false` del `agent.step` incluye el
  objeto de error tipado (ya redactado), de modo que **lo que se le muestra al usuario
  cuando algo sale mal es exactamente lo que quedó guardado**.
- **Retención**: TTL configurable (p. ej. 30 días) o por cantidad; se limpia al arrancar.
- **No duplicar logs**: `errores.jsonl` es para *excepciones* (eventos discretos,
  consultables); el log operativo continuo (`logs/silvy.log` con prefijos de subsistema
  `[SILVY][LLM]`, `[SILVY][DESCARGA]`…) queda aparte, como en los proyectos de origen.

### 9.5 Política de captura

- En el borde de cada comando: capturar **una sola vez**, normalizar con §9.2, persistir
  con §9.4, devolver `ok:false` con el error tipado y exit code que corresponda.
- Ninguna excepción se propaga fuera del hilo que la generó sin ser capturada (un
  `std::terminate` por excepción cruzando un hilo sería el peor fallo posible del agente).

## 10. Inspiración: FunshiEngineAI y SAI (D30)

Ambos proyectos fueron analizados a fondo antes de diseñar Silvy. Qué se toma de cada uno:

| Idea | Origen | Sección en Silvy |
|---|---|---|
| Interfaz `LlmGateway` + un solo gateway OpenAI-compatible para local y remoto | FunshiEngineAI (`LlmGateway.kt`) | §7 |
| Errores tipados con `codigo` + `reintentable`, clasificadores `fromHttp/fromNetwork/wrap` | SAI (`errors.ts`) → port a FunshiEngineAI (`LlmErrors.kt`) | §9 |
| Config validada fail-fast con **todos** los problemas listados; reglas por modo | FunshiEngineAI (`LlmConfig.validate`) + SAI (`validate.ts`) | §8, §9.3 |
| Retry con backoff, solo si `reintentable` | Ambos (`retryTransient` / `ApiEngine`) | §7 |
| Descarga GGUF: `.part` + `Range` + rename atómico, lock por archivo, stream de 64 KB (anti-OOM), progreso con throttle | FunshiEngineAI (`HuggingFaceDownloader.kt`), lock idéntico en SAI (`modelDownloadLock.ts`) | §6, §7 |
| Escaneo de ~20 puertos + reserva anti-TOCTOU | SAI (`portPolicy.ts`, ticket B-11) y FunshiEngineAI | §7 |
| Health-check `/health` (200/503), poll 300 ms, timeout de carga 180 s, crash → exit code + logTail | Ambos (`LocalServerManager.kt` / `LlamaCppServerManager.ts`) | §6, §7 |
| Apagado escalonado + kill del árbol de procesos + hook de cierre | Ambos | §6, §7 |
| Pump de stderr en hilo dedicado con tail acotado | FunshiEngineAI (`LlamaManager.runProcess`) | §6 |
| Timeout ≠ cancelación (nunca convertir cancelación en error fatal) | Ambos | §6, §9.2 |
| Registro de peticiones con estado terminal único + TTL (re-hidratar UI) | SAI (`chatRequestRegistry.ts`) | §7 |
| Circuit breaker con EWMA + fallback local↔remoto | SAI (`circuitBreaker.ts`, `LlmOrchestrator.ts`) | §7 (diseñado, no MVP) |
| Degradación tolerante: fallo parcial → warnings, no abortar | FunshiEngineAI (`KnowledgeBase.kt`) | §7 |
| Cola de predicciones con coalescing | SAI (`predictionQueue.ts`) | §7 (futuro, con streaming) |
| Prefijos de log por subsistema `[LLM][…]` | SAI | §9.4 |
| **Registro persistente de excepciones consultable** | **Nuevo en Silvy** — ninguno de los dos lo tiene | §9.4 |

Lo que explícitamente **no** se toma: el RAG de FunshiEngineAI (D6: el conocimiento lo
configura el usuario desde la CLI), ni el motor in-process de SAI (`node-llama-cpp`;
Silvy usa solo `llama-server` externo, "un camino"), siguiendo la misma decisión que ya
tomó FunshiEngineAI tras estudiar SAI.

## 11. Estructura prevista del repo

```
Silvy/
├── README.md               # Qué es, cómo compilar, cómo correr
├── DISENO.md               # Este documento
├── AGENTS.md               # Convenciones para agentes (estilo hermanos)
├── CMakeLists.txt          # CMake, único build soportado
├── config/
│   └── system_prompt.md    # System prompt default (español) — D7/D20
├── src/
│   ├── main.cpp            # Solo argv + lectura de la solicitud (stdin/--request-file)
│   │                       #   y escritura de la respuesta a stdout. Sin lógica.
│   ├── Comandos/           #   Un módulo por dominio de comando (agent.step,
│   │                       #   errores.list, errores.get)
│   ├── Nucleo/             #   Protocolo: Solicitud/Respuesta, dispatch por enum,
│   │                       #   catálogo de comandos
│   ├── Errores/            #   Jerarquía ErrorSilvy + CodigoError, clasificadores
│   │                       #   (§9.1–9.2), registro persistente errores.jsonl (§9.4)
│   │                       #   con rotación + cola de escritura
│   ├── Llm/                #   LlmGateway (interface), ClienteHttp + SSE,
│   │                       #   modos Local/Remoto, tool-calling OpenAI
│   ├── Modelo/             #   Descarga GGUF con reanudación + gestión de llama-server
│   │                       #   (hilo supervisor + pump de stderr, §6)
│   ├── Concurrencia/       #   Primitivas: hilo dedicado con cancelación, cola de
│   │                       #   resultados al principal, flag atómica (§6)
│   └── Config/             #   Config propio de Silvy + validar() fail-fast (§8)
├── tests/                  #   Tests headless (CHECK + ctest), incluye el gateway falso
└── .agentes/               #   Memoria de trabajo (ignorada por git)
```

Reglas de arquitectura que sostienen la estructura (mismo patrón que la CLI):

- `main.cpp` no contiene lógica: solo transporte (stdin/stdout), igual que en FunshiCLI.
- El núcleo no toca stdout: devuelve `Resultado` (respuesta + exit code).
- Comandos registrados en catálogo (`enum class` + mapa de texto + `switch` exhaustivo).
- `apiVersion` en las respuestas: se incrementa al agregar funcionalidad; los campos nuevos
  van al final.

## 12. Hitos

| Hito | Contenido | Criterio de aceptación |
|---|---|---|
| **H0 — Diseño** | Este documento. | Usuario lo revisa y lo da por bueno. |
| **H1 — Esqueleto** | Repo con CMake, `AGENTS.md`, tipos del protocolo, `main` stdin/stdout, jerarquía de excepciones + registro persistente (§9), hilos/colas básicas (§6), tests con **gateway falso**, `config/system_prompt.md`. | `cmake` + `ctest` en verde. Un request JSON de ejemplo produces una respuesta JSON determinista vía gateway falso; una excepción forzada queda en `errores.jsonl` y sale en la respuesta `ok:false`. |
| **H2 — LLM remoto** | Cliente HTTP + SSE contra API compatible OpenAI, tool-calling real, clasificadores de error HTTP/red, retry con backoff. | Contra un mock HTTP local: `agent.step` devuelve toolCalls reales; timeout vs cancelación bien diferenciados; tests en verde. |
| **H3 — LLM local** | Descarga del GGUF con reanudación + lock, arranque/supervisión de `llama-server` (health-check, crash→logTail, apagado escalonado), comandos `llm.status/download/mode`. | Modo LOCAL end-to-end con Phi-4-mini. Recarga tras crash. Descarga cancelable y reanudable. |
| **H4 — Integración CLI** | Comandos `agent.step` + `llm.*` en FunshiEngineCLI, loop del agente en su núcleo, historial + recorte, catálogo de herramientas, aprobaciones (D12). | La CLI (por JSON) da un paso completo con aprobación simulada. |
| **H5 — MVP** | Demo D22 con verificación D21: generar script C++ → `project.src.create` → `script.compile` → `diagnostics.list` → corregir hasta verde, con límite D14. | El script compilado sin diagnósticos en la CLI real. Validación del usuario. |
| **H6 — Extensibilidad** | Skills (P5), system prompt editable desde la config de la CLI, herramientas configuradas por el usuario (RAG, MCP). | Un usuario configura una skill y una tool nueva sin tocar código de Silvy. |
| **Futuro** | Streaming de tokens Silvy→CLI (D16), UI de la CLI, empaquetado con el instalador de 4 componentes. | — |

## 13. Preguntas abiertas (a resolver en el hito indicado)

| # | Pregunta | Hit. |
|---|---|---|
| P3 | Política de recorte/resumen del historial en la CLI (¿cuántos mensajes, qué dispara el resumen?). | H4 |
| P4 | Validación de lo generado según lenguaje: C++ vía `script.compile`; ¿shaders, HTML/CSS/JS cómo se verifican? (D10/D11). | H5–H6 |
| P5 | Mecanismo concreto de skills: formato de archivo, cuándo se inyectan, dónde se configuran. | H6 |
| P6 | Flujo de modificación de código del **motor** (D10): qué herramientas de la CLI cubren tocar `.cpp`/tests/CMake del repo del motor (hoy la CLI opera sobre proyectos). | H6 |
| P7 | Relación con CHATIA a futuro: ¿comparten la capa LLM de Silvy o cada uno lo suyo? | futuro |
| P10 | Concurrencia: confirmar el modelo de hilos de §6 contra el caso real de `agent.step` largo —¿el principal responde otros comandos o serializa todo hasta terminar el paso? y política de descargas múltiples (¿una a la vez o por archivo?). | H1–H3 |
| P11 | Circuit breaker + fallback local↔remoto (§7): ¿se implementa solo si habilitamos modo dual LOCAL↔REMOTO simultáneo, como resolvió FunshiEngineAI, o Silvy lo necesita desde H3? | H3+ |


---

*Documento vivo: se actualiza en el mismo commit del cambio que lo motive (regla de
documentación sincronizada de los repos hermanos).*
