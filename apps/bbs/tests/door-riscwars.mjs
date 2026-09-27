// RISC Wars through the BBS (door 2): a regular's ship, a trip home to Acorn
// Station (warps cost turns), the rankings, back to the BBS with the line up.
export default async function ({ say, expect, settle, shot, tail, since, drive, back, check, readFile, bbs }) {
  await say('2\r'); check(await expect('Opening door'), 'door 2: RISC Wars opens');
  const cmd = /Command \[TL=(\d+)\]:\[(\d+)\]/g;
  const lastCmd = () => { const m = [...since().matchAll(cmd)].pop(); return m ? { tl: +m[1], sec: +m[2] } : null; };
  const atCmd = () => /\(\?=Help\)\?\s*$/.test(tail(120).trimEnd());
  check(await drive([[/sign on as a trader.*$/s, 'Y'], [/Name your ship.*$/s, 'Pipeline Dream\r'], [/Press \[Enter\].*$/s, '\r']], atCmd, 20),
    'RISC Wars: at the command prompt (Susan takes over her ship)');
  const start = lastCmd();
  check(start && start.tl > 0, `turns left today: ${start?.tl}, sector ${start?.sec}`);
  await settle(); await shot('door-riscwars');
  if (start && start.sec !== 1) {
    await say('1\r', 600);
    await drive([[/Engage the autopilot.*$/s, 'Y'], [/block the warp lane.*$/s, 'R'], [/Press \[Enter\].*$/s, '\r']], () => atCmd() && lastCmd()?.sec === 1, 30);
  } else {
    await say('2\r', 600);
    await drive([[/Engage the autopilot.*$/s, 'Y'], [/Press \[Enter\].*$/s, '\r']], () => atCmd() && lastCmd()?.sec === 2, 30);
  }
  const now = lastCmd();
  check(now && start && now.tl < start.tl && now.sec !== start.sec, `warped to sector ${now?.sec}, turns ${start?.tl} -> ${now?.tl}`);
  await say('R', 600); check(await expect('THE TRADERS'), 'rankings');
  await settle(); await shot('door-riscwars-rank');
  await drive([[/Press \[Enter\].*$/s, '\r'], [/More.*$/s, 'Y']], atCmd, 10);
  await say('Q', 400);
  check(await drive([[/\[y\/N\]\??\s*$|\[Y\/n\]\??\s*$|\(Y\/N\).*$/is, 'Y'], [/Press \[Enter\].*$/s, '\r']], back, 20), 'quit RISC Wars: back at the BBS, line up');
  const p = readFile(bbs, 'BBS\\DOORS\\RISCWARS\\PLAYERS.DAT');
  check(p && p.toString('latin1').includes('Susan Oyelaran'), 'PLAYERS.DAT on the BBS disk has the caller');
  await say('\r'); await expect('Door # to open');
}
