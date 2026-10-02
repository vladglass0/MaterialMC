// Regenerates i18n/qt-contexts.json from a Qt Linguist .ts file of the launcher sources:
//
//   lupdate -recursive ../launcher -ts /tmp/launcher.ts && node scripts/qt-contexts.mjs /tmp/launcher.ts
//
// The file maps every source string of the (Prism Launcher) Qt code to the translation contexts it appears in.
// The web UI reuses those contexts at run time so the existing Prism translations apply to identical strings,
// even after the Qt Widgets code that defined them has been removed.
import { readFileSync, writeFileSync } from "node:fs";

const input = process.argv[2];
if (!input) {
  console.error("usage: node scripts/qt-contexts.mjs <file.ts>");
  process.exit(1);
}
const xml = readFileSync(input, "utf8");
const decode = (s) =>
  s
    .replace(/&lt;/g, "<")
    .replace(/&gt;/g, ">")
    .replace(/&quot;/g, '"')
    .replace(/&apos;/g, "'")
    .replace(/&#x([0-9a-f]+);/gi, (_, h) => String.fromCodePoint(parseInt(h, 16)))
    .replace(/&#([0-9]+);/g, (_, d) => String.fromCodePoint(parseInt(d, 10)))
    .replace(/&amp;/g, "&");

// source -> [[context, original source if it differs]]
const map = {};
const add = (source, context, original) => {
  const list = (map[source] ??= []);
  if (!list.some(([c, o]) => c === context && o === original)) list.push(original ? [context, original] : [context]);
};
for (const ctx of xml.matchAll(/<context>\s*<name>([^<]*)<\/name>([\s\S]*?)<\/context>/g)) {
  const name = decode(ctx[1]);
  for (const msg of ctx[2].matchAll(/<message[^>]*>[\s\S]*?<source>([\s\S]*?)<\/source>[\s\S]*?<\/message>/g)) {
    const source = decode(msg[1]);
    add(source, name);
    // "&Remember my choice" -> also reachable as "Remember my choice" (mnemonics mean nothing in the web UI)
    const stripped = source.replace(/&(?=[^&\s])/g, "");
    if (stripped !== source) add(stripped, name, source);
  }
}
const sorted = Object.fromEntries(Object.keys(map).sort().map((k) => [k, map[k]]));
writeFileSync(new URL("../i18n/qt-contexts.json", import.meta.url), JSON.stringify(sorted, null, 0) + "\n");
console.log(`qt-contexts.json: ${Object.keys(sorted).length} strings`);
