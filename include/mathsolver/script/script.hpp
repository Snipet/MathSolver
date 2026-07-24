#pragma once

// Umbrella header for the MathSolver script layer.
//
// The script layer sits ABOVE the computer-algebra engine and is the only
// place that knows a console session exists: values, `:=` bindings, and the
// shape of an input line. The engine below it stays pure — see
// docs/proposals/console-language.md for the design and DESIGN.md §13 for the
// boundary this library exists to enforce.

#include "mathsolver/script/environment.hpp"
#include "mathsolver/script/statement.hpp"
#include "mathsolver/script/value.hpp"
