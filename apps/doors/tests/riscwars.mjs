// RISC Wars (apps/doors/riscwars), played locally: sign on, warp, trade at two
// ports (buy Silicon at sector 3, autopilot to sector 5 and sell it), the Fab
// (fighters, holds), deploy fighters outside Acorn Space, rankings, quit; the
// data files on disk; a second run keeps the state; after DATE moves to the next
// day the regulars take their turns and the caller gets fresh turns.
export default async function ({ doorPC, check }) {
  const t = await doorPC('riscwars', 'RISCWARS.EXE', 'apps/doors/data/riscwars');
  const text = () => t.screen();
  // answer whatever interrupts us (hazards, pauses, fighters in the way) until `want` shows
  async function reach(want, ms = 20000) {
    for (let i = 0; i < 12; i++) {
      if (await t.until(want, 1500)) return true;
      const s = text();
      if (s.includes('block the warp lane')) await t.say('R', 600);
      else if (s.includes('Press [Enter]')) await t.say('\r', 600);
      else if (s.includes('Engage the autopilot')) await t.say('Y', 600);
    }
    return t.until(want, ms);
  }
  const prompt = /Command \[TL=(\d+)\]:\[(\d+)\]/g;
  const last = () => { const m = [...text().matchAll(prompt)].pop(); return m ? { tl: +m[1], sec: +m[2] } : null; };

  await t.say('RISCWARS /L\r', 1500);
  check(await t.until('sign on as a trader'), 'RISC Wars: title, universe created, new trader prompt');
  await t.shot('door-riscwars-title.png');
  await t.say('Y'); check(await t.until('Name your ship'), 'asks for a ship name');
  await t.say('Difference Engine\r'); check(await t.until('30 holds, 20 fighters'), 'dockmaster briefing');
  await t.say('\r', 800);
  check(await t.until('Acorn Station') && await t.until('Warps to'), 'sector 1: Acorn Station and The Fab, warps listed');
  check(last()?.tl === 150, 'command prompt shows 150 turns');

  // buy Silicon in sector 3
  await t.say('3\r', 800);
  check(await reach('Port Thumb'), 'warp to sector 3: Port Thumb (class SBB)');
  check(last()?.sec === 3 && last()?.tl === 149, 'a warp costs one turn');
  await t.say('P', 800); check(await t.until('How many do you buy'), 'port: selling Silicon, how many to buy');
  await t.shot('door-riscwars-port.png');
  await t.say('\r', 800); check(await t.until('You load 30 units'), 'bought 30 units of Silicon (holds full)');
  await t.say('\r', 800);

  // sell it in sector 5 (not adjacent: the autopilot plots the route)
  await t.say('5\r', 800);
  check(await t.until('shortest route'), 'sector 5 is not adjacent: course plotted');
  await t.say('Y', 1500);
  check(await reach('Jazelle Junction'), 'autopilot arrives at sector 5, Jazelle Junction');
  await t.say('P', 800); check(await t.until('How many do you sell'), 'port buys Silicon');
  await t.say('\r', 800); check(await t.until('You sell 30 units'), 'sold 30 units of Silicon');
  for (let i = 0; i < 3 && !text().includes('Press [Enter]'); i++) await t.say('0\r', 500);
  await t.say('\r', 800);
  await t.shot('door-riscwars-trade.png');

  // the Fab
  await t.say('1\r', 800); await reach('Acorn Station');
  await t.say('B', 800); check(await t.until('THE FAB'), 'the Fab at Acorn Station');
  await t.say('F', 500); await t.until('How many fighters');
  await t.say('10\r', 500); check(await t.until('10 fighters roll off the line'), 'bought 10 fighters');
  await t.say('\r', 500);
  await t.say('H', 500); await t.until('How many holds');
  await t.say('1\r', 500); check(await t.until('bolts on 1 hold '), 'bought a cargo hold');
  await t.shot('door-riscwars-fab.png');
  await t.say('\r', 500); await t.say('L', 800);

  // out of Acorn Space and deploy fighters
  await t.say('C', 500); await t.until('Plot a course');
  await t.say('11\r', 800); await t.until('Engage the autopilot');
  await t.say('Y', 2000);
  const ok11 = await reach(':[11]', 8000);
  check(ok11, 'course plotter + autopilot to sector 11');
  if (ok11) {
    await t.say('F', 500);
    if (await t.until('How many fighters should guard', 2000)) {
      await t.say('5\r', 800);
      check(await t.until('5 fighters now guard sector 11'), 'deployed 5 fighters in sector 11');
    } else check(false, 'deploy fighters prompt');
  }
  await t.say('I', 800); check(await t.until('Net worth'), 'ship info screen');
  await t.say('\r', 500);
  await t.say('R', 800);
  check(await t.until('THE TRADERS') && t.has('Karen Whitfield') && t.has('Difference Engine'), 'rankings: the regulars and the new trader');
  await t.shot('door-riscwars-rank.png');
  await t.say('\r', 500);
  await t.say('N', 800); check(await t.until('Local Player signed on'), 'news: the sign-on headline');
  await t.say('\r', 500);
  await t.say('1989\r', 800); check(await t.until('dials 555-1989'), 'easter egg: warp to sector 1989');
  const tl1 = last()?.tl;
  await t.say('Q', 500); await t.say('Y', 1500);
  check(await t.until('C:\\DOOR>'), 'Q: back to DOS');

  const scores = t.read('DOOR\\SCORES.TXT')?.toString('latin1') || '';
  check(scores.startsWith('RISC Wars - top traders') && scores.includes('Karen Whitfield'), 'SCORES.TXT written: ' + JSON.stringify(scores.split('\n')[1]));
  const news1 = t.read('DOOR\\NEWS.TXT')?.toString('latin1') || '';
  check(news1.includes('11-04-89  Local Player signed on as a trader'), 'NEWS.TXT: dated headline');
  check((t.read('DOOR\\PLAYERS.DAT')?.toString('latin1') || '').includes('Difference Engine'), 'PLAYERS.DAT holds the trader');
  check((t.read('DOOR\\UNIVERSE.DAT')?.length || 0) > 1000, 'UNIVERSE.DAT saved');

  // same day again: state kept, no sign-on
  await t.say('RISCWARS /L\r', 1500);
  await reach('Warps to');
  check(!text().includes('sign on as a trader') && last()?.sec === 11 && last()?.tl === tl1, `second run the same day: sector 11, ${tl1} turns left`);
  await t.say('Q', 500); await t.say('Y', 1500); await t.until('C:\\DOOR>');

  // the next day
  await t.say('DATE 11-05-89\r', 800);
  await t.say('RISCWARS /L\r', 1500);
  check(await t.until('A new day dawns'), 'new day: the regulars make their moves');
  check(await t.until('you have 150 turns'), 'new day: fresh turns');
  await t.say('\r', 800); await reach('Warps to');
  await t.shot('door-riscwars-newday.png');
  await t.say('Q', 500); await t.say('Y', 1500); await t.until('C:\\DOOR>');
  const news2 = t.read('DOOR\\NEWS.TXT')?.toString('latin1') || '';
  check(news2.split('\n').some((l) => l.startsWith('11-05-89')), 'NEWS.TXT: the regulars made the 11-05-89 news: ' + JSON.stringify(news2.split('\n').filter((l) => l.startsWith('11-05-89'))[0]));
}
