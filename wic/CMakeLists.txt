cmake_minimum_required(VERSION 3.20)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

project(pep_wic_codec LANGUAGES CXX)

set(PEP_HEADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${config}" config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/${config}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/${config}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} "${CMAKE_BINARY_DIR}/${config}")
endforeach()

add_library(pep_wic_codec SHARED
    src/pep_wic_codec.cpp
    src/pep_wic_codec.def
)

target_compile_features(pep_wic_codec PRIVATE cxx_std_17)
target_include_directories(pep_wic_codec PRIVATE "${PEP_HEADER_DIR}")
target_compile_definitions(pep_wic_codec PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MSVC)
    target_compile_options(pep_wic_codec PRIVATE /W4 /permissive- /EHsc)
    set_property(TARGET pep_wic_codec PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(pep_wic_codec PRIVATE
    ole32
    oleaut32
    uuid
    windowscodecs
)

set_target_properties(pep_wic_codec PROPERTIES
    OUTPUT_NAME "PepWicCodec"
)
