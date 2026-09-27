// Byte-Sized Trivia (TRIVIA.EXE /L): a perfect game (the test looks the right
// answer up in QUESTION.TXT), the new record in TRIVIA.DAT, SCORES.TXT and
// NEWS.TXT, two more games with wrong answers, then the three-a-day limit.
import fs from 'node:fs';
import path from 'node:path';
import { ROOT } from '../../term/tests/lib.mjs';

function parseQuestions() {
  const qs = []; let cur = null;
  for (const l of fs.readFileSync(path.join(ROOT, 'apps/doors/data/trivia/QUESTION.TXT'), 'latin1').split(/\r?\n/)) {
    if (l.startsWith('Q ')) { if (!cur || cur.answers.length) { cur = { q: '', answers: [], right: '' }; qs.push(cur); } cur.q += l.slice(2) + ' '; }
    else if (l.startsWith('* ') || l.startsWith('- ')) { cur.answers.push(l.slice(2)); if (l[0] === '*') cur.right = l.slice(2); }
  }
  return qs;
}

export default async function ({ doorPC, check }) {
  const qs = parseQuestions();
  const t = await doorPC('trivia', 'TRIVIA.EXE', 'apps/doors/data/trivia');
  const choices = () => {
    const m = {};
    for (const l of t.pc.lines()) { const r = /^\s+\(([A-D])\) (.*?)\s*$/.exec(l); if (r) m[r[1]] = r[2]; }
    return m;
  };
  const rightLetter = () => {
    const m = choices(), texts = Object.values(m);
    const q = qs.find((q) => q.answers.every((a) => texts.includes(a)));
    return q ? Object.keys(m).find((k) => m[k] === q.right) : null;
  };

  await t.say('TRIVIA /L\r');
  check(await t.until('A new contestant!'), 'TRIVIA /L: title screen, new contestant');
  check(t.has('B Y T E - S I Z E D'), 'ANSI title banner');
  await t.say('\r');
  check(await t.until('[P H R Q]'), 'trivia menu');
  check(t.has('Questions in the box: 108') && t.has('Games left today: 3'), 'menu: 108 questions, 3 games left today');
  await t.shot('door-trivia-menu.png');
  await t.say('H');
  check(await t.until('THE HALL OF FAME') && t.has('Susan Oyelaran') && t.has('1,745'), 'hall of fame shows the seeded regulars');
  await t.say('\r');
  await t.until('[P H R Q]');

  // ---- game 1: all right
  await t.say('P');
  let found = 0;
  for (let i = 1; i <= 10; i++) {
    if (!check(await t.until(`Question ${i} of 10`), `question ${i} shown`)) break;
    await t.until('Your answer');
    const k = rightLetter();
    if (k) found++;
    if (i === 4) await t.shot('door-trivia-question.png');
    await t.say(k || 'A');
    await t.until('Press [Enter]');
    if (i === 4) await t.shot('door-trivia-answer.png');
    await t.say('\r');
  }
  check(found === 10, `test found the right answer for every question (${found}/10)`);
  check(await t.until('GAME OVER') && t.has('A PERFECT GAME'), 'game over: a perfect game');
  check(t.has('Final score: 1,950'), 'score: 10 x (100 + 50 speed) + streak 8 x 25 + perfect 250 = 1,950');
  check(t.has('new ARM Pit RECORD'), 'beats the seeded record');
  await t.shot('door-trivia-gameover.png');
  await t.say('\r');
  await t.until('[P H R Q]');

  // ---- games 2 and 3: always "A" (some right by chance)
  for (let g = 2; g <= 3; g++) {
    await t.say('P');
    for (let i = 1; i <= 10; i++) { await t.until(`Question ${i} of 10`); await t.until('Your answer'); await t.say('A'); await t.until('Press [Enter]'); await t.say('\r'); }
    check(await t.until('GAME OVER'), `game ${g} played`);
    await t.say('\r');
    await t.until('[P H R Q]');
  }
  check(t.has('Games left today: 0'), 'no games left today');
  await t.say('P');
  check(await t.until('locked until tomorrow'), 'fourth game refused: three games a day');
  await t.say('\r');
  await t.until('[P H R Q]');
  await t.say('Q');
  check(await t.until('Back to the BBS'), 'Q leaves the door');
  await t.until('C:\\DOOR>');

  const dat = t.read('DOOR\\TRIVIA.DAT')?.toString('latin1') || '';
  const me = dat.split(/\r?\n/).find((l) => l.startsWith('Local Player|')) || '';
  const f = me.split('|');
  check(f[1] === '1950' && f[3] === '3' && f[4] === '1' && f[7] === '3', `TRIVIA.DAT: best 1950, 3 games, 1 perfect, 3 today (${me})`);
  const scores = t.read('DOOR\\SCORES.TXT')?.toString('latin1') || '';
  check(/^Byte-Sized Trivia - best games\r?\n 1\. Local Player\s+1,950/.test(scores) && scores.includes('2. Susan Oyelaran'), 'SCORES.TXT: Local Player on top, Susan second');
  check(scores.trim().split(/\r?\n/).length <= 8, 'SCORES.TXT is at most 8 lines');
  const news = t.read('DOOR\\NEWS.TXT')?.toString('latin1') || '';
  check(news.includes('11-04-89  Local Player set a new Byte-Sized Trivia record: 1,950 points!'), 'NEWS.TXT: the record headline');
}
