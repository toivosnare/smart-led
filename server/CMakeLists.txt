cmake_minimum_required(VERSION 3.13)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(PICO_BOARD pico_w)

include(pico-sdk/pico_sdk_init.cmake)

project(smart-led-server)

pico_sdk_init()

add_executable(smart-led-server
    main.c
)

add_compile_definitions(MBEDTLS_CONFIG_FILE=<custom_mbedtls_config.h>)

include_directories(${CMAKE_CURRENT_LIST_DIR})

add_subdirectory(mbedtls EXCLUDE_FROM_ALL)

target_link_libraries(smart-led-server pico_cyw43_arch_lwip_poll mbedcrypto pico_stdlib)

pico_enable_stdio_usb(smart-led-server 1)
pico_enable_stdio_uart(smart-led-server 0)

# create map/bin/hex/uf2 fileserial in addition to ELF.
pico_add_extra_outputs(smart-led-server)
