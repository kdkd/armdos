// apps/edlin/tests/scenarios.mjs - EDLIN sessions run with redirected input,
// both on the genuine MS-DOS 4.00 EDLIN (tests/mkref.mjs, DOSBox-X; results in
// tests/ref/) and on ARM-DOS (tests/run.mjs), which must match byte for byte:
// the captured standard output (EDLIN's prompts, listings and the echo of the
// redirected input) and every file left in the scenario's directory.
//
// Each scenario runs in C:\E\<NAME>\ as   EDLIN <args> < \E\<NAME>.SCR > \E\<NAME>.OUT

const CRLF = '\r\n';
const Z = '\x1A';
const lines = (n, f) => Array.from({ length: n }, (_, i) => f(i + 1)).join(CRLF) + CRLF;
const script = (...l) => l.join(CRLF) + CRLF;

const forty = lines(40, (i) => `line ${String(i).padStart(2, '0')} of the forty-line test file`);

export const scenarios = [
  // an empty file loads as one empty line (check_end looks at the byte
  // before the buffer, which is 0)
  { name: 'EMPTY', args: 'E.TXT', files: { 'E.TXT': '' },
    script: script('l', 'i', 'x', Z, 'l', 'e') },

  // the verified interactive session of UTILITIES.md 2.14, redirected
  { name: 'NEW', args: 'NEW.TXT', files: {},
    script: script('i', 'first line', 'second line', Z, 'l', '1', '', 'e') },

  // listing and paging
  { name: 'LIST', args: 'A.TXT', files: { 'A.TXT': forty },
    script: script('l', '5l', '10,15l', '30l', ',5l', '38,l', '#l', 'p', 'p', '1p', '35,39p', '20', '', 'l', '.l', '-3,+2l', 'q', 'y') },

  // editing lines, deleting, inserting, ending
  { name: 'EDIT', args: 'A.TXT', files: { 'A.TXT': forty },
    script: script('3', 'replacement third line', '4', '', '5', 'X',
      '3,5d', '1,6l', 'd', 'l', '#d', '2i', 'new two', 'new three', Z, '1,8l', '#i', 'at the end', Z,
      '38,#l', '1i', 'top', Z, '1,3l', 'i', 'mid', Z, '1,5l', 'e') },

  // copy and move
  { name: 'COPYMOVE', args: 'A.TXT', files: { 'A.TXT': lines(12, (i) => `row ${i}`) },
    script: script('1,3,10c', 'l', '1,2,20m', '#l', '2,3,1,3c', '1,12l', '5,6,1m', '1,6l',
      '1,2m', '1,2,m', '7,8,7m', '1,3,2c', '3,5,1,0c', 'l', 'e') },

  // entry errors
  { name: 'ERRORS', args: 'A.TXT', files: { 'A.TXT': lines(5, (i) => `e${i}`) },
    script: script('5,1l', '0l', 'x', '1,2,3,4,5l', '1,2i', '1,2,3,.c', '99999l', '1,2,3,4d',
      '70000', 'l ; 2d ; l', '2d;1d;l', 'z', 'q', 'n', 'l', 'e3') },

  // search
  { name: 'SEARCH', args: 'A.TXT', files: { 'A.TXT': forty },
    script: script('sline 1', 's', '1,#sforty', '?1,5sline', 'n', 'n', 'y', '?sline 3', 'n', 'n', 'n',
      'n', 'n', 'n', 'n', 'n', 'n', 'n', 'n', 'snothing', 's', '35sline 3', 'q', 'y') },

  // replace
  { name: 'REPLACE', args: 'A.TXT', files: { 'A.TXT': lines(8, (i) => `alpha beta ${i} alpha`) },
    script: script('1,#ralpha' + Z + 'omega', 'l', '?1,3romega' + Z + 'A', 'y', 'n', 'y', 'n', 'y', 'y',
      'l', '1,#rbeta' + Z, 'l', '1,#romega', 'l', '5rA' + Z + 'double A', 'l', 'e') },

  // quit and abort answers
  { name: 'QUIT', args: 'A.TXT', files: { 'A.TXT': lines(3, (i) => `q${i}`) },
    script: script('1d', 'q', 'x', 'n', 'l', 'q', 'y') },

  // ^Z handling on load, and /B
  { name: 'CTRLZ', args: 'Z.TXT', files: { 'Z.TXT': 'one' + CRLF + 'two' + Z + 'three' + CRLF },
    script: script('l', 'e') },
  // /B is accepted but has no effect in 4.00 (EDLPARSE's val_sw never
  // recognises the switch), verified with the genuine EDLIN
  { name: 'SLASHB', args: 'Z.TXT /B', files: { 'Z.TXT': 'one' + CRLF + 'two' + Z + 'three' + CRLF + Z },
    script: script('l', 'e') },
  { name: 'NOCRLF', args: 'N.TXT', files: { 'N.TXT': 'first' + CRLF + 'no newline at the end' },
    script: script('l', 'e') },
  { name: 'ONLYLF', args: 'E.TXT', files: { 'E.TXT': CRLF },
    script: script('l', 'e') },

  // control characters are shown as ^X; ^V quotes them in input
  { name: 'CONTROL', args: 'C.TXT', files: { 'C.TXT': 'tab\there' + CRLF + 'bell\x07 esc\x1B' + CRLF + 'plain' + CRLF },
    script: script('l', '3', 'x\x16Gy\x16[z', 'l', 'i', 'q\x16V\x16Aw', Z, 'l', 'e') },

  // transfer (merge)
  { name: 'MERGE', args: 'A.TXT', files: { 'A.TXT': lines(5, (i) => `main ${i}`), 'B.TXT': lines(3, (i) => `merged ${i}`) + Z },
    script: script('3tB.TXT', 'l', 'tB.TXT', 'l', 'tnofile.txt', 'tq:x.txt', 'e') },

  // write and append, and a .BAK that already exists
  { name: 'WRITEAPP', args: 'A.TXT', files: { 'A.TXT': forty, 'A.BAK': 'old backup' + CRLF },
    script: script('5w', 'l', 'a', '10w', 'l', 'w', 'l', '3a', 'l', 'e') },

  // long lines
  { name: 'LONG', args: 'L.TXT', files: { 'L.TXT': 'x'.repeat(100) + CRLF + 'y'.repeat(300) + CRLF + 'short' + CRLF },
    script: script('l', '3rshort' + Z + 'z'.repeat(250), '3rshort' + Z + 'z'.repeat(20), 'l', 'e') },

  // a file name without an extension; several commands on a line
  { name: 'NOEXT', args: 'README', files: { 'README': lines(4, (i) => `r${i}`) },
    script: script('2d;l;1', 'first', 'e') },
];

// command-line errors: stdout only (the parse errors go to STDERR)
export const cmdline = [
  { name: 'NOARG', args: '' },
  { name: 'BAKFILE', args: 'X.BAK' },
  { name: 'BADDRV', args: 'Q:X.TXT' },
  { name: 'BADPATH', args: 'NODIR\\X.TXT' },
];

// experiments (mkref.mjs --probe): behaviour to find out before it becomes a test
export const probes = [];
