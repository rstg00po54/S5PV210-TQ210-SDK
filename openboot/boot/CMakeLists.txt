cmake_minimum_required(VERSION 3.16)
project(OpenBootFirmware LANGUAGES C ASM)
set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(OUT "${CMAKE_CURRENT_BINARY_DIR}/out")
file(MAKE_DIRECTORY "${OUT}")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/sources.txt" SOURCES)
list(TRANSFORM SOURCES PREPEND "${ROOT}/")
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/variants.txt" VARIANTS)
set(ARM_FLAGS -mcpu=cortex-a8 -marm -mfloat-abi=soft)
set(C_FLAGS -Os -ffunction-sections -fdata-sections -Wall -Wextra -Werror -ffreestanding -fno-builtin
    -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables)
foreach(VARIANT IN LISTS VARIANTS)
    string(FIND "${VARIANT}" "|" SEP)
    string(SUBSTRING "${VARIANT}" 0 ${SEP} NAME)
    math(EXPR START "${SEP}+1")
    string(SUBSTRING "${VARIANT}" ${START} -1 DEFINES)
    separate_arguments(DEFINES UNIX_COMMAND "${DEFINES}")
    add_executable(${NAME} start.S ${SOURCES})
    set_target_properties(${NAME} PROPERTIES SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${OUT}")
    target_include_directories(${NAME} PRIVATE "${ROOT}/include")
    target_compile_options(${NAME} PRIVATE ${ARM_FLAGS} ${C_FLAGS} ${DEFINES})
    target_link_options(${NAME} PRIVATE ${ARM_FLAGS} -nostdlib
        "-Wl,--gc-sections,-T,${CMAKE_CURRENT_SOURCE_DIR}/iram.ld,-Map,${OUT}/${NAME}.map")
    set_property(TARGET ${NAME} APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/iram.ld")
    add_custom_command(TARGET ${NAME} POST_BUILD
        COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:${NAME}>" "${OUT}/${NAME}.bin"
        VERBATIM)
endforeach()

add_executable(ddr_app ddr_app.S)
set_target_properties(ddr_app PROPERTIES SUFFIX ".elf" RUNTIME_OUTPUT_DIRECTORY "${OUT}")
target_compile_options(ddr_app PRIVATE ${ARM_FLAGS})
target_link_options(ddr_app PRIVATE ${ARM_FLAGS} -nostdlib
    "-Wl,-T,${CMAKE_CURRENT_SOURCE_DIR}/ddr_app.ld,-Map,${OUT}/ddr_app.map")
set_property(TARGET ddr_app APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ddr_app.ld")
add_custom_command(TARGET ddr_app POST_BUILD
    COMMAND "${CMAKE_OBJCOPY}" -O binary "$<TARGET_FILE:ddr_app>" "${OUT}/ddr_app.bin"
    VERBATIM)
