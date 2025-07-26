cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set the Pico SDK path from environment variable
if(NOT DEFINED PICO_SDK_PATH)
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()

# Include the Pico SDK
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(HitboxCode C CXX ASM)

# Initialize the Pico SDK
pico_sdk_init()

# Add executable
add_executable(HitboxCode
    HitboxCode.c
    usb_descriptors.c
)

# Set program name and version
pico_set_program_name(HitboxCode "HitboxCode")
pico_set_program_version(HitboxCode "0.1")

# Enable/disable stdio over UART/USB
pico_enable_stdio_uart(HitboxCode 0)
pico_enable_stdio_usb(HitboxCode 0)

# Include directories
target_include_directories(HitboxCode PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src
    ${CMAKE_CURRENT_LIST_DIR}/tinyusb/src/class/hid
)

# Link libraries
target_link_libraries(HitboxCode
    pico_stdlib
    pico_cyw43_arch_none
)

# Add extra outputs
pico_add_extra_outputs(HitboxCode)
