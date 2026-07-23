include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
)
FetchContent_MakeAvailable(Catch2)
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

add_executable(mathsolver_tests
    test_smoke.cpp
    test_rational.cpp
    test_expr.cpp
    test_parser.cpp
    test_printer.cpp
    test_evaluator.cpp
    test_simplify.cpp
    test_simplify_review.cpp
    test_derivative.cpp
    test_integrate.cpp
    test_apart.cpp
    test_fit.cpp
    test_stats.cpp
    test_numtheory.cpp
    test_cfrac.cpp
    test_inequality.cpp
    test_discriminant.cpp
    test_trig.cpp
    test_polydiv.cpp
    test_cancel.cpp
    test_together.cpp
    test_solver.cpp
    test_solver_review.cpp
    test_transform.cpp
    test_ode.cpp
    test_series.cpp
    test_special.cpp
    test_limit.cpp
    test_discrete.cpp
    test_vector_calculus.cpp
    test_system.cpp
    test_cli.cpp
    test_plugin_dsp.cpp
    test_plugin_sys.cpp
    test_plugin_linalg.cpp
    test_plugin_pde.cpp
    test_plugin_ie.cpp
    test_plugin_hyb.cpp
    test_plugin_fem.cpp
    test_plugin_prob.cpp
)
target_link_libraries(mathsolver_tests PRIVATE mathsolver_core mathsolver_plugins Catch2::Catch2WithMain)

# The CLI end-to-end tests drive the real binary through popen().
target_compile_definitions(mathsolver_tests
    PRIVATE MATHSOLVER_CLI_PATH="$<TARGET_FILE:mathsolver>"
            # Shared §5 resolution parity vectors (variable-assignment spec
            # §10), consumed here and by tools/web_vars_test.mjs.
            MATHSOLVER_PARITY_VECTORS="${CMAKE_SOURCE_DIR}/tests/resolution_vectors.tsv")
add_dependencies(mathsolver_tests mathsolver)
catch_discover_tests(mathsolver_tests)

# Acceptance battery: run the TSV cases against the real binary via ctest.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
    add_test(NAME acceptance_battery
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/tools/run_acceptance.py
                --binary $<TARGET_FILE:mathsolver>
                ${CMAKE_SOURCE_DIR}/tests/acceptance/cases.tsv
                ${CMAKE_SOURCE_DIR}/tests/acceptance/printer_cases.tsv
                ${CMAKE_SOURCE_DIR}/tests/acceptance/eval_cases.tsv
                ${CMAKE_SOURCE_DIR}/tests/acceptance/v04_cases.tsv)
    set_tests_properties(acceptance_battery PROPERTIES LABELS acceptance TIMEOUT 300)
endif()
