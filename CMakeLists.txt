cmake_minimum_required(VERSION 3.20)
project(google_tts_sapi CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
    message(FATAL_ERROR "MSVC is required to build Google TTS SAPI COM driver.")
endif()

# Statically link runtime so users don't need separate MSVC redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

# ---- The SAPI COM DLL ----
add_library(google_tts_sapi SHARED
    src/guids.cpp
    src/catalog.cpp
    src/bridge.cpp
    src/voice.cpp
    src/tokenui.cpp
    src/server.cpp
    src/google_tts_sapi.def
)

target_include_directories(google_tts_sapi PRIVATE src)
target_link_libraries(google_tts_sapi PRIVATE sapi ole32 oleaut32 advapi32 uuid ws2_32 shell32 user32)

set_target_properties(google_tts_sapi PROPERTIES
    OUTPUT_NAME "google_tts_sapi"
    PREFIX ""
)

# ---- The Test Runner ----
add_executable(voice_test
    src/voice_test.cpp
    src/guids.cpp
)
target_include_directories(voice_test PRIVATE src)
target_link_libraries(voice_test PRIVATE sapi ole32 oleaut32 uuid user32)

# ---- The Voice Manager GUI & CLI ----
add_executable(google_tts_config WIN32
    src/config/main.cpp
    src/catalog.cpp
    src/guids.cpp
)
target_include_directories(google_tts_config PRIVATE src)
target_link_libraries(google_tts_config PRIVATE comctl32 urlmon shell32 ole32 advapi32 sapi uuid)
