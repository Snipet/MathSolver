# Acceptance-test battery

`cases.tsv` is a data-only battery of 120 end-to-end cases for the `mathsolver`
CLI, derived from DESIGN.md. **No harness exists yet** — wiring a runner (shell
or C++) happens in a later phase; nothing here is referenced by
`tests/CMakeLists.txt` on purpose. Every expected value was cross-checked
numerically (exact arithmetic via Python `fractions`, identities and
derivatives sampled at multiple points / central differences, roots by
high-precision bisection).

## File format

Tab-separated, one case per line. Lines starting with `#` (and blank lines)
are comments. Columns:

| # | column        | meaning                                                            |
|---|---------------|--------------------------------------------------------------------|
| 1 | `id`          | unique case id (`par-*`, `sim-*`, `exp-*`, `fac-*`, `dif-*`, `sol-*`, `num-*`, `ev-*`, `lx-*`, `err-*`) |
| 2 | `subcommand`  | one of `simplify expand factor solve diff eval latex`               |
| 3 | `input`       | the expression/equation argument (contains no tabs)                 |
| 4 | `extra_args`  | space-separated further CLI args, or `-` for none                   |
| 5 | `expected_kind` | `exact` \| `regex` \| `contains` \| `approx`                      |
| 6 | `expected`    | expectation (see below); `-` means "check the exit code only"       |
| 7 | `expects_exit`| expected exit code: `0` success, `1` parse/math error, `2` usage error |

## How a harness should run a case

```sh
mathsolver <subcommand> "<input>" <extra_args...>   # omit extra_args when "-"
```

- Compare against **stdout** when `expects_exit` is `0`, against **stderr**
  when it is non-zero (error diagnostics go to stderr per DESIGN.md §10).
- The exit code must equal `expects_exit` in every case.
- `expected` of `-` (used only for error cases whose exact message wording is
  not pinned by the design) means: assert the exit code, ignore the text.

## Matching semantics per `expected_kind`

- **exact** — the captured stream, with trailing whitespace/newline stripped,
  must equal `expected` byte-for-byte.
- **contains** — `expected` must occur as a literal substring of the stream.
- **regex** — Python-flavored regular expression; match with
  `re.search(pattern, stream, re.MULTILINE | re.DOTALL)` (or equivalent:
  `^`/`$` anchor individual lines, `.` crosses newlines). Several solve cases
  use lookaheads like `(?=.*^x = 2$)(?=.*^x = 3$)` to require multiple
  solution lines regardless of their order — a POSIX-ERE-only runner would
  need to special-case those.
- **approx** — `expected` is one decimal number (or a comma-separated list).
  The harness extracts every numeric token from the stream (pattern
  `[-+]?\d+\.?\d*([eE][-+]?\d+)?`) and each expected value must be within
  tolerance of **at least one** extracted number (subset semantics: extra
  numbers in the output, e.g. from `method:`/warning lines or additional
  roots, are fine). Tolerance convention:

  ```
  |actual - expected| <= 1e-6 * max(1, |expected|)
  ```

  i.e. absolute 1e-6 near zero, relative 1e-6 for large magnitudes. This is
  deliberately looser than the solver's internal `tol = 1e-12` so cases test
  the mathematics, not the printer's digit count.

## Design notes / caveats for the harness author

- `exact` is used only where DESIGN.md pins the output (plain-style printing
  rules and worked examples, e.g. `x^2 + 2*x + 1`, `5*x`, `sqrt(2)`,
  `abs(x)`). Where spacing/ordering is plausible-but-not-pinned (factored
  forms, multi-term derivative results, solution ordering), cases use
  `regex`/`contains`/`approx` instead. If the implementation's formatting
  legitimately differs while remaining design-conformant, prefer widening the
  regex over changing the implementation.
- Guard cases (`sim-09/10/18/26/28/29`, `fac-05/06`) assert that the tool does
  **not** rewrite: expected equals the (canonically printed) input. These are
  as important as the positive cases.
- `sol-16`/`sol-17` and `lx-01` run the same command twice with different
  assertions; a harness may batch identical invocations but must evaluate each
  line independently.
- `num-*` roots were computed to ~1e-12 by bisection; `sin(x) = x/2` has
  additional roots (0 and -1.8954...), which the subset semantics of `approx`
  absorbs.
- Exit-code-2 cases (`err-06`, `err-07`) assume the CLI validates
  `--range LO HI` arity and `name=value` binding syntax as usage errors; if
  the implementation classifies these differently, reconcile with DESIGN.md
  §10 first (usage errors are exit 2 there).
- Inputs are written in a mix of plain ASCII and LaTeX on purpose — both go
  through the same grammar (DESIGN.md §4). Remember to single-quote inputs in
  a shell harness: many contain `\`, `(`, `*`, `^`, `{}`.
