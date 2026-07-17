// Theme mode store: system / light / dark, persisted, applied via data-theme.

export type ThemeMode = "system" | "light" | "dark";

const KEY = "mathsolver.theme";
const ORDER: ThemeMode[] = ["system", "light", "dark"];

function load(): ThemeMode {
  try {
    const v = localStorage.getItem(KEY);
    return v === "light" || v === "dark" ? v : "system";
  } catch {
    return "system";
  }
}

function apply(mode: ThemeMode) {
  const el = document.documentElement;
  if (mode === "system") el.removeAttribute("data-theme");
  else el.setAttribute("data-theme", mode);
}

class ThemeStore {
  mode = $state<ThemeMode>(load());

  constructor() {
    apply(this.mode);
  }

  cycle() {
    this.mode = ORDER[(ORDER.indexOf(this.mode) + 1) % ORDER.length];
    apply(this.mode);
    try {
      localStorage.setItem(KEY, this.mode);
    } catch {
      /* storage unavailable */
    }
  }
}

export const theme = new ThemeStore();
