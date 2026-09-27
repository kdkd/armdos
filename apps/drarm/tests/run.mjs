#!/usr/bin/env node
// apps/drarm/tests/run.mjs - SAY.EXE and DOCTOR.EXE (Dr. ARMitso) on ARM-DOS with
// the machine's audio recorded:
//  * SAY speaks test sentences: the audio is analysed (not silent, pitch in the
//    male robot range, pitch settings move it, question rises / statement
//    falls, speaking rate, F1/F2 formant tracks move), stdin input, /PC
//    (PC speaker) returns cleanly with the BIOS tick still running;
//  * DOCTOR: the intro, the name spelled aloud letter by letter, the greeting,
//    a scripted conversation (replies checked), the commands, the swearing ->
//    ARM DATA ABORT -> recovery gag, QUIT back to the prompt.
// Screenshots and WAV samples go to build/drarm-test/.
import fs from 'node:fs';
import { B, makeImage, writeWav, RATE } from '../../sbtest/tests/audio.mjs';
import { boot } from '../../../emu/testkit.mjs';
import { FS, to11k, rms, span, pitchTrack, median, formantTracks, stdev } from './analyse.mjs';

const OUT = B('drarm-test');
let failures = 0;
const check = (ok, what, extra = '') => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${what}${extra ? '  ' + extra : ''}`); if (!ok) failures++; };
for (const f of ['rom.bin', 'IO.SYS', 'ARMDOS.SYS', 'COMMAND.COM', 'SAY.EXE', 'DOCTOR.EXE', 'bootsect.bin'])
  if (!fs.existsSync(B(f))) { console.log(`build/${f} missing`); process.exit(2); }
fs.mkdirSync(OUT, { recursive: true });
fs.writeFileSync(`${OUT}/stdin.txt`, 'This text comes from a file.\r\nIt has two sentences.\r\n');

const hd = makeImage(OUT, 'drarm', [
  { src: 'build/SAY.EXE', dst: 'DOS\\SAY.EXE' },
  { src: 'build/DOCTOR.EXE', dst: 'DOS\\DOCTOR.EXE' },
  { src: 'apps/drarm/data/ADVICE.TXT', dst: 'DOS\\ADVICE.TXT' },
  { src: 'build/drarm-test/stdin.txt', dst: 'STDIN.TXT' },
]);
// boot with the audio captured; remember where each rendered chunk starts
// (the emulator renders the SB16 in chunks, one per run slice)
const chunks = [], bounds = [];
let total = 0;
const pc = await boot({ rom: B('rom.bin'), hd, audio: { rate: RATE, speaker: true, onAudio: (l, r) => { chunks.push(l); bounds.push(total); total += l.length; } } });
pc.audio = () => {
  pc.machine.audio.pump();
  const L = new Float32Array(total); let o = 0;
  for (const c of chunks) { L.set(c, o); o += c.length; }
  return { L, R: L };
};
/** sample-to-sample jumps in [a, b) (44.1 kHz capture), apart from the samples
 * next to the renderer's chunk boundaries */
function jumps(a, b) {
  const { L } = pc.audio();
  b = Math.min(b, L.length);
  const near = new Uint8Array(b - a);
  for (const x of bounds) for (let k = x - 6; k <= x + 6; k++) if (k >= a && k < b) near[k - a] = 1;
  let inner = 0, edge = 0;
  for (let i = a + 1; i < b; i++) { const d = Math.abs(L[i] - L[i - 1]); if (near[i - a]) edge = Math.max(edge, d); else inner = Math.max(inner, d); }
  return { inner, edge };
}
const underruns = () => { const m = [...pc.debug.matchAll(/SPEAK underruns=(\d+)/g)]; return m.length ? m.map((x) => +x[1]) : null; };
check(pc.waitText('C:\\>', { timeoutMs: 30000 }), 'booted to C:\\>');
pc.type('CLS\r'); pc.waitIdle();

const audioFrom = (t0, t1) => { const { L } = pc.audio(); return L.subarray(Math.round(t0 * RATE / 1000), Math.round(t1 * RATE / 1000)); };
const prompted = () => { const l = pc.lines().map((s) => s.trimEnd()).filter((s) => s); return l.length && l[l.length - 1] === 'C:\\>'; };

/** run a DOS command that speaks; returns the audio (11025 Hz) and the time it took */
function run(cmd, file) {
  pc.type('CLS\r'); pc.until(prompted, { timeoutMs: 5000 });
  pc.type(cmd + '\r');
  pc.run(50 * cmd.length + 100);
  const t0 = pc.timeMs;
  const ok = pc.until(prompted, { timeoutMs: 60000, stepMs: 20 });
  const t1 = pc.timeMs;
  const x = audioFrom(t0, t1);
  if (file) writeWav(`${OUT}/${file}`, x, x);
  return { ok, x: to11k(x), secs: (t1 - t0) / 1000, screen: pc.screen() };
}

function analyse(name, r, syllables) {
  const x = r.x, [a, b] = span(x, 0.02);
  const dur = (b - a) / FS;
  const tr = pitchTrack(x.subarray(Math.max(0, a), b + 1));
  const voiced = tr.filter((f) => f.f0 > 0).map((f) => f.f0);
  const f0 = median(voiced);
  const { F1, F2 } = formantTracks(x.subarray(Math.max(0, a), b + 1), tr);
  console.log(`     "${name}": ${dur.toFixed(2)} s of speech, rms ${rms(x.subarray(a, b)).toFixed(3)}, median F0 ${f0.toFixed(0)} Hz, ` +
    `${voiced.length} voiced frames, F1 ${median(F1)}+-${stdev(F1).toFixed(0)} F2 ${median(F2)}+-${stdev(F2).toFixed(0)} Hz, ${(syllables / dur).toFixed(1)} syll/s`);
  return { dur, tr, voiced, f0, F1, F2 };
}

// ---- SAY ----
const s1 = run('SAY The quick brown fox jumps over the lazy dog.', 'say-fox.wav');
check(s1.ok && !/Invalid|Bad command/.test(s1.screen), 'SAY ran and returned to the prompt');
const a1 = analyse('fox', s1, 11);
check(a1.dur > 1.5 && rms(s1.x) > 0.01, 'SAY produced sound', `${a1.dur.toFixed(2)} s`);
check(a1.f0 >= 64 && a1.f0 <= 110, 'pitch in the low male robot range (64-110 Hz)', `${a1.f0.toFixed(0)} Hz`);
check(a1.voiced.length > 60, 'mostly voiced speech');
const rate1 = 11 / a1.dur;
check(rate1 > 2.5 && rate1 < 5, 'speaking rate 2.5-5 syllables/s', rate1.toFixed(2));
check(stdev(a1.F2) > 150 && stdev(a1.F1) > 40, 'formant tracks present and moving (F1, F2)',
  `F1 sd ${stdev(a1.F1).toFixed(0)}, F2 sd ${stdev(a1.F2).toFixed(0)} Hz`);
check(median(a1.F1) > 250 && median(a1.F1) < 800 && median(a1.F2) > 900 && median(a1.F2) < 2200, 'F1/F2 in vowel ranges');

const s2 = run('SAY Hello, what is your name?', 'say-question.wav');
const a2 = analyse('question', s2, 6);
const s3 = run('SAY Please tell me more about your problems.', 'say-statement.wav');
const a3 = analyse('statement', s3, 9);
const endPitch = (a) => { const v = a.tr.filter((f) => f.f0); return median(v.slice(-6).map((f) => f.f0)); };
const midPitch = (a) => median(a.voiced);
check(endPitch(a2) > midPitch(a2) + 5, 'a question ends rising', `${endPitch(a2).toFixed(0)} vs ${midPitch(a2).toFixed(0)} Hz`);
check(endPitch(a3) < midPitch(a3) - 5, 'a statement ends falling', `${endPitch(a3).toFixed(0)} vs ${midPitch(a3).toFixed(0)} Hz`);

const hi = analyse('pitch 8', run('SAY /P:8 Tell me more about your problems.', 'say-pitch8.wav'), 9);
const lo = analyse('pitch 2', run('SAY /P:2 Tell me more about your problems.', 'say-pitch2.wav'), 9);
check(hi.f0 > a3.f0 + 10 && lo.f0 < a3.f0 - 5, '/P moves the pitch', `${lo.f0.toFixed(0)} < ${a3.f0.toFixed(0)} < ${hi.f0.toFixed(0)} Hz`);
const fast = analyse('speed 9', run('SAY /S:9 Tell me more about your problems.'), 9);
check(fast.dur < a3.dur * 0.8, '/S:9 speaks faster', `${fast.dur.toFixed(2)} s vs ${a3.dur.toFixed(2)} s`);
const num = analyse('numbers', run('SAY It costs $4.50, 15% of 1,234 in 1991.', 'say-numbers.wav'), 30);
check(num.dur > 4, 'numbers are read out in words', `${num.dur.toFixed(2)} s`);
const st = run('SAY < STDIN.TXT', 'say-stdin.wav');
const ast = analyse('stdin', st, 13);
check(st.ok && ast.dur > 2, 'SAY reads standard input');
{
  // PC speaker ("RealSound" PWM through PIT channel 2). The emulator's speaker
  // model only renders square waves, so this checks the plumbing: the program
  // speeds up PIT channel 0 for the utterance's length, keeps the BIOS clock
  // ticking, and puts both channels back.
  const pit = pc.machine.pit;
  pc.type('CLS\r'); pc.until(prompted, { timeoutMs: 5000 });
  const tick = () => pc.cpu.m32[0x46C >> 2];
  pc.type('SAY /PC This is the PC speaker.\r');
  pc.run(400);
  const t0 = pc.timeMs, k0 = tick();
  let sawFast = false;
  const ok = pc.until(() => { if (pit.ch[0].reload < 200) sawFast = true; return prompted(); }, { timeoutMs: 20000, stepMs: 5 });
  const secs = (pc.timeMs - t0) / 1000, ticks = tick() - k0;
  check(ok && secs > 1.0, 'SAY /PC (PC speaker) plays for the length of the sentence and returns', `${secs.toFixed(2)} s`);
  check(sawFast && pit.ch[0].reload === 0x10000, 'PIT channel 0 ran at the sample rate (divisor 108) and was restored', `${pit.ch[0].reload}`);
  check(Math.abs(ticks - secs * 18.2) < 4, 'the BIOS clock kept ticking at 18.2 Hz meanwhile', `${ticks} ticks in ${secs.toFixed(2)} s`);
  pc.type('ECHO TICK\r'); pc.run(500);
  check(pc.hasText('TICK'), 'the machine runs normally afterwards');
}

// ---- DOCTOR ----
pc.type('CLS\r'); pc.until(prompted, { timeoutMs: 5000 });
const tDoc = pc.timeMs;
pc.type('DOCTOR\r');
check(pc.waitText('WHAT IS YOUR NAME?', { timeoutMs: 20000 }), 'DOCTOR: title and name prompt');
check(pc.hasText('DR. ARMITSO') && pc.hasText('ARM Intelligent Text-to-Speech Operator'), 'DOCTOR: the banner');
pc.until(() => false, { timeoutMs: 800 });
await pc.png(`${OUT}/doctor-intro.png`);
// the name, slowly, one letter at a time: each letter is spoken
const tName = pc.timeMs;
for (const c of 'CHEKHOV') { pc.type(c); pc.run(700); }
const nameAudio = to11k(audioFrom(tName, pc.timeMs));
writeWav(`${OUT}/doctor-name-spelled.wav`, audioFrom(tName, pc.timeMs), audioFrom(tName, pc.timeMs));
{
  // count separate bursts of sound (letters)
  let bursts = 0, on = false, quiet = 0;
  const w = Math.round(0.01 * FS);
  for (let i = 0; i + w < nameAudio.length; i += w) {
    const r = rms(nameAudio.subarray(i, i + w));
    if (r > 0.02) { if (!on) bursts++; on = true; quiet = 0; } else if (++quiet > 8) on = false;
  }
  check(bursts === 7, 'each typed letter of the name is spoken (7 letters, 7 bursts)', `${bursts}`);
}
check(pc.hasText('CHEKHOV'), 'the name is shown as typed');
pc.type('\r');
const tGreet = pc.timeMs;
check(pc.waitText('HELLO CHEKHOV, MY NAME IS DR. ARMITSO.', { timeoutMs: 20000 }), 'greeting with the name');
check(pc.waitText('TROUBLING YOU?', { timeoutMs: 60000 }), 'the introduction finishes');
pc.run(800);
writeWav(`${OUT}/doctor-intro.wav`, audioFrom(tDoc, pc.timeMs), audioFrom(tDoc, pc.timeMs));
await pc.png(`${OUT}/doctor-greeting.png`);
{
  const x = to11k(audioFrom(tGreet, pc.timeMs));
  const a = analyse('introduction', { x }, 60);
  check(a.dur > 10, 'the introduction is spoken', `${a.dur.toFixed(1)} s`);
  // no dropouts or crackle in a long utterance: every sample-to-sample step is
  // one the 3.8 kHz-limited voice can make (the capture's chunk edges are
  // reported separately: the emulator holds the last sample there)
  const j = jumps(Math.round(tGreet * RATE / 1000), Math.round(pc.timeMs * RATE / 1000));
  check(j.inner < 0.15 && j.edge < 0.35, 'no discontinuities in the spoken introduction', `max step ${j.inner.toFixed(3)} (at the renderer's chunk edges: ${j.edge.toFixed(3)})`);
}

// words appear in step with the voice: shortly after a line starts, only part of it is on screen
/** type a line, wait for the reply to finish; returns the new screen lines */
function talk(text, until, timeoutMs = 30000) {
  pc.type(text + '\r');
  const ok = pc.until(() => until ? pc.hasText(until) : false, { timeoutMs, stepMs: 20 });
  pc.run(400);
  return ok;
}
pc.type('I am sad about my job\r');
pc.until(() => pc.hasText('I AM SORRY'), { timeoutMs: 10000, stepMs: 10 });
const partial = pc.lines().some((l) => l.startsWith('I AM SORRY') && !l.includes('SAD.'));
check(partial, 'the reply appears word by word as it is spoken');
check(pc.waitText('I AM SORRY TO HEAR YOU ARE SAD.', { timeoutMs: 10000 }), 'keyword reply (SAD)');
const script = [
  ['I feel lonely', 'DO YOU OFTEN FEEL LONELY?'],
  ['my mother never calls me', 'TELL ME MORE ABOUT YOUR MOTHER.'],
  ['my mother never calls me', 'YOU JUST SAID THAT. IS YOUR KEYBOARD STUCK?'],
  ['I want a new computer', 'I SEE YOU ARE USING AN ARM926. HOW DOES THAT MAKE YOU FEEL?'],
  ['I want a better life', 'WHAT WOULD IT MEAN TO YOU IF YOU GOT A BETTER LIFE?'],
  ['you are a terrible doctor', 'WHAT MAKES YOU THINK I AM A TERRIBLE DOCTOR?'],
  ['qzx jkfw vbnm', 'THAT DOES NOT COMPUTE.'],
  ['', 'SAY SOMETHING, CHEKHOV. MY SPEAKER IS GETTING COLD.'],
  ['whatever', 'I SEE.'],
  ['the cat sat there', 'PLEASE GO ON.'],
  ['ok then', 'EARLIER YOU MENTIONED YOUR JOB. TELL ME MORE ABOUT THAT.'],
  ['do you know dr sbaitso', 'THAT IS MY COUSIN.'],
];
for (const [q, a] of script) check(talk(q, a), `conversation: "${q}"`, a);
await pc.png(`${OUT}/doctor-conversation.png`);

// commands
check(talk('SAY I am a talking computer', 'I AM A TALKING COMPUTER'), 'SAY command repeats the text');
check(talk('HELP', 'QUIT            END THE SESSION'), 'HELP lists the commands');
await pc.png(`${OUT}/doctor-help.png`);
check(talk('PITCH 7', 'PITCH IS NOW 7. HOW DO I SOUND?'), 'PITCH 7');
check(talk('PARAM', 'PITCH 7, SPEED 5, TONE 5, VOLUME 5.'), 'PARAM shows the settings');
check(talk('RESET', 'MY VOICE IS BACK TO NORMAL.'), 'RESET');
check(talk('.READ C:\\DOS\\ADVICE.TXT', 'CALL ME IN THE MORNING.'), '.READ reads a file aloud');
check(talk('.PHON hello world', 'h EH l OW1'), '.PHON shows the phonemes');
check(talk('CLS', 'DR. ARMITSO'), 'CLS');

// swearing: two warnings, then the crash
check(talk('damn this', 'PLEASE WATCH YOUR LANGUAGE.'), 'swearing: first warning');
check(talk('this is crap', 'MIGHT GET A MEMORY FAULT'), 'swearing: second warning');
pc.type('oh shit\r');
check(pc.waitText('ARM DATA ABORT', { timeoutMs: 30000 }), 'swearing: the ARM DATA ABORT crash screen');
pc.waitText('Reloading psychology module', { timeoutMs: 20000 });
await pc.png(`${OUT}/doctor-crash.png`);
check(pc.hasText('r15=0001F3A8') && pc.hasText('CPSR='), 'the crash screen shows the registers');
check(pc.waitText('I HAD A LITTLE BREAKDOWN THERE. WHERE WERE WE?', { timeoutMs: 30000 }), 'the doctor recovers');
check(!pc.hasText('ARM DATA ABORT'), 'the conversation screen is back');
await pc.png(`${OUT}/doctor-recovered.png`);

check(talk('QUIT', 'GOODBYE, CHEKHOV.'), 'QUIT says goodbye');
check(pc.until(prompted, { timeoutMs: 20000 }), 'back at the DOS prompt');
// Ctrl-C / Ctrl-Break leave at once: mid-sentence, at the prompt, and in SAY
{
  const ivt = () => [0x1B, 0x23].map((n) => pc.cpu.m32[n] >>> 0);
  const vec0 = ivt();
  const quietAfter = (ms) => { const t = pc.timeMs; pc.run(ms); const x = to11k(audioFrom(t + 100, pc.timeMs)); return rms(x); };
  const breakTest = (what, keys) => {
    const t0 = pc.timeMs;
    pc.type(keys);
    const ok = pc.until(prompted, { timeoutMs: 3000, stepMs: 10 });
    const secs = (pc.timeMs - t0) / 1000, silence = quietAfter(600);
    check(ok && secs < 1.0 && silence < 0.005, `${what}: back at the prompt at once, speech stopped`, `${secs.toFixed(2)} s, rms after ${silence.toFixed(4)}`);
    check(JSON.stringify(ivt()) === JSON.stringify(vec0), `${what}: INT 1Bh/23h restored`);
  };
  pc.type('CLS\rDOCTOR\r');
  pc.until(() => pc.hasText('DR. ARMITSO, BY'), { timeoutMs: 20000, stepMs: 10 });
  pc.run(300);
  check(!pc.hasText('SYSTEMS.'), 'DOCTOR is mid-sentence');
  breakTest('Ctrl-C during speech', '{CTRL+C}');
  check(pc.hasText('C:\\>') && !pc.hasText('D R') && !pc.lines().some((l) => l.includes('ARM Intelligent')), 'the screen was reset as on QUIT');
  pc.type('CLS\rDOCTOR\r');
  pc.waitText('WHAT IS YOUR NAME?', { timeoutMs: 20000 });
  pc.type('ANN\r');
  pc.waitText('TROUBLING YOU?', { timeoutMs: 60000 }); pc.run(800);
  breakTest('Ctrl-C at the prompt', '{CTRL+C}');
  pc.type('CLS\rDOCTOR\r');
  pc.until(() => pc.hasText('DR. ARMITSO, BY'), { timeoutMs: 20000, stepMs: 10 }); pc.run(300);
  breakTest('Ctrl-Break during speech', '{CTRL+SCROLL}');
  pc.type('CLS\rSAY This is a rather long sentence that will be interrupted before it ends.\r');
  pc.run(2500);
  breakTest('Ctrl-C in SAY', '{CTRL+C}');
  check(pc.hasText('^C'), 'SAY shows ^C');
}
{
  const u = underruns();
  check(u && u.length >= 10 && u.every((v) => v === 0), 'no ring-buffer underruns in any SAY or DOCTOR run', JSON.stringify(u));
}
check(!pc.faults.length, 'no CPU faults', JSON.stringify(pc.faults.slice(0, 3)));
{
  const { L } = pc.audio();
  console.log(`     ${(L.length / RATE).toFixed(1)} s of audio in total; WAVs and PNGs in build/drarm-test/`);
}
console.log(failures ? `${failures} FAILED` : 'all passed');
process.exit(failures ? 1 : 0);
