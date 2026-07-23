// A single line of rendered output plus a semantic tone the UI maps to a
// color. The one-shot/batch printer ignores the tone (except routing errors to
// stderr); the Ink UI colors by it.

export type Tone = "result" | "normal" | "muted" | "warn" | "note" | "error";

export interface OutLine {
  text: string;
  tone: Tone;
}

export const line = (text: string, tone: Tone = "normal"): OutLine => ({ text, tone });
