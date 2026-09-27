// cases.mjs - the case file format shared by run.mjs (and mirrored by
// ref/capture.py) and the screen normalisation.
//
// cases.txt:
//   # comment
//   === NAME [reboot] [settle=MS] [refwait=SECONDS]
//   a command line            (typed, then Enter)
//   keys{F3}{NOCR}            (testkit escapes; {NOCR} = no Enter at the end)
//
// Every case starts with CLS; the screen after the last line is compared.

export function parseCases(text) {
  const cases = [];
  let cur = null;
  for (const raw of text.split(/\r?\n/)) {
    if (raw.startsWith('=== ')) {
      const [name, ...opts] = raw.slice(4).trim().split(/\s+/);
      const o = {};
      for (const x of opts) {
        const [k, v] = x.split('=');
        o[k] = v ?? true;
      }
      cur = { name, opts: o, lines: [] };
      cases.push(cur);
    } else if (cur && raw.length && !raw.startsWith('#')) {
      cur.lines.push(raw);
    }
  }
  return cases;
}

// Fields that legitimately differ between the real DOS 4.00 run and ours.
export function normalise(screen) {
  let s = screen.replace(/\r/g, '');
  const lines = s.split('\n').map((l) => l.replace(/\s+$/, ''));
  while (lines.length && lines[lines.length - 1] === '') lines.pop();
  s = lines.join('\n');
  // DIR lines: date and time columns (also when a filter upper-cased them)
  s = s.replace(/\d\d-\d\d-\d\d  [ \d]\d:\d\d[apAP]/g, '<date>  <time>');
  // free space, serial numbers
  s = s.replace(/ +\d+ (bytes free)/gi, ' <n> $1');
  s = s.replace(/(Volume Serial Number is )[0-9A-F]{4}-[0-9A-F]{4}/gi, '$1<serial>');
  // sizes of COMMAND.COM and of the test programs (x86 twins on the real DOS)
  s = s.replace(/^(COMMAND  COM|RET      COM|ARGS     COM|UPCASE   COM|TRASH    COM|WAITKEY  COM) +\d+/gm, '$1 <size>');
  // current date and time
  s = s.replace(/(Current date is \w\w\w )[\d-]+/g, '$1<date>');
  s = s.replace(/(Current time is +)[\d:.]+[ap]?/g, '$1<time>');
  // pipe temporary files: 8 hex digits, no extension
  s = s.replace(/^[0-9A-F]{8}  {9}0 /gm, '<pipefile>          0 ');
  return s;
}
