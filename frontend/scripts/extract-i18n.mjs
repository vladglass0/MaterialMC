// Collects every translatable string of the web UI and writes src/i18n/keys.generated.json.
//
// Strings are marked in the code with t("...") / tn("...", n) (string literals only). For each string the
// Qt translation contexts it had in the launcher's Qt code are attached (i18n/qt-contexts.json), so the
// backend can resolve it with the existing Prism Launcher translations (i18n.catalog).
import { readFileSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { join, relative } from "node:path";

const root = new URL("..", import.meta.url).pathname;
const contexts = JSON.parse(readFileSync(join(root, "i18n/qt-contexts.json"), "utf8"));
const MAX_CONTEXTS = 6;

function* files(dir) {
  for (const name of readdirSync(dir)) {
    const path = join(dir, name);
    if (statSync(path).isDirectory()) yield* files(path);
    else if (/\.(ts|tsx)$/.test(name) && !name.endsWith(".d.ts")) yield path;
  }
}

// t("..."), t('...'), tn("...", ...) — first argument must be a plain string literal.
const CALL = /\b(t|tn)\(\s*("(?:[^"\\\n]|\\.)*"|'(?:[^'\\\n]|\\.)*')/g;
const unquote = (lit) => JSON.parse(lit[0] === "'" ? `"${lit.slice(1, -1).replace(/\\'/g, "'").replace(/"/g, '\\"')}"` : lit);

const keys = new Map();
for (const file of files(join(root, "src"))) {
  const code = readFileSync(file, "utf8");
  for (const m of code.matchAll(CALL)) {
    const source = unquote(m[2]);
    const entry = keys.get(source) ?? { s: source };
    if (m[1] === "tn") entry.n = true;
    const ctx = contexts[source];
    if (ctx) entry.c = ctx.slice(0, MAX_CONTEXTS);
    entry.f ??= relative(root, file);
    keys.set(source, entry);
  }
}
const list = [...keys.values()].sort((a, b) => a.s.localeCompare(b.s)).map(({ f: _f, ...rest }) => rest);
const out = join(root, "src/i18n/keys.generated.json");
const json = JSON.stringify(list) + "\n";
let previous = "";
try {
  previous = readFileSync(out, "utf8");
} catch {
  /* first run */
}
if (previous !== json) writeFileSync(out, json);
const reused = list.filter((k) => k.c).length;
console.log(`i18n: ${list.length} strings (${reused} reuse Prism translations)`);
