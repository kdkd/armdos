// apps/doors/tests/local.mjs - `make doors-test`: every door played locally
// ("DOOR /L") on one headless ARM-PC, from a private C: holding the door and
// its data files in C:\DOOR. The two-machine tests (the BBS answering the
// phone, DOOR.SYS, the door on COM2) are in apps/bbs/tests.
// Each door's checks live in tests/<door>.mjs and export `async (t) => {}`.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { makeDisk, startPC, runAll, has, screen, check, failed, readFile, ROOT } from '../../term/tests/lib.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const rtc = new Date(1989, 10, 4, 21, 30, 0).getTime();

/** Boot a PC whose C:\DOOR holds exe + the files of dataDir (relative to ROOT), cd there. */
async function doorPC(name, exe, dataDir, extra = {}) {
  const files = [{ src: `build/${exe}`, dst: `DOOR\\${exe}` }];
  const abs = dataDir && path.join(ROOT, dataDir);
  if (abs && fs.existsSync(abs))
    for (const f of fs.readdirSync(abs)) if (fs.statSync(path.join(abs, f)).isFile()) files.push({ src: path.join(dataDir, f), dst: `DOOR\\${f}` });
  const img = makeDisk(name, { files, dirs: ['DOOR'], ...extra });
  const pc = await startPC(img, { rtcBaseMs: extra.rtcBaseMs ?? rtc });
  const pcs = [pc];
  const run = (ms, pred) => runAll(pcs, [], ms, 4, pred);
  const until = (text, ms = 20000) => run(ms, () => has(pc, text));
  const say = async (keys, pause = 300) => { pc.type(keys); await run(pause); await run(20000, () => pc.m.typingDone()); };
  check(await until('C:\\>', 30000), `${name}: booted`);
  await say('CD \\DOOR\r');
  return { pc, run, until, say, has: (t) => has(pc, t), screen: () => screen(pc), read: (p) => readFile(pc, p), shot: (f) => pc.shot(f) };
}

const tests = fs.readdirSync(HERE).filter((f) => f.endsWith('.mjs') && f !== 'local.mjs').sort();
const only = process.argv[2];
for (const f of tests) {
  if (only && !f.startsWith(only)) continue;
  const mod = await import(path.join(HERE, f));
  console.log(`---- ${f}`);
  await mod.default({ doorPC, check, rtc });
}
console.log(failed() ? `${failed()} FAILED` : 'all passed');
process.exit(failed() ? 1 : 0);
