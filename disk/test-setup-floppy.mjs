#!/usr/bin/env node
// disk/test-setup-floppy.mjs - the Startup diskette as a DOS setup disk ("make setup-floppy-test"):
// it boots with DOS in A:\DOS and the DOS Shell runs from it; FORMAT C: /S (current volume
// label, the warning, Y) wipes the hard disk and makes it bootable; INSTALL copies DOS to
// C:\DOS with CONFIG.SYS, AUTOEXEC.BAT and a C: DOSSHELL.BAT; without the diskette the PC then
// starts from the fresh C:, loads the CD-ROM driver, and the Shell and EDIT run from there.
import fs from 'node:fs';
import { boot } from '../emu/testkit.mjs';

let fails = 0, n = 0;
const ok = (c, what, detail) => { n++; console.log(`${c ? 'ok  ' : 'FAIL'} ${what}`); if (!c) { fails++; if (detail) console.log('     ' + String(detail).replace(/\n/g, '\n     ')); } return c; };
const B = (f) => new Uint8Array(fs.readFileSync('build/' + f));
const pc = await boot({ rom: B('rom.bin'), hd: B('hd.img'), fd: B('floppy-boot.img') });
const scr = () => pc.screen();
const wait = (t, ms = 60000) => pc.waitText(t, { timeoutMs: ms });

ok(wait('This is the ARM-DOS 4.00 Startup diskette') && wait('A:\\>'), 'boots from the Startup diskette', scr());
pc.run(300);
pc.type('DOSSHELL\r');
ok(wait('Start Programs', 30000), 'the DOS Shell runs from A:\\DOS', scr());
pc.type('{F3}'); ok(wait('A:\\DOS>', 20000), 'F3 leaves the Shell', scr());
pc.type('CD \\\rCLS\rFORMAT C: /S\r');
ok(wait('Enter current volume label for drive C:'), 'FORMAT C: asks for the current volume label', scr());
pc.type('ARM-DOS\r');
ok(wait('Proceed with Format (Y/N)?') && /WILL BE LOST/.test(scr()), 'the warning and Y/N', scr());
pc.type('Y\r');
ok(wait('Volume label (11 characters', 300000) && /System transferred/.test(scr()), 'Format complete, System transferred', scr());
pc.type('\r');
ok(wait('bytes available on disk', 30000) && wait('A:\\>'), 'the disk space report', scr());
pc.type('CLS\rINSTALL\r');
ok(wait('Ctrl+Alt+Del to start ARM-DOS from the hard disk', 120000), 'INSTALL copies DOS to C:', scr());
pc.run(500);

pc.machine.ejectFloppy();
pc.machine.resetRequest();
ok(wait('Type DOSSHELL for the ARM-DOS Shell.') && wait('C:\\>'), 'without the diskette: boots from the fresh C:', scr());
ok(/Drive D: = Driver ARMCD001/.test(scr()), 'CONFIG.SYS / AUTOEXEC.BAT from the install: CD-ROM driver loaded', scr());
pc.run(300);
pc.type('CLS\rDIR C:\\\r'); pc.run(2500);
ok(/COMMAND\s+COM/.test(scr()) && /DOS\s+<DIR>/.test(scr()) && !/GAMES/.test(scr()), 'C:\\ holds only DOS: COMMAND.COM, DOS, CONFIG.SYS, AUTOEXEC.BAT', scr());
pc.type('CLS\rTYPE C:\\DOS\\DOSSHELL.BAT\r'); pc.run(2000);
ok(/@CD C:\\DOS/.test(scr()), 'C:\\DOS\\DOSSHELL.BAT is the C: one', scr());
pc.type('CLS\rDOSSHELL\r');
ok(wait('Start Programs', 30000), 'the DOS Shell runs from C:\\DOS', scr());
pc.type('{F3}'); ok(wait('C:\\DOS>', 20000), 'F3 leaves the Shell', scr());
pc.type('CLS\rCD \\\rEDIT\r');
ok(wait('File', 30000) && /Edit/.test(scr()), 'EDIT runs', scr());

console.log(`\nSETUP FLOPPY: ${n - fails} passed, ${fails} failed`);
process.exit(fails ? 1 : 0);
