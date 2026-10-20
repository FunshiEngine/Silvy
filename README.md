# Silvy

**Agente de inteligencia artificial de código para el motor gráfico
[FunshiEngine](../FunshiEngineOpenGL).**

Silvy es el "cerebro" del agente: una capa LLM (local con llama-server/GGUF o remota vía
API compatible OpenAI) expuesta como binario que se consume por **JSON sobre stdin/stdout**
desde la [FunshiEngineCLI](../FunshiEngineCLI). El loop del agente —planificar, ejecutar
herramientas, observar el resultado, iterar— vive en la CLI, y en el futuro su UI humana
alimenta las instrucciones y aprueba las acciones sensibles.

No es un chatbot (ese es CHATIA/FunshiEngineAI) ni una app de escritorio: **Silvy no tiene
UI propia y no trae conocimiento propio** — RAG, skills y system prompt los aporta el
usuario desde la CLI.

```jsonc
// 1 request = 1 paso. Ejemplo: la CLI pide el siguiente turno del agente
{ "command": "agent.step", "messages": [ … ], "tools": [ … ] }
→
{ "apiVersion": 1, "ok": true, "result": { "content": "…", "toolCalls": [ … ] } }
```

## Documentación

- [DISENO.md](DISENO.md) — **diseño y plan completos**: decisiones, arquitectura,
  protocolo, modelo de hilos, patrones de diseño, Clean Code, sistema de excepciones con
  registro persistente, convenciones, hitos y preguntas abiertas. Empezar por ahí.
- [AGENTS.md](AGENTS.md) — convenciones para agentes y humanos que trabajen en el repo.

## Compilación

Requisitos: CMake 3.15+, compilador C++17 y git (FetchContent trae `nlohmann/json` y
`cpp-httplib` con tag fijo). En Windows/MSYS2 usar el toolchain de `mingw64`:

```bash
export PATH=/mingw64/bin:$PATH
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

La primera configuración necesita red para clonar las dependencias. `config/` se copia junto
al binario como prompt de sistema empaquetado. Si el sistema tiene OpenSSL, el cliente HTTP
habla también `https://`; sin él, solo `http://`.

## Uso

```bash
build/Silvy --request-file solicitud.json
build/Silvy --request < solicitud.json
build/Silvy --home C:/ruta/a/mi-home --request-file solicitud.json
```

La respuesta JSON sale por stdout (una línea). Códigos de salida: `0` éxito, `1` fallo
operacional, `2` solicitud inválida.

Desde stdin se atienden **varias** solicitudes seguidas, una por objeto JSON, hasta el
final del flujo: el runtime del LLM local (modelo descargado, descarga en curso y
`llama-server`) vive en el proceso y sobrevive entre pasos. Con `--request-file` se
atiende una sola solicitud.

### Comandos

- `agent.step` — un paso del agente: historial completo, catálogo de herramientas,
  prompt de sistema opcional y `maxIterations` como referencia para la CLI.
- `errores.list` — últimos errores persistidos; `args` admite `limite` y `codigo`.
- `errores.get` — detalle de un error por id (`args.id`); un id inexistente responde
  `encontrado:false`, no es un fallo.
- `llm.status` — estado del LLM local: modo, modelo, servidor vivo y puerto, bytes
  descargados, porcentaje de carga y último error.
- `llm.download` — inicia o reanuda la descarga del GGUF en segundo plano.
- `llm.cancel` — cancela la descarga en curso; el `.part` se conserva.
- `llm.mode` — cambia `LOCAL`↔`REMOTE` (`args.mode`); escribe la configuración
  resultante en `<casa>/config.json` y detiene el servidor local al pasar a `REMOTE`.

### Casa de Silvy

Los datos de runtime viven en `silvy-home/` junto al binario. Precedencia:
`--home` > `SILVY_HOME` > junto al binario. El prompt de sistema se resuelve en este
orden: `systemPrompt` del request > `<casa>/system_prompt.md` >
`config/system_prompt.md` junto al binario.

Las excepciones se registran en `<casa>/errores/errores.jsonl` (append-only, con rotación
por tamaño y retención). Toda respuesta `ok:false` devuelve exactamente el objeto que quedó
guardado.

### Configuración del LLM

El archivo opcional `<casa>/config.json` describe con qué LLM habla Silvy. El `config` del
request pisa los campos presentes; los campos desconocidos se ignoran.

```jsonc
{
  "mode": "REMOTE",                          // LOCAL | REMOTE
  "baseUrl": "https://api.openai.com/v1",    // base de la API compatible OpenAI
  "apiKey": "sk-...",                        // obligatoria en REMOTE salvo en localhost
  "modelName": "phi-4-mini-instruct",
  "temperature": 0.2,
  "timeoutMs": 180000,                       // presupuesto total del paso, incluido el stream
  "connectTimeoutMs": 10000,
  "maxRetries": 2,                           // reintentos solo sobre errores reintentables
  "retryBaseMs": 300,                        // base del backoff exponencial

  "hfBaseUrl": "https://huggingface.co",     // base de descarga del GGUF (LOCAL)
  "hfRepo": "unsloth/Phi-4-mini-instruct-GGUF",
  "hfFile": "Phi-4-mini-instruct-Q4_K_M.gguf",
  "llamaServerPath": "llama-server",         // ruta o nombre en el PATH (LOCAL)
  "contextSize": 8192,                       // contexto pedido al servidor local
  "gpuLayers": 0,                            // capas de GPU (-ngl); 0 = solo CPU
  "serverPort": 8081,                        // puerto preferido del servidor local
  "portScan": 20,                            // cuántos puertos se prueban
  "loadTimeoutMs": 180000                    // presupuesto de carga del modelo
}
```

La configuración se valida al arrancar y en cada paso: si algo falta o es inválido, el
error `NO_CONFIGURADO` lista **todos** los problemas juntos. El modo `REMOTE` habla por
HTTP + SSE con una API compatible OpenAI, con tool-calling real. El modo `LOCAL` descarga
el GGUF a la casa (reanudable, con lock) y levanta `llama-server` como proceso hijo: el
log del servidor queda en `<casa>/logs/llama-server.log` y el modo `LOCAL` usa su puerto
como base de la API. Silvy **no** compila `llama.cpp`: el ejecutable lo aporta el
usuario, por `llamaServerPath` o por el `PATH`.

El GGUF y el modelo pesan: la descarga por defecto (Phi-4-mini Q4_K_M) ronda los 2,4 GB.
La `apiKey` se guarda en `<casa>/config.json` en texto plano, como es habitual en las
herramientas locales.

Los errores del proveedor llegan tipados: `CLAVE_INVALIDA` (401/403), `SIN_CUOTA` (429),
`MODELO_NO_DISPONIBLE` (404 o destino remoto inalcanzable), `CONTEXTO_AGOTADO` (400 por
contexto), `SERVIDOR_CAIDO` (5xx o fallo de red en local), `TIMEOUT` (se agotó el
presupuesto del paso) y `CANCELADO` (lo pidió el humano). Solo los reintentables repiten
petición, con backoff exponencial.

## Estado

H1 (esqueleto) en pie: protocolo JSON de solicitud/respuesta, errores tipados con registro
persistente, hilo trabajador, núcleo con despacho de comandos y tests con un gateway LLM
falso determinista.

H2 (LLM remoto) en pie: cliente HTTP + SSE contra una API compatible OpenAI, tool-calling
real, clasificación de errores por estado y por fallo de transporte, reintentos con backoff
solo sobre errores reintentables y cancelación del paso en curso.

H3 (LLM local) en pie: descarga reanudable del GGUF con lock y verificación de integridad,
proceso hijo y supervisor de `llama-server` (health-check, apagado escalonado, log en
archivo), runtime local dentro del proceso y los comandos `llm.status/download/cancel/mode`.
El modo `LOCAL` real (`agent.step` contra `llama-server`) necesita el ejecutable, que
Silvy no compila.

## Licencia

Apache License 2.0 — igual que el motor y la CLI.
