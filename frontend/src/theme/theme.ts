import { useSyncExternalStore } from "react";
import {
  argbFromHex,
  Blend,
  hexFromArgb,
  Hct,
  MaterialDynamicColors,
  SchemeTonalSpot,
  TonalPalette,
} from "@material/material-color-utilities";

/**
 * Material 3 dynamic color: the whole palette is generated from one seed color (the "tonal spot"
 * scheme, as on Android 12+) and exposed as `--md-sys-color-*` custom properties on <html>.
 * The choice is a per-UI preference, so it lives in localStorage rather than the launcher settings.
 */

export type ThemeMode = "system" | "light" | "dark";

export interface ThemePrefs {
  seed: string;
  mode: ThemeMode;
}

export const DEFAULT_SEED = "#3f8f4a";
export const SEED_PRESETS = ["#3f8f4a", "#2f6fbf", "#6750a4", "#b3261e", "#c26a00", "#00838f", "#8e4585", "#5d6b2f"];

const STORAGE_KEY = "materialmc.theme";

// Status colors outside the M3 core roles, harmonized towards the seed so they fit the palette.
const CUSTOM_COLORS = { success: 0xff3f9b4a, warning: 0xffd29a1e, info: 0xff3b78d8 } as const;

function load(): ThemePrefs {
  try {
    const parsed = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? "null") as Partial<ThemePrefs> | null;
    if (parsed && /^#[0-9a-f]{6}$/i.test(parsed.seed ?? "")) {
      const mode = parsed.mode === "light" || parsed.mode === "dark" ? parsed.mode : "system";
      return { seed: parsed.seed!, mode };
    }
  } catch {
    // Storage unavailable or corrupted: fall back to the defaults.
  }
  return { seed: DEFAULT_SEED, mode: "system" };
}

let prefs = load();
const listeners = new Set<() => void>();
const darkQuery = window.matchMedia("(prefers-color-scheme: dark)");

function isDark(): boolean {
  return prefs.mode === "dark" || (prefs.mode === "system" && darkQuery.matches);
}

function kebab(name: string): string {
  return name.replace(/_/g, "-");
}

function apply() {
  const dark = isDark();
  const seed = argbFromHex(prefs.seed);
  const scheme = new SchemeTonalSpot(Hct.fromInt(seed), dark, 0);
  const vars: string[] = [];

  for (const color of new MaterialDynamicColors().allColors) {
    if (!color || color.name.endsWith("palette_key_color")) continue;
    vars.push(`--md-sys-color-${kebab(color.name)}: ${hexFromArgb(color.getArgb(scheme))};`);
  }

  for (const [name, value] of Object.entries(CUSTOM_COLORS)) {
    const palette = TonalPalette.fromInt(Blend.harmonize(value, seed));
    const [main, on, container, onContainer] = dark ? [80, 20, 30, 90] : [40, 100, 90, 10];
    vars.push(
      `--md-sys-color-${name}: ${hexFromArgb(palette.tone(main))};`,
      `--md-sys-color-on-${name}: ${hexFromArgb(palette.tone(on))};`,
      `--md-sys-color-${name}-container: ${hexFromArgb(palette.tone(container))};`,
      `--md-sys-color-on-${name}-container: ${hexFromArgb(palette.tone(onContainer))};`,
    );
  }

  let style = document.getElementById("md-theme") as HTMLStyleElement | null;
  if (!style) {
    style = document.createElement("style");
    style.id = "md-theme";
    document.head.appendChild(style);
  }
  style.textContent = `:root { color-scheme: ${dark ? "dark" : "light"}; ${vars.join(" ")} }`;
  document.documentElement.dataset.theme = dark ? "dark" : "light";
}

export function initTheme() {
  apply();
  darkQuery.addEventListener("change", () => {
    if (prefs.mode === "system") {
      apply();
      listeners.forEach((l) => l());
    }
  });
}

export function setThemePrefs(next: Partial<ThemePrefs>) {
  prefs = { ...prefs, ...next };
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(prefs));
  } catch {
    // Not persisted; the change still applies to this session.
  }
  apply();
  listeners.forEach((l) => l());
}

function subscribe(listener: () => void) {
  listeners.add(listener);
  return () => listeners.delete(listener);
}

export function useThemePrefs(): ThemePrefs {
  return useSyncExternalStore(subscribe, () => prefs);
}
