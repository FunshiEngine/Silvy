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
#ifndef SILVY_MODELO_DESCARGA_GGUF_H
#define SILVY_MODELO_DESCARGA_GGUF_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace silvy {

// Bloque de escritura del stream: el archivo se baja de a trozos para no
// cargar el modelo entero en memoria.
constexpr std::size_t kTamBloqueDescarga = 64 * 1024;

// Progreso de una descarga en curso.
struct ProgresoDescarga final {
    // Bytes ya escritos en el archivo parcial.
    std::uint64_t bytesRecibidos = 0;
    // Bytes esperados; 0 si el servidor no lo informa.
    std::uint64_t bytesTotales = 0;
    // true si la descarga continuó desde un archivo parcial previo.
    bool reanudada = false;
};

// Datos de una descarga. El destino final y el parcial viven en la misma
// carpeta, para que el rename final sea atómico.
struct OpcionesDescarga final {
    // URL completa del GGUF.
    std::string url;
    // Archivo final (se renombra desde el parcial al terminar).
    std::filesystem::path destino;
    // Archivo parcial: conserva lo descargado para poder reanudar.
    std::filesystem::path parcial;
    // Lock por ruta: dos descargas del mismo archivo no corren a la vez.
    std::filesystem::path lock;
    // Límite de conexión HTTP.
    int timeoutConexionMs = 10000;
    // Presupuesto total de la descarga.
    int timeoutTotalMs = 7200000;
    // Frecuencia mínima entre reportes de progreso.
    int intervaloProgresoMs = 300;
    // Espera máxima por el lock de otro proceso antes de rendirse.
    int esperaLockMs = 30000;
    // Callback de progreso (opcional).
    std::function<void(const ProgresoDescarga&)> alProgresar;
};

// Resultado de una descarga terminada.
struct ResultadoDescarga final {
    std::filesystem::path archivo;
    std::uint64_t bytes = 0;
    // true si el archivo ya estaba completo antes de empezar.
    bool yaEstaba = false;
    // true si se retomó desde un archivo parcial.
    bool reanudada = false;
};

// URL de descarga de un archivo de HuggingFace. La base por defecto es
// huggingface.co; se puede cambiar a un espejo.
std::string urlDeHuggingFace(const std::string& repo,
                              const std::string& archivo,
                              const std::string& base = "https://huggingface.co");

// Cabecera Range para pedir desde un byte: "bytes=1024-".
std::string cabeceraRange(std::uint64_t desde);

// Bytes totales que anuncia una cabecera "Content-Range: bytes 0-99/1234".
std::uint64_t totalDesdeContentRange(const std::string& valor);

// Bytes totales que anuncia una cabecera "Content-Length: 1234".
std::uint64_t totalDesdeContentLength(const std::string& valor);

// Descarga un GGUF con reanudación: escribe en el archivo parcial, sigue
// desde donde quedó si el servidor acepta Range, y renombra al final. Un
// lock por ruta evita que dos descargas del mismo archivo corran a la vez.
class DescargadorGguf final {
public:
    explicit DescargadorGguf(OpcionesDescarga opciones);

    // Descarga el archivo. Lanza ErrorCancelado si se cancela (el parcial
    // se conserva) y ErrorSilvy con DESCARGA_FALLIDA si falla (el parcial
    // también se conserva, para poder reanudar).
    ResultadoDescarga descargar(const std::atomic<bool>& cancelado);

private:
    OpcionesDescarga opciones_;
};

}  // namespace silvy

#endif  // SILVY_MODELO_DESCARGA_GGUF_H
