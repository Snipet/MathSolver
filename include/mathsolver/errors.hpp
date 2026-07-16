#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace mathsolver {

/// Base class for every error thrown by MathSolver.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Exact rational arithmetic exceeded the 64-bit range.
class OverflowError : public Error {
public:
    using Error::Error;
};

/// Division by an exactly-zero value (rational arithmetic or 0^negative).
class DivisionByZeroError : public Error {
public:
    using Error::Error;
};

/// Numeric evaluation failed: unbound symbol, domain error (ln of a
/// non-positive value, asin out of [-1,1], even root of a negative, ...),
/// or a non-finite result.
class EvalError : public Error {
public:
    using Error::Error;
};

/// Input text could not be parsed. Carries the half-open byte range
/// [begin, end) of the offending region in the original input so callers
/// can render caret diagnostics.
class ParseError : public Error {
public:
    ParseError(const std::string& message, std::size_t begin, std::size_t end)
        : Error(message), begin_(begin), end_(end) {}

    std::size_t begin() const noexcept { return begin_; }
    std::size_t end() const noexcept { return end_; }

private:
    std::size_t begin_;
    std::size_t end_;
};

} // namespace mathsolver
