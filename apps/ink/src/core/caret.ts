// Render the caret diagnostic for a parse error, byte-for-byte identical to
// caret_diagnostic() in apps/main.cpp. The engine reports [begin, end) as byte
// offsets into the UTF-8 input, so the display is built over UTF-8 bytes:
//
//     error: unknown command '\fraq'
//         \fraq{1}{2} + x
//         ^~~~~

const encoder = new TextEncoder();
const decoder = new TextDecoder();

/** Bytes of the display cell for one source byte (tabs collapse to a space,
 *  control bytes become a visible escape, everything else passes through). */
function cell(b: number): number[] {
  switch (b) {
    case 0x09: // \t
      return [0x20];
    case 0x0a: // \n
      return [0x5c, 0x6e];
    case 0x0d: // \r
      return [0x5c, 0x72];
    case 0x0b: // \v
      return [0x5c, 0x76];
    case 0x0c: // \f
      return [0x5c, 0x66];
    default:
      return [b];
  }
}

export function caretDiagnostic(source: string, message: string, begin: number, end: number): string {
  const bytes = encoder.encode(source);
  const b = Math.min(begin, bytes.length);
  const e = Math.min(Math.max(end, b), bytes.length);

  const echoed: number[] = [];
  let pad = 0; // display spaces spanning [0, begin)
  let markerWidth = 0; // display width of the offending region [begin, end)
  for (let i = 0; i < bytes.length; i++) {
    const c = cell(bytes[i]!);
    echoed.push(...c);
    if (i < b) pad += c.length;
    else if (i < e) markerWidth += c.length;
  }

  let out = `error: ${message}\n    ${decoder.decode(Uint8Array.from(echoed))}\n    ${" ".repeat(pad)}^`;
  if (markerWidth > 1) out += "~".repeat(markerWidth - 1);
  return out;
}
