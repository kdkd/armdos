// Pit Poker (PITPOKER.EXE /L): a new player gets 100 credits, deals, holds,
// draws; the test reads the cards off the screen, scores the hand itself and
// checks the credits; the bankroll persists in POKER.DAT across a restart;
// SCORES.TXT is the leaderboard.
const RANKS = '23456789TJQKA';
const PAYS = [0, 1, 2, 3, 4, 6, 9, 25, 50, 250];

function evaluate(cards) {                       // [{r:0..12, s:'♥'}]
  const cnt = Array(13).fill(0);
  for (const c of cards) cnt[c.r]++;
  const fl = cards.every((c) => c.s === cards[0].s);
  const groups = cnt.filter((n) => n > 1).sort();
  let st = false, top = -1;
  if (!groups.length) {
    const rs = cards.map((c) => c.r).sort((a, b) => a - b);
    if (rs[4] - rs[0] === 4) { st = true; top = rs[4]; }
    else if (rs.join() === '0,1,2,3,12') { st = true; top = 3; }
  }
  if (st && fl) return top === 12 ? 9 : 8;
  if (groups.includes(4)) return 7;
  if (groups.includes(3) && groups.includes(2)) return 6;
  if (fl) return 5;
  if (st) return 4;
  if (groups.includes(3)) return 3;
  if (groups.length === 2) return 2;
  if (groups.length === 1 && cnt.findIndex((n) => n === 2) >= 9) return 1;
  return 0;
}

export default async function ({ doorPC, check }) {
  const t = await doorPC('poker', 'PITPOKER.EXE', 'apps/doors/data/poker');
  const credits = () => { const m = /Credits (\d[\d,]*)/.exec(t.pc.lines()[0]); return m ? +m[1].replace(/,/g, '') : NaN; };
  const readCards = () => {
    const L = t.pc.lines();
    const ranks = [...L[13].matchAll(/│(\S{1,2}) +│/g)].map((m) => m[1] === '10' ? 8 : RANKS.indexOf(m[1]));
    const suits = [...L[14].matchAll(/│ +(\S) +│/g)].map((m) => m[1]);
    return ranks.map((r, i) => ({ r, s: suits[i] }));
  };

  await t.say('PITPOKER /L\r');
  check(await t.until('A new player!'), 'PITPOKER /L: a new player gets a stake');
  check(t.has('P I T   P O K E R') && t.has('100'), 'title and 100 credits');
  await t.shot('door-poker-title.png');
  await t.say('\r');
  check(await t.until('bet & deal'), 'poker table: pay table and the deal prompt');
  check(t.has('Royal Flush') && t.has('4000') && t.has('Jacks or Better') && t.has('Full House'), '9/6 pay table with the 4000-credit royal');
  check(credits() === 100, 'credits 100 on the header');

  let expect = 100;
  for (let h = 1; h <= 4; h++) {
    const bet = h >= 3 ? 2 : 5;              // Enter keeps the last bet
    await t.say(h === 3 ? '2' : '\r');
    if (!check(await t.until('to draw'), `hand ${h}: dealt (bet ${bet})`)) break;
    const before = readCards();
    check(before.length === 5 && before.every((c) => c.r >= 0 && /[♥♦♣♠]/.test(c.s)), `hand ${h}: five cards on screen (${before.map((c) => RANKS[c.r] + c.s).join(' ')})`);
    await t.say('1'); await t.say('3');
    check(t.pc.lines()[18].includes('HELD'), `hand ${h}: HELD shown under the held cards`);
    await t.say('3');                          // un-hold card 3
    if (h === 1) await t.shot('door-poker-hold.png');
    await t.say('\r', 600);
    await t.until('bet & deal');
    const after = readCards();
    check(after[0].r === before[0].r && after[0].s === before[0].s, `hand ${h}: held card 1 kept (${after.map((c) => RANKS[c.r] + c.s).join(' ')})`);
    const hv = evaluate(after);
    const win = hv === 9 && bet === 5 ? 4000 : PAYS[hv] * bet;
    expect += win - bet;
    check(credits() === expect, `hand ${h}: hand value ${hv}, paid ${win}, credits ${credits()} = ${expect}`);
    if (h === 1) await t.shot('door-poker-result.png');
  }
  await t.say('S');
  check(await t.until('HIGH ROLLERS OF THE PIT') && t.has('Bill Tran') && t.has('4,655'), 'scores: seeded high rollers');
  await t.shot('door-poker-scores.png');
  await t.say('\r');
  await t.until('bet & deal');
  await t.say('Q');
  check(await t.until(`You cash out with ${expect.toLocaleString('en-US')} credits`), 'Q cashes out with the right credits');
  await t.until('C:\\DOOR>');

  const dat = t.read('DOOR\\POKER.DAT')?.toString('latin1') || '';
  const me = (dat.split(/\r?\n/).find((l) => l.startsWith('Local Player|')) || '').split('|');
  check(+me[1] === expect && +me[7] === 4, `POKER.DAT: credits ${me[1]}, 4 hands`);
  const scores = t.read('DOOR\\SCORES.TXT')?.toString('latin1') || '';
  check(/^Pit Poker - biggest bankrolls\r?\n 1\. Bill Tran\s+4,655  best hand: Royal Flush/.test(scores) && scores.trim().split(/\r?\n/).length <= 8, 'SCORES.TXT: title + top five, Bill Tran first');

  // same day again: no top-up (unless broke), the bankroll carries over
  await t.say('PITPOKER /L\r');
  check(await t.until(`You have ${expect.toLocaleString('en-US')} credits`), 'restart the same day: welcome back with the same bankroll (no top-up)');
  await t.say('\r');
  await t.until('bet & deal');
  await t.say('Q');
  await t.until('C:\\DOOR>');
}
