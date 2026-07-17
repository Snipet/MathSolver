cmake_minimum_required(VERSION 3.24)

project(MathSolver
    VERSION 0.5.0
    DESCRIPTION "A from-scratch computer-algebra system: parse, simplify, differentiate, solve"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MATHSOLVER_SANITIZE "Build with Address + UndefinedBehavior sanitizers" OFF)
if(MATHSOLVER_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

if(EMSCRIPTEN)
    # The whole library relies on C++ exceptions; use native wasm EH.
    add_compile_options(-fwasm-exceptions)
    add_link_options(-fwasm-exceptions)
    # Size tuning (measured 2026-07-16, emcc 6.0.3, all 27 smoke checks pass):
    # -Oz -flto at compile AND link shrinks mathsolver.wasm from 827 KB raw /
    # 306 KB gzip (Release -O3, no link opt) to 549 KB / 233 KB with no
    # measurable slowdown on numeric solve/integrate spot checks. Also tried:
    # -O2 (bigger), -sASSERTIONS=0 / -sFILESYSTEM=0 / -sTEXTDECODER=2 (already
    # defaults at link -Oz — byte-identical output), --closure 1 (broken in
    # this emscripten install), extra wasm-opt strip pass (gzip got larger).
    set(CMAKE_CXX_FLAGS_RELEASE "-Oz -DNDEBUG")
    add_compile_options(-flto)
    add_link_options(-Oz -flto)
endif()

add_library(mathsolver_warnings INTERFACE)
if(MSVC)
    target_compile_options(mathsolver_warnings INTERFACE /W4)
else()
    target_compile_options(mathsolver_warnings INTERFACE -Wall -Wextra -Wpedantic)
endif()

add_library(mathsolver_core STATIC
    src/rational.cpp
    src/expr.cpp
    src/parser.cpp
    src/printer.cpp
    src/evaluator.cpp
    src/simplify.cpp
    src/derivative.cpp
    src/integrate.cpp
    src/solver.cpp
    src/transform.cpp
    src/plugin.cpp
)
target_include_directories(mathsolver_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mathsolver_core PRIVATE mathsolver_warnings)

# Built-in plugins (docs/PLUGINS.md): numeric domain extensions behind the
# uniform command surface in include/mathsolver/plugin.hpp. Hosts that link
# this library call plugins::register_builtin_plugins() at startup.
add_library(mathsolver_plugins STATIC
    plugins/register_builtin.cpp
    plugins/dsp/dsp.cpp
    plugins/dsp/dsp_design.cpp
    plugins/sys/sys.cpp
    plugins/sys/sys_lti.cpp
)
target_link_libraries(mathsolver_plugins PUBLIC mathsolver_core PRIVATE mathsolver_warnings)

if(EMSCRIPTEN)
    # ES6-module WASM build consumed by the Svelte app in web/.
    add_executable(mathsolver_wasm wasm/bindings.cpp)
    target_link_libraries(mathsolver_wasm PRIVATE mathsolver_core mathsolver_plugins mathsolver_warnings)
    set_target_properties(mathsolver_wasm PROPERTIES OUTPUT_NAME "mathsolver")
    target_link_options(mathsolver_wasm PRIVATE
        --bind
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        -sEXPORT_NAME=createMathSolverModule
        -sENVIRONMENT=web,worker,node
        -sALLOW_MEMORY_GROWTH=1
        -sSTACK_SIZE=2097152
    )
else()
    add_executable(mathsolver apps/main.cpp)
    target_link_libraries(mathsolver PRIVATE mathsolver_core mathsolver_warnings)

    option(MATHSOLVER_BUILD_TESTS "Build MathSolver unit tests" ON)
    if(MATHSOLVER_BUILD_TESTS)
        enable_testing()
        add_subdirectory(tests)
    endif()
endif()
