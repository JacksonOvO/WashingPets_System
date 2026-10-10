cmake_minimum_required(VERSION 3.22)

set(APP_FLASH_OFFSET "0x00004000" CACHE STRING
    "Application offset from the beginning of internal flash")

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME APP)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Keep the App vector-table relocation outside CubeMX-generated files.
target_compile_definitions(stm32cubemx INTERFACE
    USER_VECT_TAB_ADDRESS
    VECT_TAB_OFFSET=${APP_FLASH_OFFSET}U
)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    Core/Src/app_iap.c
    Core/Src/app_flow.c
    Core/Src/app_param.c
    Core/Src/app_tasks.c
    Core/Src/auto_device.c
    Core/Src/auto_process.c
    Core/Src/auto_stage_entry_env.c
    Core/Src/auto_stage_entry.c
    Core/Src/auto_stage_wash_ready.c
    Core/Src/auto_stage_spray.c
    Core/Src/auto_stage_bath.c
    Core/Src/auto_stage_clean.c
    Core/Src/auto_stage_dry.c
    Core/Src/auto_stage_exit_prepare.c
    Core/Src/auto_stage_exit.c
    Core/Src/clean_process.c
    Core/Src/oxygen_sensor.c
    Core/Src/pwm_fan.c
    Core/Src/task_auto.c
    Core/Src/task_clean.c
    Core/Src/task_comm485.c
    Core/Src/task_host232.c
    Core/Src/task_life_support.c
    Core/Src/task_output.c
    Core/Src/task_self_test.c
    Core/Src/task_water_tank.c
    Core/Src/task_watchdog.c
    Core/Src/temperature_module.c
    Core/Src/thyristor.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# Generate raw binary and Intel HEX images after linking the ELF file.
get_filename_component(TOOLCHAIN_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
find_program(APP_OBJCOPY
    NAMES arm-none-eabi-objcopy starm-objcopy
    HINTS "${TOOLCHAIN_BIN_DIR}"
    NO_DEFAULT_PATH
    REQUIRED
)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${APP_OBJCOPY} -O binary
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMAND ${APP_OBJCOPY} -O ihex
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${CMAKE_COMMAND}
            -DINPUT_BIN=${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
            -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DVERSION_HEADER=${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc/app_iap.h
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy_versioned_app_bin.cmake
    BYPRODUCTS
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
            ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    COMMENT "Generating ${CMAKE_PROJECT_NAME}.bin, ${CMAKE_PROJECT_NAME}.hex and versioned APP bin"
)
