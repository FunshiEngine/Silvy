cmake_minimum_required(VERSION 3.15)

project(Silvy VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# nlohmann/json (protocolo JSON): header-only, por FetchContent con tag fijo
# para builds reproducibles. SOURCE_SUBDIR apunta a una carpeta que no existe:
# Silvy consume solo el header, así FetchContent no ejecuta el CMake de la
# dependencia (que declara mínimos viejos incompatibles con CMake 4.x).
include(FetchContent)
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR sin-cmake
)
FetchContent_MakeAvailable(nlohmann_json)

add_library(silvy_core STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Comandos/ErroresComandos.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Comandos/PasoAgente.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Config/ConfigLlm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Config/ConfigSilvy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Concurrencia/HiloTrabajador.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Errores/Errores.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Errores/ErroresHttp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Errores/RegistroErrores.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Llm/PeticionLlm.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Nucleo/CatalogoComandos.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Nucleo/NucleoSilvy.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Nucleo/Protocolo.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Nucleo/Respuesta.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Nucleo/Solicitud.cpp
)

target_include_directories(silvy_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_include_directories(silvy_core SYSTEM PUBLIC
    ${nlohmann_json_SOURCE_DIR}/include
)
target_compile_options(silvy_core PRIVATE -Wall -Wextra)

enable_testing()

add_executable(silvy-tests
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/GatewayFalso.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/SilvyTests.cpp
)
target_link_libraries(silvy-tests PRIVATE silvy_core)
target_include_directories(silvy-tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
target_compile_options(silvy-tests PRIVATE -Wall -Wextra)
target_compile_definitions(silvy-tests PRIVATE
    SILVY_DIR_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)
add_test(NAME silvy-tests COMMAND silvy-tests)
# El e2e por subproceso se salta (código 77) si el binario Silvy no está
# junto a las pruebas, por ejemplo en un build parcial.
set_tests_properties(silvy-tests PROPERTIES SKIP_RETURN_CODE 77)

# En Windows con MinGW, las DLLs del runtime no llegan solas al directorio de
# salida: sin ellas el proceso muere al arrancar. Se copian junto al
# ejecutable para que funcione desde cualquier terminal sin preparar el PATH.
function(silvy_copiar_dlls_runtime destino)
    if(WIN32 AND MINGW)
        get_filename_component(_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        foreach(_dll IN ITEMS libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
            if(EXISTS "${_bin}/${_dll}")
                add_custom_command(TARGET ${destino} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            "${_bin}/${_dll}" "$<TARGET_FILE_DIR:${destino}>"
                    COMMENT "Copiando ${_dll} junto a ${destino}")
            endif()
        endforeach()
    endif()
endfunction()

silvy_copiar_dlls_runtime(silvy-tests)

add_executable(Silvy
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)
target_link_libraries(Silvy PRIVATE silvy_core)
target_compile_options(Silvy PRIVATE -Wall -Wextra)
target_compile_definitions(Silvy PRIVATE
    SILVY_VERSION="${PROJECT_VERSION}"
)

# El prompt de sistema default viaja junto al binario: si la casa no tiene
# system_prompt.md, Silvy usa esta copia empaquetada.
add_custom_command(TARGET Silvy POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/config"
            "$<TARGET_FILE_DIR:Silvy>/config"
    COMMENT "Copiando config/ junto a Silvy"
)

silvy_copiar_dlls_runtime(Silvy)
