#!/usr/bin/env node
// apps/cdrom/tests/redir-mock.mjs - the kernel's redirector callouts (INT 2Fh
// AH=11h, see apps/cdrom/README.md) against MOCKRED.COM, a mock
// redirector serving a small read-only D:, with the real COMMAND.COM.
//   node apps/cdrom/tests/redir-mock.mjs    (make cdrom-redir-test)
import fs from 'node:fs';
import { startPC, check, failed, B, readHdFile, lastLine } from './harness.mjs';

const pc = await startPC({
  name: 'redir-mock', outDir: 'build/cdrom-test', shell: 'command',
  files: [{ src: 'build/cdrom-test/MOCKRED.COM', dst: 'T\\' }, { src: 'build/sdk-tests/T_HELLO.EXE', dst: 'T\\' }],
});
check(pc.bootOk, 'booted to C:\\>');
const scr = () => pc.screen();
const cls = () => pc.cmd('CLS');

pc.cmd('C:\\T\\MOCKRED C:\\T\\T_HELLO.EXE');
check(pc.hasText('MOCKRED installed'), 'mock redirector installed');

cls(); pc.cmd('DIR D:');
let s = scr();
console.log(s);
check(/Volume in drive D is MOCKCD/.test(s), 'DIR D: volume label (FCB search, attr 08h)');
check(!/Serial Number/.test(s), 'no serial number line (69h fails on a remote drive)');
check(/Directory of\s+D:\\/.test(s), 'Directory of D:\\');
check(/HELLO\s+TXT\s+53\s+0?6-01-93\s+12:00p/.test(s), 'HELLO.TXT listed with size, date and time');
check(/SUB\s+<DIR>/.test(s), 'SUB <DIR>');
check(/T_HELLO\s+EXE/.test(s), 'the loaded program listed');
check(/File\(s\)\s+0 bytes free/.test(s), 'n File(s) 0 bytes free (110Ch)');

cls(); pc.cmd('DIR D:\\SUB');
s = scr();
console.log(s);
check(/Directory of\s+D:\\SUB/.test(s) && /INNER\s+TXT\s+13/.test(s) && /\.\s+<DIR>/.test(s), 'DIR D:\\SUB (CHDIR via 1105h + FCB search)');

cls(); pc.cmd('TYPE D:\\HELLO.TXT');
check(pc.hasText('Hello from drive D:, served by the mock redirector.'), 'TYPE D:\\HELLO.TXT (1116h open, 1108h read)');

cls(); pc.cmd('COPY D:\\HELLO.TXT C:\\');
check(/1 File\(s\) copied/.test(scr()), 'COPY D:\\HELLO.TXT C:\\');
cls(); pc.cmd('TYPE C:\\HELLO.TXT');
check(pc.hasText('Hello from drive D:, served by the mock redirector.'), 'TYPE C:\\HELLO.TXT (the copy)');
check(readHdFile(pc, 'HELLO.TXT') === 'Hello from drive D:, served by the mock redirector.\r\n', 'C:\\HELLO.TXT content on the image');

cls(); pc.cmd('D:');
check(lastLine(pc).startsWith('D:\\>'), 'D: (prompt D:\\>)');
pc.cmd('CD SUB');
check(lastLine(pc).startsWith('D:\\SUB>'), 'CD SUB (prompt D:\\SUB>)');
cls(); pc.cmd('DIR');
check(/Directory of\s+D:\\SUB/.test(scr()) && /INNER\s+TXT/.test(scr()), 'DIR in D:\\SUB');
check(/Volume in drive D is MOCKCD/.test(scr()), 'the label is found from a subdirectory (root search)');
pc.cmd('TYPE INNER.TXT');
check(pc.hasText('Inside SUB.'), 'TYPE INNER.TXT (relative to the remote current directory)');
pc.cmd('CD \\');
check(lastLine(pc).startsWith('D:\\>'), 'CD \\');
pc.cmd('CD NOWHERE');
check(pc.hasText('Invalid directory'), 'CD NOWHERE -> Invalid directory');

cls(); pc.cmd('D:\\T_HELLO');
check(pc.hasText('Hello from ARM-DOS!'), 'a program runs from D: (EXEC through 1116h/1108h)');
cls(); pc.cmd('T_HELLO');
check(pc.hasText('Hello from ARM-DOS!'), 'a program runs from the current directory on D:');

cls(); pc.cmd('DEL D:\\HELLO.TXT');
check(pc.hasText('Access denied'), 'DEL D:\\HELLO.TXT -> Access denied');
cls(); pc.cmd('MD D:\\NEW');
check(/Unable to create directory/.test(scr()), 'MD D:\\NEW fails');
cls(); pc.cmd('COPY C:\\HELLO.TXT D:\\X.TXT');
check(!/1 File\(s\) copied/.test(scr()), 'COPY to D: fails');
console.log(scr());

cls(); pc.cmd('TYPE D:\\NOFILE.TXT');
check(pc.hasText('File not found'), 'TYPE D:\\NOFILE.TXT -> File not found');

// R3: 1206h from inside a callout raises DOS's INT 24h
cls();
pc.type('TYPE D:\\MISSING.TXT\r');
check(pc.waitText('Abort, Retry, Fail?', { timeoutMs: 10000 }), 'INT 2Fh 1206h -> Abort, Retry, Fail?');
s = scr();
check(/Not ready reading drive D/.test(s), '"Not ready reading drive D"');
await pc.shot('redir-crit.png');
pc.type('R');
pc.run(500);
check((scr().match(/Abort, Retry, Fail\?/g) || []).length >= 2, 'Retry asks again');
pc.type('F');
pc.until(() => /^D:\\>/.test(lastLine(pc)) && pc.m.biosKeyBufferEmpty(), { timeoutMs: 10000 });
console.log(scr());
check(/^D:\\>/.test(lastLine(pc)), 'Fail -> back to the prompt');

cls(); pc.cmd('C:');
pc.cmd('DIR C:\\');
s = scr();
check(/Volume in drive C is ARM-DOS/.test(s) && /COMMAND\s+COM/.test(s) && /bytes free/.test(s), 'C: still works (DIR C:\\)');
await pc.shot('redir-dir-c.png');
check(pc.faults.length === 0, 'no CPU faults');

console.log(failed() ? `${failed()} check(s) FAILED` : 'redirector mock: all checks passed');
process.exit(failed() ? 1 : 0);
