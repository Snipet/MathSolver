// The in-app help screen. Mirrors print_repl_help() in apps/main.cpp, trimmed
// to what the terminal app supports (session assignments are not modelled).

import { line, type OutLine } from "./outline.js";

const HELP: [string, OutLine["tone"]][] = [
  ["Enter a bare expression to simplify it, or an equation to solve it.", "normal"],
  ["Commands (arguments separated by top-level commas):", "normal"],
  ["  solve <equation>[, <variable>]        solve; also x^2 < 4 (inequalities)", "muted"],
  ["  solve <eq>; <eq>[; ...][, vars...]    linear system", "muted"],
  ["  diff <expression>[, <variable>]       derivative", "muted"],
  ["  integrate <expr>[, <var>[, <lo>, <hi>]]", "muted"],
  ["  eval <expression>, x=1[, y=2 ...]     numeric evaluation", "muted"],
  ["  subs <expression>, x=y+1[, z=2 ...]   substitution", "muted"],
  ["  simplify / expand / factor / cancel / together / latex <expr>", "muted"],
  ["  collect / apart <expression>[, <variable>]", "muted"],
  ["  laplace / ilaplace <expression>[, <variable>]", "muted"],
  ["  dsolve <ode>[, y(0)=v, y'(0)=v, ...]  solve an IVP", "muted"],
  ["  series <expression>[, <var>[, <center>[, <order>]]]", "muted"],
  ["  limit <expression>, <variable>, <point>[, left|right]", "muted"],
  ["  mlimit <expr>, <x>, <a>, <y>, <b>     2-D limit by path sampling", "muted"],
  ["  sum / product <term>, <var>, <lo>, <hi>   (hi may be inf)", "muted"],
  ["  rsolve <recurrence>[, a(0)=v, ...]    closed form of a recurrence", "muted"],
  ["  fit <x,y; x,y; ...> [| <model> [<degree>]]   least-squares regression", "muted"],
  ["  stats <v1, v2, v3, ...>               exact summary statistics", "muted"],
  ["  seq <a0>, <a1>, <a2>, <a3>[, ...]     recognize the pattern", "muted"],
  ["  stirling [<var>[, <terms>]]           ln Gamma asymptotics", "muted"],
  ["  grad / div / curl / laplacian / jacobian / hessian <field>, <vars...>", "muted"],
  ["  factor <n>   gcd / lcm <a, b, ...>   isprime / nextprime / divisors / totient <n>", "muted"],
  ["  cfrac <rational | sqrt(n) | real>    continued fraction + convergents", "muted"],
  ["  mod <a, m>   powmod <b, e, m>   modinv <a, m>   crt <r..; m..>", "muted"],
  ["", "normal"],
  ["  clear    clear the screen        help    this screen        quit / exit", "muted"],
  ["Add --latex on the command line to render results as LaTeX.", "normal"],
];

export function helpLines(): OutLine[] {
  return HELP.map(([text, tone]) => line(text, tone));
}
