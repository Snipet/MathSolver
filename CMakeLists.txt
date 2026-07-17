cmake_minimum_required(VERSION 3.24)

project(MathSolver
    VERSION 0.3.0
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
)
target_include_directories(mathsolver_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(mathsolver_core PRIVATE mathsolver_warnings)

add_executable(mathsolver apps/main.cpp)
target_link_libraries(mathsolver PRIVATE mathsolver_core mathsolver_warnings)

option(MATHSOLVER_BUILD_TESTS "Build MathSolver unit tests" ON)
if(MATHSOLVER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
