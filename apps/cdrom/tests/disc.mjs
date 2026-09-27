#!/usr/bin/env node
// apps/cdrom/tests/disc.mjs - checks the built "ARM-DOS Multimedia Sampler '93" disc:
// the ISO 9660 track reads back (every file byte-identical to its source), the manifest
// is consistent with the files, the WAVs are 44.1 kHz stereo, whole sectors and not
// silent, Opus/MP3 durations match the WAVs (ffprobe), and the text files are CRLF/CP437.
//   node apps/cdrom/tests/disc.mjs [--disc build/cdrom/sampler93]
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { IsoReader } from '../tools/iso9660.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');
const ai = process.argv.indexOf('--disc');
const DIR = path.resolve(ROOT, ai >= 0 ? process.argv[ai + 1] : 'build/cdrom/sampler93');
let fails = 0;
const check = (ok, what) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}`); if (!ok) fails++; return ok; };

const disc = JSON.parse(fs.readFileSync(path.join(DIR, 'disc.json'), 'utf8'));
check(disc.id === 'sampler93' && disc.title === "ARM-DOS Multimedia Sampler '93" && disc.volumeId === 'SAMPLER93' && /^\d{13}$/.test(disc.mcn), 'manifest header (id, title, volumeId, 13-digit MCN)');
check(disc.tracks[0].number === 1 && disc.tracks[0].type === 'data' && disc.tracks.slice(1).every((t, i) => t.number === i + 2 && t.type === 'audio'),
  `track 1 data, tracks 2-${disc.tracks.length} audio`);
check(disc.tracks.length >= 4 && disc.tracks.length <= 7, `${disc.tracks.length - 1} audio tracks (3-6 wanted)`);

// ---- ISO
const isoBuf = new Uint8Array(fs.readFileSync(path.join(DIR, disc.tracks[0].file)));
check(isoBuf.length === disc.tracks[0].sectors * 2048, `data track: ${disc.tracks[0].sectors} sectors = data.iso size`);
const iso = new IsoReader(isoBuf);
check(iso.pvd.volumeId === 'SAMPLER93' && iso.pvd.volumeSpace === disc.tracks[0].sectors, 'PVD volume id and space size');
check(iso.pvd.copyrightFile === 'COPYRGHT.TXT;1' && iso.pvd.abstractFile === 'ABSTRACT.TXT;1' && iso.pvd.biblioFile === 'BIBLIO.TXT;1' &&
  ['COPYRGHT.TXT', 'ABSTRACT.TXT', 'BIBLIO.TXT'].every((f) => iso.lookup(f)), 'copyright/abstract/bibliographic file ids name root files');
check(iso.pvd.created.startsWith('1993'), `creation date ${iso.pvd.created}`);
const all = iso.walk().filter((e) => !e.rec.dir);
// files copied from the tree must be byte-identical to their sources
const sources = {
  'DEMO\\DEMO.EXE': 'build/DEMO.EXE', 'DEMO\\DEMO.NFO': null, 'ZORK\\ZORK1.EXE': 'build/ZORK1.EXE', 'ZORK\\ZORK1.DAT': 'apps/zork/data/ZORK1.DAT',
  'ZORK\\ZORK2.DAT': 'apps/zork/data/ZORK2.DAT', 'ZORK\\ZORK3.DAT': 'apps/zork/data/ZORK3.DAT', 'ADVENT\\ADVENT.EXE': 'build/ADVENT.EXE',
  'UTILS\\ARMINFO.EXE': 'build/ARMINFO.EXE', 'ANSI\\ANSI.ART': 'apps/ansi/art/ANSI.ART',
};
for (const f of fs.readdirSync(path.join(ROOT, 'apps/cdrom/disc/files/PICTURES'))) sources[`PICTURES\\${f}`] = `apps/cdrom/disc/files/PICTURES/${f}`;
for (const f of fs.readdirSync(path.join(ROOT, 'apps/cdrom/disc/files/TEXTS'))) sources[`TEXTS\\${f}`] = `apps/cdrom/disc/files/TEXTS/${f}`;
let same = 0, bad = [];
for (const [dst, src] of Object.entries(sources)) {
  if (!src) continue;
  const p = path.join(ROOT, src);
  if (!fs.existsSync(p)) { console.log(`     (not built: ${src})`); continue; }
  const got = iso.readFile(dst);
  if (got && Buffer.compare(Buffer.from(got), fs.readFileSync(p)) === 0) same++; else bad.push(dst);
}
check(!bad.length, `${same} binary files byte-identical to their sources${bad.length ? ': ' + bad.join(' ') : ''}`);
for (const need of ['README.TXT', 'MENU.BAT', 'CREDITS.TXT', 'MUSIC\\TRACKS.TXT', 'ZORK\\LICENSE.TXT', 'BASIC\\GUESS.BAS', '1.BAT', '8.BAT'])
  if (!iso.lookup(need)) check(false, `missing ${need}`);
check(true, `${all.length} files on the data track`);
// text files: CRLF only, CP437 (no UTF-8 sequences), menu box intact
let crlf = true, utf8 = false;
for (const e of all) {
  if (!/\.(TXT|BAT|NFO|BAS)$/.test(e.path)) continue;
  const s = Buffer.from(iso.readFile(e.rec)).toString('latin1');
  if (/[^\r]\n/.test(s)) { crlf = false; console.log('     LF only:', e.path); }
  if (/[\xC2-\xF4][\x80-\xBF]/.test(s) && !e.path.startsWith('TEXTS')) { const m = /[\xE2][\x80-\xBF][\x80-\xBF]/.test(s); if (m) { utf8 = true; console.log('     UTF-8?', e.path); } }
}
check(crlf, 'every text file on the disc is CRLF');
check(!utf8, 'no UTF-8 left in the text files (CP437)');
const menu = Buffer.from(iso.readFile('MENU.BAT')).toString('latin1').split('\r\n').filter((l) => /^ECHO [\xC9\xBA\xC7\xC8]/.test(l));
check(menu.length >= 10 && menu.every((l) => l.length === menu[0].length), `MENU.BAT box: ${menu.length} lines of ${menu[0].length - 5} columns`);
const credits = Buffer.from(iso.readFile('CREDITS.TXT')).toString('latin1');
check(disc.tracks.slice(1).every((t) => credits.includes(t.title) && credits.includes(t.artist)) && credits.includes('Attribution 4.0'), 'CREDITS.TXT credits every track (CC BY 4.0 attribution)');
// independent reader
if (spawnSync('xorriso', ['-version']).status === 0) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'disc-'));
  const r = spawnSync('xorriso', ['-abort_on', 'FAILURE', '-return_with', 'WARNING', '32', '-indev', path.join(DIR, 'data.iso'), '-find', '/', '-type', 'f'], { encoding: 'utf8' });
  const n = (r.stdout || '').split('\n').filter((l) => l.startsWith("'/")).length;
  check(r.status === 0 && n === all.length, `xorriso lists the same ${n} files, no warnings`);
  fs.rmSync(tmp, { recursive: true, force: true });
}

// ---- audio
const probe = (f) => {
  const r = spawnSync('ffprobe', ['-v', 'error', '-show_entries', 'format=duration:stream=codec_name,sample_rate,channels', '-of', 'json', f], { encoding: 'utf8' });
  return r.status === 0 ? JSON.parse(r.stdout) : null;
};
let lba = disc.tracks[0].sectors;
for (const t of disc.tracks.slice(1)) {
  lba += t.pregap;
  const wav = fs.readFileSync(path.join(DIR, t.files.wav));
  let o = 12, rate = 0, ch = 0, bits = 0, doff = 0, dlen = 0;
  while (o + 8 <= wav.length) {
    const id = wav.toString('latin1', o, o + 4), len = wav.readUInt32LE(o + 4);
    if (id === 'fmt ') { ch = wav.readUInt16LE(o + 10); rate = wav.readUInt32LE(o + 12); bits = wav.readUInt16LE(o + 22); }
    if (id === 'data') { doff = o + 8; dlen = len; break; }
    o += 8 + len;
  }
  const frames = dlen / 4;
  check(rate === 44100 && ch === 2 && bits === 16, `track ${t.number} "${t.title}": WAV 44.1 kHz 16-bit stereo`);
  check(frames === t.frames && t.sectors === Math.ceil(frames / 588) && frames % 588 === 0 && Math.abs(t.seconds - frames / 44100) < 0.06,
    `  frames ${t.frames} = ${t.sectors} sectors x 588, ${t.seconds} s, starts at LBA ${lba}`);
  lba += t.sectors;
  let peak = 0, sum = 0;
  const pcm = new Int16Array(wav.buffer, wav.byteOffset + doff, dlen / 2);
  for (let i = 0; i < pcm.length; i += 7) { const v = Math.abs(pcm[i]); if (v > peak) peak = v; sum += v * v; }
  const rms = Math.sqrt(sum / (pcm.length / 7)) / 32768;
  check(peak < 32767 && rms > 0.05 && rms < 0.5, `  not silent, not clipped: peak ${(peak / 32768).toFixed(3)}, rms ${rms.toFixed(3)}`);
  // loud all the way through: every 10-s window has sound
  let quiet = 0;
  for (let w = 0; w + 441000 * 2 <= pcm.length; w += 441000 * 2) {
    let s = 0; for (let i = w; i < w + 441000 * 2; i += 97) s += Math.abs(pcm[i]);
    if (s / (441000 * 2 / 97) < 100) quiet++;
  }
  check(quiet <= 1, `  no silent 10-s stretches (${quiet})`);
  for (const k of ['opus', 'mp3']) {
    const f = path.join(DIR, t.files[k]);
    const p = probe(f);
    const d = p ? +p.format.duration : NaN;
    const sz = fs.statSync(f).size;
    check(p && Math.abs(d - frames / 44100) < 0.1 && p.streams[0].channels === 2, `  ${k}: ${p?.streams[0].codec_name} ${d.toFixed(3)} s (WAV ${(frames / 44100).toFixed(3)} s), ${(sz / 1048576).toFixed(2)} MB`);
  }
  const osz = fs.statSync(path.join(DIR, t.files.opus)).size;
  check(osz > 1e6 && osz < 4.5e6, `  Opus size ${(osz / 1048576).toFixed(2)} MB within 1-4.5 MB`);
  check(/^https:\/\/incompetech\.com\//.test(t.source) && t.licence === 'CC BY 4.0' && /creativecommons\.org\/licenses\/by\/4\.0/.test(t.licenceUrl), `  licence ${t.licence}, source ${t.source}`);
}
const total = disc.tracks.slice(1).reduce((s, t) => s + t.seconds, 0);
check(total > 12 * 60 && total < 18 * 60, `total audio ${(total / 60).toFixed(1)} min; disc lead-out at LBA ${lba} (${Math.floor((lba + 150) / 4500)}:${String(Math.floor((lba + 150) / 75) % 60).padStart(2, '0')})`);
// the originals match the recorded sha256
const cfg = JSON.parse(fs.readFileSync(path.join(ROOT, 'apps/cdrom/disc/music/tracks.json'), 'utf8'));
const { createHash } = await import('node:crypto');
check(cfg.tracks.every((t) => createHash('sha256').update(fs.readFileSync(path.join(ROOT, '3rdparty/cdrom-music', t.file))).digest('hex') === t.sha256), 'music originals match their recorded sha256');

console.log(fails ? `${fails} FAILED` : 'disc: all passed');
process.exit(fails ? 1 : 0);
