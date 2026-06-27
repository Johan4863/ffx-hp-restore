cmake_minimum_required(VERSION 3.16)
project(ffx_hp_restore CXX)

# ─── Ustawienia projektu ───────────────────────────────────────────────────────
# FFX HD Remaster to proces 32-bit (x86)!
# MUSISZ kompilowac jako 32-bit: -A Win32 lub z toolchainem x86.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ─── Target: DLL moda ─────────────────────────────────────────────────────────
add_library(ffx_hp_restore SHARED
    src/hp_restore.cpp
)

# Nazwa pliku wyjsciowego - taka jak w hook.ini Module Loadera
set_target_properties(ffx_hp_restore PROPERTIES
    OUTPUT_NAME   "ffx_hp_restore"
    SUFFIX        ".dll"
    PREFIX        ""
)

# Optymalizacje / flagi
target_compile_options(ffx_hp_restore PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W3 /O2 /GS->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-O2 -Wall>
)

# Linkowanie - tylko CRT i Win32
target_link_libraries(ffx_hp_restore PRIVATE
    kernel32
    user32
)

# Eksport DEF (alternatywa do __declspec, opcjonalne)
# set_target_properties(ffx_hp_restore PROPERTIES LINK_FLAGS "/DEF:${CMAKE_SOURCE_DIR}/exports.def")

message(STATUS "====================================================")
message(STATUS "  FFX HP Restore mod - konfiguracja CMake gotowa")
message(STATUS "  WAZNE: Kompiluj jako x86 (32-bit)!")
message(STATUS "  VS: cmake -A Win32 ..")
message(STATUS "  MinGW: cmake -G \"MinGW Makefiles\" -DCMAKE_C_FLAGS=-m32 ..")
message(STATUS "====================================================")
