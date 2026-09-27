#!/usr/bin/env node
// Stages the static web site into build/site (see web/README.md).
//
//   node web/tools/build-site.mjs [--out build/site]
//
// * stages css, js, fonts, icons and the emulator into r/<release id>/ (below), and web/docs (the manual) to docs/
// * copies the emulator (emu/*.mjs, emu/dev/*.mjs) to site/emu/*.js, rewriting
//   the relative imports, so no web server needs to know the .mjs MIME type
// * gzips rom.bin / hd.img / floppy images into site/images/*.<hash>.gz (the page
//   inflates them with DecompressionStream) and writes images.json, into the release, with
//   sizes and content hashes (the hash keys the hard disk's saved sectors)
// * turns web/disks.json into the release's disks.json (entries whose image does not
//   exist yet are kept, marked "missing", so the disk box shows them as blanks)
import { readFileSync, writeFileSync, mkdirSync, readdirSync, statSync, existsSync, rmSync, copyFileSync, renameSync } from 'node:fs';
import { join, dirname, basename, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { gzipSync } from 'node:zlib';
import { createHash } from 'node:crypto';
import { makeIcons } from './icons.mjs';
import { makePcFont } from './pcfont.mjs';

const WEB = dirname(dirname(fileURLToPath(import.meta.url)));
const ROOT = dirname(WEB);
const args = process.argv.slice(2);
const arg = (k, d) => { const i = args.indexOf(k); return i >= 0 ? args[i + 1] : d; };
const OUT = resolve(ROOT, arg('--out', 'build/site'));
const BUILD = resolve(ROOT, process.env.BUILD || 'build');

rmSync(OUT, { recursive: true, force: true });
mkdirSync(OUT, { recursive: true });

function copyTree(src, dst, filter = () => true) {
  if (!existsSync(src)) return;
  for (const name of readdirSync(src)) {
    const s = join(src, name), d = join(dst, name);
    if (name.startsWith('.')) continue;
    if (statSync(s).isDirectory()) { copyTree(s, d, filter); continue; }
    if (!filter(s)) continue;
    mkdirSync(dirname(d), { recursive: true });
    copyFileSync(s, d);
  }
}
const sha = (buf) => createHash('sha256').update(buf).digest('hex').slice(0, 16);

// ---- the release directory: everything the page loads (css, js, the emulator, fonts, icons) goes
// under r/<release id>/, the id a hash of all of it (and of index.html, the manifest and sw.js), so
// every URL there is immutable and any cache - the browser's, a CDN's, the service worker's - may
// keep it forever. index.html (and the manifest, and the service worker's name) are the only files
// whose content changes under the same name; they point at the release. The modules import each
// other relatively, so a page and its Web Workers can only ever load one release's files.
const STAGE = join(OUT, 'r', 'staging');
for (const dir of ['css', 'js', 'assets', 'fonts']) copyTree(join(WEB, dir), join(STAGE, dir));
copyTree(join(WEB, 'fonts'), join(OUT, 'fonts'));         // (the manual's own copy: docs/docs.css)
const vgaFont = readFileSync(join(ROOT, 'emu', 'fonts', 'vga8x16.bin'));
makeIcons(join(STAGE, 'icons'), vgaFont);
// the machine's IBM VGA 8x16 face as a web font (web/tools/pcfont.mjs), for the page and the manual
const vgaTtf = makePcFont(vgaFont, { family: 'PC VGA' });
for (const dir of [join(STAGE, 'fonts'), join(OUT, 'fonts')]) writeFileSync(join(dir, 'pcvga.ttf'), vgaTtf);

// ---- emulator modules (.mjs -> .js)
const emuOut = join(STAGE, 'emu');
function copyModules(src, dst) {
  mkdirSync(dst, { recursive: true });
  for (const name of readdirSync(src)) {
    if (!name.endsWith('.mjs') || name === 'headless.mjs' || name === 'testkit.mjs') continue;
    const code = readFileSync(join(src, name), 'utf8')
      .replace(/(from\s+['"]\.{1,2}\/[^'"]+)\.mjs(['"])/g, '$1.js$2')
      .replace(/(import\(\s*['"]\.{1,2}\/[^'"]+)\.mjs(['"])/g, '$1.js$2');
    writeFileSync(join(dst, name.replace(/\.mjs$/, '.js')), code);
  }
}
copyModules(join(ROOT, 'emu'), emuOut);
copyModules(join(ROOT, 'emu', 'dev'), join(emuOut, 'dev'));
copyModules(join(ROOT, 'emu', 'online'), join(emuOut, 'online'));   // ARM-DOS Online (555-0199), apps/online
copyFileSync(join(ROOT, 'emu', 'dev', 'OPL3-LICENSE.txt'), join(emuOut, 'dev', 'OPL3-LICENSE.txt'));   // LGPL-2.1: dev/opl3.js (Nuked OPL3)
mkdirSync(join(emuOut, 'fonts'), { recursive: true });
for (const f of ['vga8x16.bin', 'cga8x8.bin', 'mda9x14.bin', 'README.md']) copyFileSync(join(ROOT, 'emu', 'fonts', f), join(emuOut, 'fonts', f));

// the page and the manifest point into the release ("@R@" until its id is known)
const pageHtml = readFileSync(join(WEB, 'index.html'), 'utf8').replace(/\b(href|src)="(css|js|icons|assets|fonts)\//g, '$1="r/@R@/$2/');
const manifest = readFileSync(join(WEB, 'manifest.webmanifest'), 'utf8').replace(/"src": "icons\//g, '"src": "r/@R@/icons/');
const swTemplate = readFileSync(join(WEB, 'sw.js'), 'utf8');

// ---- the manual (web/docs, plain HTML), linked from the page's header: its stylesheet and pictures
// get their hash in the name, so a cached copy can never be the wrong one for the page
{
  const docs = join(OUT, 'docs');
  copyTree(join(WEB, 'docs'), docs);
  const renamed = new Map();
  for (const f of ['docs.css', ...readdirSync(join(docs, 'img')).map((n) => 'img/' + n)]) {
    const p = join(docs, f), dot = f.lastIndexOf('.');
    const to = `${f.slice(0, dot)}.${sha(readFileSync(p)).slice(0, 10)}${f.slice(dot)}`;
    renameSync(p, join(docs, to)); renamed.set(f, to);
  }
  for (const n of readdirSync(docs)) if (n.endsWith('.html')) {
    const p = join(docs, n);
    writeFileSync(p, readFileSync(p, 'utf8').replace(/\b(href|src)="([^"#?]+)"/g, (m, k, u) => (renamed.has(u) ? `${k}="${renamed.get(u)}"` : m)));
  }
}

// ---- images
const imgDir = join(OUT, 'images');
mkdirSync(imgDir, { recursive: true });
function stageImage(path) {
  const raw = readFileSync(path);
  const h = sha(raw);
  // the hash in the name: a new image is a new URL, so no browser or proxy cache can serve a stale disk
  const name = `${basename(path)}.${h}.gz`;
  writeFileSync(join(imgDir, name), gzipSync(raw, { level: 9 }));
  return { file: 'images/' + name, size: raw.length, sha: h };
}
// The hard disk is streamed: fixed 256 KB chunks of the raw image, each gzipped and named by
// its own content hash (images/c/<hash>.gz), so a chunk that doesn't change between releases
// keeps its URL and stays cached. All-zero chunks are not stored at all (null in the index).
export const CHUNK = 256 * 1024;
function stageChunked(path) {
  const raw = readFileSync(path);
  const dir = join(imgDir, 'c');
  mkdirSync(dir, { recursive: true });
  const chunks = []; let stored = 0, zipped = 0;
  for (let off = 0; off < raw.length; off += CHUNK) {
    const part = raw.subarray(off, Math.min(raw.length, off + CHUNK));
    if (part.every((b) => b === 0)) { chunks.push(null); continue; }
    const h = createHash('sha256').update(part).digest('hex').slice(0, 20);
    const f = join(dir, h + '.gz');
    if (!existsSync(f)) writeFileSync(f, gzipSync(part, { level: 9 }));
    zipped += statSync(f).size; stored++;
    chunks.push(h);
  }
  console.log(`build-site: ${relative(ROOT, path)}: ${chunks.length} chunks of ${CHUNK / 1024} KB, ${stored} stored, ${(zipped / 1048576).toFixed(1)} MB gzipped`);
  return { size: raw.length, sha: sha(raw), chunkSize: CHUNK, chunkBase: 'images/c/', chunks, zipped };
}
const need = (p) => { if (!existsSync(p)) { console.error(`build-site: missing ${relative(ROOT, p)} (run make first)`); process.exit(1); } return p; };
const images = {
  rom: stageImage(need(join(BUILD, 'rom.bin'))),
  hd: stageChunked(need(join(BUILD, 'hd.img'))),
};
// D:, the user's own drive (disk/d.json; web/js/keepdisk.js): only its non-zero sectors, as
// [lba u32 LE][512 bytes] records, gzipped - a few KB, fetched once, when a browser first makes D:
if (existsSync(join(BUILD, 'd.img'))) {
  const raw = readFileSync(join(BUILD, 'd.img')), parts = [];
  for (let lba = 0; lba < raw.length / 512; lba++) {
    const sec = raw.subarray(lba * 512, lba * 512 + 512);
    if (sec.some((b) => b)) { const h = Buffer.alloc(4); h.writeUInt32LE(lba); parts.push(h, sec); }
  }
  const recs = Buffer.concat(parts), h = sha(recs), name = `d.sectors.${h}.gz`;
  writeFileSync(join(imgDir, name), gzipSync(recs, { level: 9 }));
  images.d = { file: 'images/' + name, size: recs.length, sha: h, sectors: raw.length / 512 };
}
// the ARM Pit BBS's hard disk (docs/MODEM.md; built by apps/bbs), booted in a worker on demand
if (existsSync(join(BUILD, 'bbs.img'))) images.bbs = stageImage(join(BUILD, 'bbs.img'));
// the General MIDI sound set (apps/midi/sf, GeneralUser GS derived; emu/dev/gmsynth.mjs): fetched by
// web/js/audio-gm.js only when a program first uses the MPU-401
if (existsSync(join(ROOT, '3rdparty/midi/ARMGS.SFA'))) {
  images.gm = stageImage(join(ROOT, '3rdparty/midi/ARMGS.SFA'));
  copyFileSync(join(ROOT, 'apps/midi/sf/GeneralUser-GS-LICENSE.txt'), join(imgDir, 'ARMGS-LICENSE.txt'));
}
// CD-ROMs (apps/cdrom, `make cdrom-disc`; web/js/cdrom.js): disc.json with the data track
// gzipped and the audio as Opus + MP3 (no WAVs), under images/cd/<id>/ so the service worker
// caches them when first used and never precaches them
images.cdroms = [];
for (const id of ['sampler93']) {
  const dir = join(BUILD, 'cdrom', id);
  if (!existsSync(join(dir, 'disc.json'))) { console.error(`build-site: note: ${relative(ROOT, dir)}/disc.json missing (make cdrom-disc); no CD-ROMs`); continue; }
  const disc = JSON.parse(readFileSync(join(dir, 'disc.json'), 'utf8'));
  const out = join(imgDir, 'cd', disc.id), rel = `images/cd/${disc.id}/`;
  mkdirSync(out, { recursive: true });
  for (const t of disc.tracks) {
    if (t.type === 'data') {
      const raw = readFileSync(join(dir, t.file)), h = sha(raw);
      writeFileSync(join(out, `${t.file}.${h}.gz`), gzipSync(raw, { level: 9 }));
      t.data = { file: `${rel}${t.file}.${h}.gz`, size: raw.length, sha: h };
      delete t.file;
    } else {
      t.audio = {};
      for (const k of ['opus', 'mp3']) {
        if (!t.files?.[k] || !existsSync(join(dir, t.files[k]))) continue;
        const raw = readFileSync(join(dir, t.files[k])), f = t.files[k], dot = f.lastIndexOf('.');
        const named = `${f.slice(0, dot)}.${sha(raw)}${f.slice(dot)}`;
        copyFileSync(join(dir, f), join(out, named));
        t.audio[k] = { file: rel + named, size: raw.length };
      }
      delete t.files;
    }
  }
  images.cdroms.push(disc);
}
// (in the release: the page and its disks can only ever come as a pair - index.html alone decides both)
writeFileSync(join(STAGE, 'images.json'), JSON.stringify(images, null, 1) + '\n');

// ---- the disk box
const box = JSON.parse(readFileSync(join(WEB, 'disks.json'), 'utf8'));
const staged = new Map();
for (const d of box.disks) {
  const p = d.file ? join(ROOT, d.file) : null;
  if (p && existsSync(p)) {
    if (!staged.has(p)) staged.set(p, stageImage(p));
    Object.assign(d, staged.get(p));
  } else {
    if (d.file) console.error(`build-site: note: ${d.file} does not exist yet; "${d.label}" stays an empty sleeve`);
    d.missing = true; delete d.file;
  }
}
writeFileSync(join(STAGE, 'disks.json'), JSON.stringify(box, null, 1) + '\n');

// ---- the release id: a hash of the whole release directory and of index.html, the manifest and sw.js
const releaseFiles = [];
(function walk(d, pre) {
  for (const n of readdirSync(d).sort()) {
    const p = join(d, n);
    if (statSync(p).isDirectory()) walk(p, pre + n + '/'); else releaseFiles.push([pre + n, readFileSync(p)]);
  }
})(STAGE, '');
const RELEASE = (() => {
  const h = createHash('sha256').update(pageHtml).update(manifest).update(swTemplate);
  for (const [f, b] of releaseFiles) h.update(f).update(b);
  return h.digest('hex').slice(0, 12);
})();
renameSync(STAGE, join(OUT, 'r', RELEASE));
// (copies at the top, read by pages from before release directories)
for (const f of ['images.json', 'disks.json']) copyFileSync(join(OUT, 'r', RELEASE, f), join(OUT, f));
writeFileSync(join(OUT, 'index.html'), pageHtml.replaceAll('@R@', RELEASE));
writeFileSync(join(OUT, 'manifest.webmanifest'), manifest.replaceAll('@R@', RELEASE));

// ---- the service worker: sw-<release>.js, which the page registers - a new name for every release,
// so like everything under r/ it never changes and any cache may keep it - plus the same as sw.js
// for pages from before release directories, which registered that. It lists the shell -
// index.html and the release directory - with each file's hash, which it checks as it installs.
{
  const shell = [['index.html', sha(readFileSync(join(OUT, 'index.html')))]];
  for (const [f, b] of releaseFiles) if (!/\.(md|txt)$/.test(f)) shell.push([`r/${RELEASE}/${f}`, sha(b)]);
  const sw = swTemplate.replace("'__BUILD__'", JSON.stringify(RELEASE)).replace('__SHELL__', JSON.stringify(shell));
  writeFileSync(join(OUT, `sw-${RELEASE}.js`), sw);
  writeFileSync(join(OUT, 'sw.js'), sw);
  console.log(`build-site: release ${RELEASE}: ${shell.length} files in the app shell`);
}

let total = 0;
(function du(d) { for (const n of readdirSync(d)) { const p = join(d, n); const s = statSync(p); if (s.isDirectory()) du(p); else total += s.size; } })(OUT);
console.log(`build-site: ${relative(ROOT, OUT)} staged, ${(total / 1024).toFixed(0)} KB`);
