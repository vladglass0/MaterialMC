/**
 * Translations of the web UI.
 *
 * Mark every user-visible string with `t("Text %1", arg)` or `tn("%n mod(s)", count)` — string literals only,
 * Qt-style placeholders (%1..%9, %n). `scripts/extract-i18n.mjs` collects them at build time; strings that
 * also existed in the Qt Widgets UI carry their Qt contexts, so the backend (`i18n.catalog`) resolves them
 * with the existing Prism Launcher translations. Untranslated strings fall back to the English source.
 */
import { useSyncExternalStore } from "react";
import { bridge } from "../api/bridge";
import keys from "./keys.generated.json";

interface Catalog {
  language: string;
  locale: string;
  strings: Record<string, string>;
  plurals: Record<string, Record<string, string>>;
}

let catalog: Catalog = { language: "en_US", locale: "en", strings: {}, plurals: {} };
let pluralRules = new Intl.PluralRules("en");
let version = 0;
const listeners = new Set<() => void>();

function substitute(text: string, args: ReadonlyArray<string | number>): string {
  if (args.length === 0) return text;
  return text.replace(/%(\d)/g, (match, digit: string) => {
    const value = args[Number(digit) - 1];
    return value === undefined ? match : String(value);
  });
}

/** Translates `source`; `%1`..`%9` are replaced by `args`. */
export function t(source: string, ...args: Array<string | number>): string {
  return substitute(catalog.strings[source] ?? source, args);
}

/** Translates a string with a count: picks the plural form for `n` and replaces `%n`, then `%1`.. by `args`. */
export function tn(source: string, n: number, ...args: Array<string | number>): string {
  let text = source;
  const forms = catalog.plurals[source];
  if (forms) {
    const category = pluralRules.select(n);
    const sample = Object.keys(forms).find((s) => pluralRules.select(Number(s)) === category);
    if (sample !== undefined) text = forms[sample] ?? source;
  } else {
    // English fallback for "(s)" style sources
    text = source.replace(/\(s\)/g, n === 1 ? "" : "s");
  }
  return substitute(text.replace(/%n/g, n.toLocaleString(locale())), args);
}

/** BCP 47 locale of the selected language, for Intl formatters. */
export function locale(): string {
  return catalog.locale || "en";
}

export function language(): string {
  return catalog.language;
}

function notify() {
  version++;
  for (const listener of listeners) listener();
}

/** Re-renders the caller whenever the language changes. Returns a number that changes with the catalog. */
export function useI18nVersion(): number {
  return useSyncExternalStore(
    (listener) => {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },
    () => version,
  );
}

export async function loadTranslations(): Promise<void> {
  if (!bridge.available) return;
  try {
    const result = (await bridge.call("i18n.catalog", { keys })) as Catalog;
    catalog = result;
    try {
      pluralRules = new Intl.PluralRules(result.locale || "en");
    } catch {
      pluralRules = new Intl.PluralRules("en");
    }
    document.documentElement.lang = result.locale || "en";
    notify();
  } catch (error) {
    console.warn("[i18n] could not load translations", error);
  }
}

bridge.on("i18n.changed", () => void loadTranslations());
