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

## Compilación (prevista — ver H1 en el diseño)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure
```

## Estado

En fase de diseño: el plan completo y sus decisiones están en
[DISENO.md](DISENO.md). Sin código todavía.

## Licencia

Apache License 2.0 (prevista — igual que el motor y la CLI).
