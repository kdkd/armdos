// Byte-Sized Trivia through the BBS (door 3): one full game of ten questions,
// answered from QUESTION.TXT by reading the choices off the caller's screen.
import fs from 'node:fs';
import path from 'node:path';
import { ROOT } from '../../term/tests/lib.mjs';

function parseQuestions() {
  const qs = []; let cur = null;
  for (const l of fs.readFileSync(path.join(ROOT, 'apps/doors/data/trivia/QUESTION.TXT'), 'latin1').split(/\r?\n/)) {
    if (l.startsWith('Q ')) { if (!cur || cur.answers.length) { cur = { answers: [], right: '' }; qs.push(cur); } }
    else if (l.startsWith('* ') || l.startsWith('- ')) { cur.answers.push(l.slice(2)); if (l[0] === '*') cur.right = l.slice(2); }
  }
  return qs;
}

export default async function ({ say, expect, settle, shot, tail, since, drive, back, check, readFile, bbs, v }) {
  const qs = parseQuestions();
  const rightLetter = () => {
    const m = {};
    for (const l of v.lines()) { const r = /^\s+\(([A-D])\) (.*?)\s*$/.exec(l); if (r) m[r[1]] = r[2]; }
    const texts = Object.values(m), q = qs.find((q) => q.answers.length && q.answers.every((a) => texts.includes(a)));
    return q ? Object.keys(m).find((k) => m[k] === q.right) : null;
  };
  await say('3\r'); check(await expect('Opening door'), 'door 3: Byte-Sized Trivia opens');
  const atMenu = () => /\[P H R Q\].*$/s.test(tail(60));
  check(await drive([[/Press \[Enter\].*$/s, '\r']], atMenu, 20), 'trivia menu');
  await say('P');
  let right = 0;
  for (let i = 1; i <= 10; i++) {
    if (!await expect(`Question ${i} of 10`)) break;
    await expect('Your answer');
    const k = rightLetter();
    if (k) right++;
    if (i === 3) { await settle(); await shot('door-trivia'); }
    await say(k || 'A');
    await expect('Press [Enter]');
    await say('\r');
  }
  check(right === 10 && await expect('GAME OVER'), `a full game over the phone line (${right}/10 right)`);
  await settle(); await shot('door-trivia-over');
  await drive([[/Press \[Enter\].*$/s, '\r']], atMenu, 10);
  await say('Q');
  check(await drive([[/Press \[Enter\].*$/s, '\r']], back, 20), 'quit trivia: back at the BBS');
  const dat = readFile(bbs, 'BBS\\DOORS\\TRIVIA\\TRIVIA.DAT')?.toString('latin1') || '';
  check(dat.split(/\r?\n/).some((l) => l.startsWith('Susan Oyelaran|')), 'TRIVIA.DAT on the BBS disk has the caller');
  await say('\r'); await expect('Door # to open');
}
