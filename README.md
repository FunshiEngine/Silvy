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

Requisitos: CMake 3.15+, compilador C++17 y git (FetchContent trae `nlohmann/json` con
tag fijo). En Windows/MSYS2 usar el toolchain de `mingw64`:

```bash
export PATH=/mingw64/bin:$PATH
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

La primera configuración necesita red para clonar la dependencia. `config/` se copia junto
al binario como prompt de sistema empaquetado.

## Uso

```bash
build/Silvy --request-file solicitud.json
build/Silvy --request < solicitud.json
build/Silvy --home C:/ruta/a/mi-home --request-file solicitud.json
```

La respuesta JSON sale por stdout (una línea). Códigos de salida: `0` éxito, `1` fallo
operacional, `2` solicitud inválida.

### Comandos

- `agent.step` — un paso del agente: historial completo, catálogo de herramientas,
  prompt de sistema opcional y `maxIterations` como referencia para la CLI.
- `errores.list` — últimos errores persistidos; `args` admite `limite` y `codigo`.
- `errores.get` — detalle de un error por id (`args.id`); un id inexistente responde
  `encontrado:false`, no es un fallo.

### Casa de Silvy

Los datos de runtime viven en `silvy-home/` junto al binario. Precedencia:
`--home` > `SILVY_HOME` > junto al binario. El prompt de sistema se resuelve en este
orden: `systemPrompt` del request > `<casa>/system_prompt.md` >
`config/system_prompt.md` junto al binario.

Las excepciones se registran en `<casa>/errores/errores.jsonl` (append-only, con rotación
por tamaño y retención). Toda respuesta `ok:false` devuelve exactamente el objeto que quedó
guardado.

## Estado

Esqueleto (H1) en pie: protocolo JSON de solicitud/respuesta, errores tipados con registro
persistente, hilo trabajador, núcleo con despacho de comandos y tests con un gateway LLM
falso determinista. El gateway real (HTTP + SSE), la descarga del GGUF y `llama-server`
llegan en los hitos siguientes; por eso hoy el binario responde `NO_CONFIGURADO` a
`agent.step`.

## Licencia

Apache License 2.0 — igual que el motor y la CLI.
