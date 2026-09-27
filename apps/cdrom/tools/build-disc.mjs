#!/usr/bin/env node
// apps/cdrom/tools/build-disc.mjs - builds the "ARM-DOS Multimedia Sampler '93" disc:
//
//   node apps/cdrom/tools/build-disc.mjs [--out build/cdrom/sampler93] [--root .]
//
// Output (in --out): data.iso (track 1, ISO 9660), tNN.wav (44.1 kHz 16-bit stereo, padded to
// whole 588-frame sectors; for node tests), tNN.opus (Ogg Opus ~96 kbps) and tNN.mp3
// (~128 kbps, Safari fallback) for every audio track, and disc.json (the manifest the
// emulator's ATAPI drive reads; schema in apps/cdrom/README.md).
//
// Audio: every original in 3rdparty/cdrom-music/ (listed in apps/cdrom/disc/music/tracks.json;
// tools/fetch-3rdparty.sh downloads them) is decoded by ffmpeg,
// loudness-normalised (EBU R128 two-pass loudnorm, -16 LUFS, -1.5 dBTP, linear), resampled to
// 44.1 kHz and written as WAV; the Opus and MP3 are encoded from that WAV. Audio is only
// re-made when the original or this script is newer than the outputs (it is slow).
// The ISO is rebuilt every run (a second or so) and only written when its bytes change.
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { buildIso } from './iso9660.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf(n); return i >= 0 ? args[i + 1] : d; };
const ROOT = path.resolve(opt('--root', path.join(HERE, '../../..')));
const OUT = path.resolve(ROOT, opt('--out', 'build/cdrom/sampler93'));
const DISC = path.join(ROOT, 'apps/cdrom/disc');
const MUSIC = path.join(ROOT, '3rdparty/cdrom-music');
fs.mkdirSync(OUT, { recursive: true });
const SELF_MTIME = Math.max(fs.statSync(fileURLToPath(import.meta.url)).mtimeMs, fs.statSync(path.join(DISC, 'music/tracks.json')).mtimeMs);

// ------------------------------------------------------------ UTF-8 -> CP437 text
const CP437_HI = 'ÇüéâäàåçêëèïîìÄÅÉæÆôöòûùÿÖÜ¢£¥₧ƒáíóúñÑªº¿⌐¬½¼¡«»░▒▓│┤╡╢╖╕╣║╗╝╜╛┐└┴┬├─┼╞╟╚╔╩╦╠═╬╧╨╤╥╙╘╒╓╫╪┘┌█▄▌▐▀αßΓπΣσµτΦΘΩδ∞φε∩≡±≥≤⌠⌡÷≈°∙·√ⁿ²■ ';
const toCp437 = (s, what) => {
  s = s.replace(/\r\n/g, '\n').replace(/\n/g, '\r\n');
  const out = new Uint8Array(s.length);
  let n = 0;
  for (const ch of s) {
    const c = ch.codePointAt(0);
    if (c < 0x80) { out[n++] = c; continue; }
    let k = CP437_HI.indexOf(ch);
    if (ch === '·') k = 0xFA - 0x80;         // U+00B7 is CP437 FAh (∙ U+2219 is F9h)
    if (k < 0) throw new Error(`${what}: character ${ch} (U+${c.toString(16)}) is not in code page 437`);
    out[n++] = 0x80 + k;
  }
  return out.subarray(0, n);
};

// ------------------------------------------------------------ audio
const ffmpeg = (a, what) => {
  const r = spawnSync('ffmpeg', ['-hide_banner', '-nostdin', '-y', ...a], { encoding: 'utf8', maxBuffer: 1 << 26 });
  if (r.status !== 0) throw new Error(`ffmpeg failed (${what}):\n${r.stderr.slice(-2000)}`);
  return r.stderr;
};
// the loudness filter works at 192 kHz: back to 44.1 kHz with the SoX resampler when this
// ffmpeg has it (Debian's does), else ffmpeg's own (Homebrew's ffmpeg is built without libsoxr)
const RESAMPLER = /--enable-libsoxr/.test(spawnSync('ffmpeg', ['-hide_banner', '-buildconf'], { encoding: 'utf8' }).stdout || '')
  ? 'resampler=soxr' : 'resampler=swr:filter_size=64:cutoff=0.97';
const newer = (outs, ins) => {
  if (!outs.every((f) => fs.existsSync(f))) return true;
  const o = Math.min(...outs.map((f) => fs.statSync(f).mtimeMs));
  return ins.some((m) => m > o);
};
function wavInfo(file) {
  const b = fs.readFileSync(file);
  if (b.toString('latin1', 0, 4) !== 'RIFF' || b.toString('latin1', 8, 12) !== 'WAVE') throw new Error(`${file}: not a WAV`);
  let o = 12, fmt = null, data = null;
  while (o + 8 <= b.length) {
    const id = b.toString('latin1', o, o + 4), len = b.readUInt32LE(o + 4);
    if (id === 'fmt ') fmt = { ch: b.readUInt16LE(o + 10), rate: b.readUInt32LE(o + 12), bits: b.readUInt16LE(o + 22) };
    if (id === 'data') { data = { off: o + 8, len: Math.min(len, b.length - o - 8) }; break; }
    o += 8 + len + (len & 1);
  }
  return { b, fmt, data };
}
function writeWav(file, pcm) {                  // pcm: Buffer of s16le stereo
  const h = Buffer.alloc(44);
  h.write('RIFF', 0, 'latin1'); h.writeUInt32LE(36 + pcm.length, 4); h.write('WAVE', 8, 'latin1');
  h.write('fmt ', 12, 'latin1'); h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20); h.writeUInt16LE(2, 22);
  h.writeUInt32LE(44100, 24); h.writeUInt32LE(44100 * 4, 28); h.writeUInt16LE(4, 32); h.writeUInt16LE(16, 34);
  h.write('data', 36, 'latin1'); h.writeUInt32LE(pcm.length, 40);
  fs.writeFileSync(file, Buffer.concat([h, pcm]));
}

function makeAudio(t, nn) {
  const src = path.join(MUSIC, t.file);
  if (!fs.existsSync(src)) throw new Error(`${path.relative(process.cwd(), src)} is missing: run tools/fetch-3rdparty.sh`);
  const wav = path.join(OUT, `t${nn}.wav`), opus = path.join(OUT, `t${nn}.opus`), mp3 = path.join(OUT, `t${nn}.mp3`);
  const srcM = Math.max(fs.statSync(src).mtimeMs, SELF_MTIME);
  if (newer([wav, opus, mp3], [srcM])) {
    process.stdout.write(`  track ${+nn}: ${t.title} - measuring loudness ... `);
    const m = ffmpeg(['-i', src, '-af', 'loudnorm=I=-16:TP=-1.5:LRA=11:print_format=json', '-f', 'null', '-'], 'loudnorm pass 1');
    const j = JSON.parse(m.slice(m.lastIndexOf('{'), m.lastIndexOf('}') + 1));
    const ln = `loudnorm=I=-16:TP=-1.5:LRA=11:measured_I=${j.input_i}:measured_TP=${j.input_tp}:measured_LRA=${j.input_lra}:measured_thresh=${j.input_thresh}:offset=${j.target_offset}:linear=true`;
    process.stdout.write(`${j.input_i} LUFS -> -16; decoding ... `);
    const raw = path.join(OUT, `t${nn}.raw`);
    ffmpeg(['-i', src, '-af', `${ln},aresample=44100:${RESAMPLER}`, '-ar', '44100', '-ac', '2', '-f', 's16le', '-acodec', 'pcm_s16le', raw], 'decode');
    let pcm = fs.readFileSync(raw);
    fs.rmSync(raw);
    const frames = pcm.length / 4, padded = Math.ceil(frames / 588) * 588;
    if (padded > frames) pcm = Buffer.concat([pcm, Buffer.alloc((padded - frames) * 4)]);
    writeWav(wav, pcm);
    process.stdout.write('encoding Opus + MP3\n');
    ffmpeg(['-i', wav, '-c:a', 'libopus', '-b:a', '96k', '-vbr', 'on', '-compression_level', '10', '-map_metadata', '-1',
      '-metadata', `title=${t.title}`, '-metadata', `artist=${t.artist}`, '-metadata', `comment=${t.licence} ${t.licenceUrl}`, opus], 'opus');
    ffmpeg(['-i', wav, '-c:a', 'libmp3lame', '-b:a', '128k', '-map_metadata', '-1', '-id3v2_version', '3',
      '-metadata', `title=${t.title}`, '-metadata', `artist=${t.artist}`, '-metadata', `comment=${t.licence} ${t.licenceUrl}`, mp3], 'mp3');
  }
  const w = wavInfo(wav);
  if (w.fmt.rate !== 44100 || w.fmt.ch !== 2 || w.fmt.bits !== 16) throw new Error(`${wav}: not 44.1 kHz 16-bit stereo`);
  const frames = w.data.len / 4;
  if (frames % 588) throw new Error(`${wav}: not whole sectors`);
  return { frames, sectors: frames / 588, files: { wav: path.basename(wav), opus: path.basename(opus), mp3: path.basename(mp3) },
    sizes: { opus: fs.statSync(opus).size, mp3: fs.statSync(mp3).size } };
}

const cfg = JSON.parse(fs.readFileSync(path.join(DISC, 'music/tracks.json'), 'utf8'));
const audio = cfg.tracks.map((t, i) => ({ ...t, number: i + 2, ...makeAudio(t, String(i + 2).padStart(2, '0')) }));
const mmss = (s) => `${Math.floor(s / 60)}:${String(Math.round(s % 60)).padStart(2, '0')}`;
const lastTrack = audio.length + 1;

// ------------------------------------------------------------ the data track
const files = [];
const notes = [];
const addText = (dst, text) => files.push({ path: dst, data: toCp437(text, dst) });
const addFile = (dst, src, optional = false) => {
  const p = path.join(ROOT, src);
  if (!fs.existsSync(p)) {
    if (optional) { notes.push(`skipped ${dst}: ${src} not built`); return false; }
    throw new Error(`missing ${src}`);
  }
  files.push({ path: dst, data: fs.readFileSync(p) });
  return true;
};
const subst = (s) => s
  .replace('@LASTTRACK@', String(lastTrack))
  .replace('@AUDIOCOUNT@', String(audio.length))
  .replace('@TRACKLIST@', audio.map((t) => `  Track ${String(t.number).padStart(2)}  ${t.title.padEnd(28)} ${t.artist.padEnd(16)} ${mmss(t.frames / 44100).padStart(5)}`).join('\n') + '\n')
  .replace('@TRACKCREDITS@', audio.map((t) => `  Track ${t.number}: "${t.title}" by ${t.artist} (incompetech.com)\n` +
    `           Licensed under Creative Commons: By Attribution 4.0 License\n` +
    `           ${t.source.replace('https://', '')}\n`).join('\n') + '\n');

// authored text (UTF-8 in the repo, CP437 + CRLF on the disc)
const TEXT = path.join(DISC, 'text');
const walk = (d, pre = '') => fs.readdirSync(d, { withFileTypes: true }).sort((a, b) => a.name < b.name ? -1 : 1).flatMap((e) =>
  e.isDirectory() ? walk(path.join(d, e.name), pre + e.name + '\\') : [[pre + e.name, path.join(d, e.name)]]);
for (const [dst, src] of walk(TEXT)) addText(dst, subst(fs.readFileSync(src, 'utf8')));
// files copied verbatim (pictures, public-domain texts already in CP437)
for (const [dst, src] of walk(path.join(DISC, 'files'))) files.push({ path: dst, data: fs.readFileSync(src) });

addText('MUSIC\\TRACKS.TXT', 'CD AUDIO TRACKS\n═══════════════\n\n' +
  'Track  1  (data - the files on this disc)\n' +
  audio.map((t) => `Track ${String(t.number).padStart(2)}  ${t.title.padEnd(28)} ${t.artist.padEnd(16)} ${mmss(t.frames / 44100).padStart(5)}`).join('\n') +
  `\n\nTotal playing time ${mmss(audio.reduce((s, t) => s + t.frames / 44100, 0))}.\n\n` +
  'All tracks: Kevin MacLeod (incompetech.com), licensed under Creative Commons:\n' +
  'By Attribution 4.0 License - http://creativecommons.org/licenses/by/4.0/\n' +
  'Play them with CDPLAY (or CDPLAY /R and Ctrl+Alt+C).\n');

// the programs
if (addFile('DEMO\\DEMO.EXE', 'build/DEMO.EXE', true)) addFile('DEMO\\DEMO.NFO', 'apps/demo/DEMO.NFO');
let zork = 0;
for (const n of [1, 2, 3]) {
  if (addFile(`ZORK\\ZORK${n}.EXE`, `build/ZORK${n}.EXE`, true)) zork++;
  addFile(`ZORK\\ZORK${n}.DAT`, `apps/zork/data/ZORK${n}.DAT`);
}
addFile('ZORK\\LICENSE.TXT', 'apps/zork/data/LICENSE.TXT');
addFile('ZORK\\MOJOZORK.TXT', 'apps/zork/src/LICENSE-MOJOZORK.TXT');
if (addFile('ADVENT\\ADVENT.EXE', 'build/ADVENT.EXE', true)) addFile('ADVENT\\LICENSE.TXT', 'apps/advent/src/COPYING');
addFile('ANSI\\ANSI.ART', 'apps/ansi/art/ANSI.ART');
for (const f of fs.readdirSync(path.join(ROOT, 'apps/basic/disk/BASIC')).sort()) addFile(`BASIC\\${f}`, `apps/basic/disk/BASIC/${f}`);
addFile('BASIC\\LICENSE.TXT', 'apps/basic/src/COPYING');
addFile('UTILS\\ARMINFO.EXE', 'build/ARMINFO.EXE', true);

// text files on the disc must be CRLF (the licences in the repo are LF)
for (const f of files) {
  if (/\.(TXT|BAT|NFO|BAS)$/i.test(f.path)) {
    const s = Buffer.from(f.data).toString('latin1');
    if (/[^\r]\n/.test(s) || s.startsWith('\n')) f.data = Buffer.from(s.replace(/\r?\n/g, '\r\n'), 'latin1');
  }
}

const iso = buildIso({
  volumeId: 'SAMPLER93', volumeSetId: 'SAMPLER93', systemId: 'ARM-DOS',
  publisher: 'EUROPA MICRO SYSTEMS INC.', preparer: 'EUROPA MICRO SYSTEMS MULTIMEDIA DIVISION',
  application: 'ARM-DOS ISO 9660 PREMASTERING 1.0',
  copyrightFile: 'COPYRGHT.TXT', abstractFile: 'ABSTRACT.TXT', biblioFile: 'BIBLIO.TXT',
  date: '1993-06-17 12:00:00', files,
});
const isoPath = path.join(OUT, 'data.iso');
if (!fs.existsSync(isoPath) || Buffer.compare(fs.readFileSync(isoPath), Buffer.from(iso)) !== 0) fs.writeFileSync(isoPath, iso);

// ------------------------------------------------------------ the manifest
const disc = {
  id: 'sampler93', title: "ARM-DOS Multimedia Sampler '93", volumeId: 'SAMPLER93', mcn: '0019930617000',
  tracks: [
    { number: 1, type: 'data', file: 'data.iso', sectors: iso.length / 2048 },
    ...audio.map((t) => ({
      number: t.number, type: 'audio', pregap: 150, sectors: t.sectors, frames: t.frames, files: t.files,
      title: t.title, artist: t.artist, licence: t.licence, licenceUrl: t.licenceUrl, source: t.source,
      seconds: Math.round(t.frames / 44100 * 10) / 10,
    })),
  ],
};
const json = JSON.stringify(disc, null, 2) + '\n';
const jp = path.join(OUT, 'disc.json');
if (!fs.existsSync(jp) || fs.readFileSync(jp, 'utf8') !== json) fs.writeFileSync(jp, json);
else fs.utimesSync(jp, new Date(), new Date());            // up to date: touch for make

// ------------------------------------------------------------ CDPLAY.INI (C:\DOS, from apps/cdrom/hd.json)
// CDPLAY's disc database, keyed by the disc ID CDPLAY computes from the TOC
// (cdplay.c disc_id()): ntracks << 24, plus each track's start and the lead-out
// as Red Book frames (LBA + 150), 32 bits.
{
  let lba = 0, id = disc.tracks.length << 24;
  disc.tracks.forEach((t, i) => { if (i) lba += t.pregap; id += lba + 150; lba += t.sectors; });
  id = (id + lba + 150) >>> 0;
  const hex = id.toString(16).toUpperCase().padStart(8, '0');
  const ini = '; CDPLAY.INI - the ARM-DOS CD Player\'s disc database.\n' +
    '; One section per disc, named by its disc ID (made from the table of contents).\n' +
    '; Keys: title, artist, numtracks, and 0=, 1=, ... the titles of tracks 1, 2, ...\n\n' +
    `[${hex}]\ntitle=${disc.title}\nartist=${[...new Set(audio.map((t) => t.artist))].join(', ')}\nnumtracks=${disc.tracks.length}\n` +
    disc.tracks.map((t, i) => `${i}=${t.type === 'data' ? 'Data (the files on this disc)' : t.title}`).join('\n') + '\n';
  const ip = path.join(OUT, 'CDPLAY.INI');
  const bytes = toCp437(ini, 'CDPLAY.INI');
  if (!fs.existsSync(ip) || Buffer.compare(fs.readFileSync(ip), Buffer.from(bytes)) !== 0) fs.writeFileSync(ip, bytes);
}

for (const n of notes) console.log(`  note: ${n}`);
const secs = audio.reduce((s, t) => s + t.frames / 44100, 0);
console.log(`  ${OUT}: data.iso ${(iso.length / 1048576).toFixed(1)} MB (${files.length} files), ${audio.length} audio tracks ${mmss(secs)}, ` +
  `Opus ${(audio.reduce((s, t) => s + t.sizes.opus, 0) / 1048576).toFixed(1)} MB, MP3 ${(audio.reduce((s, t) => s + t.sizes.mp3, 0) / 1048576).toFixed(1)} MB`);
