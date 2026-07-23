import assert from "node:assert/strict";
import { test } from "node:test";
import { render } from "ink-testing-library";
import { processLine } from "../src/core/run.js";
import { App } from "../src/ui/App.js";
import { createStub } from "./stub.js";

const tick = (ms = 40) => new Promise((r) => setTimeout(r, ms));

test("processLine: help, quit, and error routing", async () => {
  const { engine } = createStub();

  const help = await processLine(engine, "help");
  assert.ok(help.lines.length > 0);
  assert.equal(help.isError, false);

  const quit = await processLine(engine, "quit");
  assert.equal(quit.control, "quit");

  const usage = await processLine(engine, "grad x^2"); // missing variables
  assert.equal(usage.isError, true);
  assert.equal(usage.lines[0]!.tone, "error");
});

test("processLine renders a bare expression through simplify", async () => {
  const stub = createStub();
  stub.responses.simplify = () => ({ ok: true, plain: "5*x", latex: "5x" });
  const res = await processLine(stub.engine, "2x + 3x");
  assert.deepEqual(
    res.lines.map((l) => l.text),
    ["5*x"],
  );
});

test("interactive app: typing an expression shows the result", async () => {
  const stub = createStub();
  stub.responses.simplify = () => ({ ok: true, plain: "5*x", latex: "5x" });

  const { lastFrame, stdin, unmount } = render(
    <App engine={stub.engine} version="0.6.0" latex={false} />,
  );

  // The banner is visible immediately.
  assert.match(lastFrame() ?? "", /MathSolver 0\.6\.0/);

  stdin.write("2x + 3x");
  await tick();
  stdin.write("\r");
  await tick(120); // allow processLine's deferred engine call + state update

  assert.match(lastFrame() ?? "", /5\*x/);
  unmount();
});
