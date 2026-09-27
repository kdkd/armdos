// apps/defrag/tests/disks.mjs - fragmented scratch volumes for the DEFRAG tests.
// Files are written interleaved, some deleted, the holes refilled with bigger
// files (which mtools, like DOS, spreads over the first free clusters),
// directories grown late so their own chains fragment, a hidden+system file
// dropped in the middle (unmovable), files replaced by bigger versions.
import path from 'node:path';
import { mt, rng } from './fat.mjs';

const data = (r, n, tag) => {
  const b = Buffer.alloc(n);
  let x = (r() * 0xFFFFFFFF) >>> 0;
  for (let i = 0; i < n; i++) { x = (Math.imul(x, 1103515245) + 12345) >>> 0; b[i] = x >>> 24; }
  Buffer.from(tag).copy(b, 0);
  return b;
};

/** model: Map DOS path -> content */
export function fragment(img, dir, { seed = 1, scale = 1, count = 40 } = {}) {
  const r = rng(seed), model = new Map();
  const size = (lo, hi) => Math.floor((lo + r() * (hi - lo)) * scale);
  const dirs = ['', 'SUB1\\', 'SUB1\\DEEP\\', 'DOCS\\'];
  let ops = [['md', 'SUB1'], ['md', 'SUB1\\DEEP']];
  // round 1: many small files interleaved over the directories
  for (let i = 0; i < count; i++) {
    const n = `${dirs[i % 3]}R${String(i).padStart(3, '0')}.DAT`;
    ops.push(['put', n, data(r, size(300, 7000), n)]);
  }
  img = mt(img, dir, ops, model);
  // delete every third, drop an unmovable hidden system file into the space
  ops = [];
  for (let i = 0; i < count; i += 3) ops.push(['del', `${dirs[i % 3]}R${String(i).padStart(3, '0')}.DAT`]);
  ops.push(['put', 'LOCKED.SYS', data(r, size(1500, 3000), 'LOCKED')], ['attr', 'LOCKED.SYS', '+h'], ['attr', 'LOCKED.SYS', '+s']);
  ops.push(['md', 'DOCS']);
  // round 2: bigger files fill the holes (fragmented)
  for (let i = 0; i < count / 2; i++) {
    const n = `${dirs[(i % 2) ? 1 : 3]}B${String(i).padStart(3, '0')}.BIN`;
    ops.push(['put', n, data(r, size(3000, 26000), n)]);
  }
  img = mt(img, dir, ops, model);
  // round 3: delete some, "append" (replace with a longer version), one big file
  ops = [];
  let k = 0;
  for (const n of [...model.keys()]) {
    if (n.startsWith('LOCKED')) continue;
    if (k % 5 === 1) ops.push(['del', n]);
    else if (k % 7 === 2) { const old = model.get(n); ops.push(['put', n, Buffer.concat([old, data(r, size(2000, 12000), 'APPEND')])]); }
    k++;
  }
  ops.push(['put', 'BIG.ZZZ', data(r, size(60000, 90000), 'BIG')]);
  for (let i = 0; i < 12; i++) ops.push(['put', `SUB1\\LATE${i}.TXT`, data(r, size(100, 900), 'LATE')]);
  img = mt(img, dir, ops, model);
  // round 4: a few more deletions leave holes between the files
  ops = [];
  k = 0;
  for (const n of [...model.keys()]) if (!n.startsWith('LOCKED') && k++ % 6 === 3) ops.push(['del', n]);
  img = mt(img, dir, ops, model);
  return { img, model };
}
