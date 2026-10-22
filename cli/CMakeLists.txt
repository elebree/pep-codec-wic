cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

# pep.exe: command-line encoder and decoder for .pep images
project(pep_cli LANGUAGES CXX)

set(PEP_HEADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(PEP_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../common")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/${config}")
endforeach()

add_executable(pep_cli
    src/pep_cli.cpp
    src/quantize.cpp
    src/quantize.h
)

target_compile_features(pep_cli PRIVATE cxx_std_17)
target_include_directories(pep_cli PRIVATE "${PEP_HEADER_DIR}" "${PEP_COMMON_DIR}")
target_compile_definitions(pep_cli PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(pep_cli PRIVATE /W4 /permissive- /EHsc)
    # Expands wildcards in arguments, e.g. `pep info *.pep`.
    target_link_options(pep_cli PRIVATE wsetargv.obj)
    set_property(TARGET pep_cli PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(pep_cli PRIVATE
    ole32
    uuid
    windowscodecs
)

set_target_properties(pep_cli PROPERTIES
    OUTPUT_NAME "pep"
)
