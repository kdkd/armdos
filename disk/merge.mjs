#!/usr/bin/env node
// Merge a base disk manifest with per-app fragments: apps/*/<name>.json
// (name = hd, floppy, floppy-games, floppy-utils, ...). A fragment may hold
//   files: [...], tree: [...], dirs: [...]   - appended to the base
//   excludeFromDos: ["FOO.EXE"]                - kept out of the catch-all build/*.EXE -> DOS\ entries
// so that each app only ever edits its own directory.
import fs from 'node:fs';
import path from 'node:path';
const [base, out] = process.argv.slice(2);
const name = path.basename(base, '.json');
const m = JSON.parse(fs.readFileSync(base, 'utf8'));
m.files ??= []; m.tree ??= []; m.dirs ??= [];
const excl = [];
const appsDir = 'apps';
const skipApps = (process.env.MERGE_SKIP || '').split(/[ ,]+/).filter(Boolean);   // e.g. MERGE_SKIP=gem for a publish build
const frags = fs.existsSync(appsDir) ? fs.readdirSync(appsDir).sort().filter((a) => !skipApps.includes(a))
  .map((a) => path.join(appsDir, a, name + '.json')).filter((f) => fs.existsSync(f)) : [];
for (const f of frags) {
  const fr = JSON.parse(fs.readFileSync(f, 'utf8'));
  m.files.push(...(fr.files ?? []));
  m.tree.push(...(fr.tree ?? []));
  for (const d of fr.dirs ?? []) if (!m.dirs.includes(d)) m.dirs.push(d);
  excl.push(...(fr.excludeFromDos ?? []));
}
// MERGE_EXCLUDE=GEM.EXE,DESKTOP.EXE keeps a skipped app's build outputs out of the catch-all DOS\ rule
excl.push(...(process.env.MERGE_EXCLUDE || '').split(/[ ,]+/).filter(Boolean));
for (const e of m.files) if (typeof e.src === 'string' && /^build\/\*\.(EXE|COM|SYS)$/.test(e.src)) e.exclude = [...(e.exclude ?? []), ...excl];
// the merged manifest lives in build/, so its base must still point at the project root
m.base = path.relative(path.dirname(out), path.resolve(path.dirname(base), m.base ?? '.')) || '.';
fs.mkdirSync(path.dirname(out), { recursive: true });
const text = JSON.stringify(m, null, 1);
if (!fs.existsSync(out) || fs.readFileSync(out, 'utf8') !== text) fs.writeFileSync(out, text);
console.log(`merged ${base} + ${frags.length} fragment(s) -> ${out}`);
