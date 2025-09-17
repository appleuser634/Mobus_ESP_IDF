set(WASM3_DIR "${CMAKE_CURRENT_LIST_DIR}/../../third_party/wasm3")
set(WASM3_SRC ${WASM3_DIR}/source)

set(WASM3_SOURCES
    ${WASM3_SRC}/m3_api_libc.c
    ${WASM3_SRC}/m3_api_wasi.c
    ${WASM3_SRC}/m3_bind.c
    ${WASM3_SRC}/m3_code.c
    ${WASM3_SRC}/m3_compile.c
    ${WASM3_SRC}/m3_core.c
    ${WASM3_SRC}/m3_env.c
    ${WASM3_SRC}/m3_exec.c
    ${WASM3_SRC}/m3_function.c
    ${WASM3_SRC}/m3_info.c
    ${WASM3_SRC}/m3_module.c
    ${WASM3_SRC}/m3_parse.c
)

idf_component_register(
    SRCS ${WASM3_SOURCES}
    INCLUDE_DIRS ${WASM3_DIR}/source include
    PRIV_INCLUDE_DIRS ${WASM3_DIR}/source
    REQUIRES nvs_flash spi_flash spiffs Input Display esp_adc
    PRIV_REQUIRES esp_timer esp_system driver freertos
)

# wasm3 expects to know target platform
component_compile_options(-DM3_COMPILER=1)
