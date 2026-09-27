// Pit Poker through the BBS (door 4): three hands (hold the first card, draw),
// cash out, back to the BBS.
export default async function ({ say, expect, settle, shot, tail, drive, back, check, readFile, bbs, v }) {
  await say('4\r'); check(await expect('Opening door'), 'door 4: Pit Poker opens');
  const atDeal = () => /bet & deal.*$/s.test(tail(120));
  check(await drive([[/Press \[Enter\].*$/s, '\r']], atDeal, 20), 'poker table: the deal prompt');
  let hands = 0;
  for (let h = 1; h <= 3; h++) {
    await say('\r');
    if (!await expect('to draw')) break;
    await say('1');
    if (h === 1) { await settle(); await shot('door-poker'); }
    await say('\r', 400);
    if (await expect('bet & deal')) hands++;
  }
  check(hands === 3, `played ${hands} hands of Jacks or Better`);
  await settle(); await shot('door-poker-result');
  await say('Q');
  check(await expect('You cash out with'), 'cash out');
  check(await drive([[/Press \[Enter\].*$/s, '\r']], back, 20), 'quit poker: back at the BBS');
  const dat = readFile(bbs, 'BBS\\DOORS\\POKER\\POKER.DAT')?.toString('latin1') || '';
  check(dat.split(/\r?\n/).some((l) => l.startsWith('Susan Oyelaran|')), 'POKER.DAT on the BBS disk has the caller');
  await say('\r'); await expect('Door # to open');
}
