#!/usr/bin/env python3
"""
gen.py - the x86 CPU conformance suite for X86.EXE.

Each template is an instruction (plus optional operand conditioning); each
case gives EAX..EDX and the incoming arithmetic flags (random, with the
interesting boundary values mixed in).  The same templates and cases are
emitted twice:

  CPUTn.COM   16-bit DOS program, run under X86.EXE on ARM-DOS
  cputn.elf   32-bit Linux program, run natively on the host's x86 CPU

Both print one line per case - template, EAX EBX ECX EDX, and FLAGS masked
to the flags the instruction defines - so the outputs must be identical.
(16-bit operand forms behave the same in 32-bit protected mode as in real
mode; the registers only.)

usage: gen.py OUTDIR   -> OUTDIR/cputN.asm (DOS) and OUTDIR/cputN_elf.asm
"""
import os
import random
import sys

CF, PF, AF, ZF, SF, OF = 0x001, 0x004, 0x010, 0x040, 0x080, 0x800
SZP = SF | ZF | PF
ALL = CF | PF | AF | ZF | SF | OF

T = []   # (name, pre, instr, mask)


def t(instr, mask, pre=''):
    T.append((instr, pre, instr, mask))


regs8, regs16, regs32 = ('al', 'bl'), ('ax', 'bx'), ('eax', 'ebx')
for op in ('add', 'adc', 'sub', 'sbb', 'cmp'):
    t(f'{op} al, bl', ALL); t(f'{op} ax, bx', ALL); t(f'{op} eax, ebx', ALL)
    t(f'{op} bl, 0x81', ALL); t(f'{op} bx, -3', ALL); t(f'{op} ebx, 0x12345678', ALL)
    t(f'{op} cx, 0x8001', ALL)
for op in ('and', 'or', 'xor', 'test'):
    t(f'{op} al, bl', SZP | CF | OF); t(f'{op} ax, bx', SZP | CF | OF); t(f'{op} eax, ebx', SZP | CF | OF)
    t(f'{op} bx, 0x0FF0', SZP | CF | OF)
for op in ('inc', 'dec'):
    t(f'{op} al', ALL); t(f'{op} cx', ALL); t(f'{op} edx', ALL); t(f'{op} bh', ALL)
t('neg al', ALL); t('neg bx', ALL); t('neg ecx', ALL)
t('not al', ALL); t('not bx', ALL); t('not ecx', ALL)
for op in ('shl', 'shr', 'sar'):
    for r in ('al', 'bx', 'edx'):
        t(f'{op} {r}, 1', SZP | CF | OF)
    t(f'{op} al, cl', SZP | CF, 'and cl, 7')
    t(f'{op} bx, cl', SZP | CF, 'and cl, 15')
    t(f'{op} edx, cl', SZP | CF, 'and cl, 31')
    t(f'{op} bx, 5', SZP | CF)
for op in ('rol', 'ror', 'rcl', 'rcr'):
    for r in ('al', 'bx', 'edx'):
        t(f'{op} {r}, 1', CF | OF)
    t(f'{op} al, cl', CF, 'and cl, 7')
    t(f'{op} bx, cl', CF, 'and cl, 15')
    t(f'{op} edx, cl', CF, 'and cl, 31')
    t(f'{op} bl, 3', CF)
t('mul bl', CF | OF); t('mul bx', CF | OF); t('mul ebx', CF | OF)
t('imul bl', CF | OF); t('imul bx', CF | OF); t('imul ebx', CF | OF)
t('imul cx, bx', CF | OF); t('imul ecx, ebx', CF | OF)
t('imul cx, bx, -7', CF | OF); t('imul cx, bx, 1234', CF | OF); t('imul ecx, ebx, 100000', CF | OF)
t('div bl', 0, 'and ah, 0x3F\n or bl, 0x40')
t('div bx', 0, 'and dx, 0x3FFF\n or bx, 0x4000')
t('div ebx', 0, 'and edx, 0x3FFFFFFF\n or ebx, 0x40000000')
t('idiv bl', 0, 'movsx ax, al\n and bl, 0x7E\n or bl, 2')
t('idiv bx', 0, 'cwd\n and bx, 0x7FFE\n or bx, 2')
t('idiv ebx', 0, 'cdq\n and ebx, 0x7FFFFFFE\n or ebx, 2')
t('daa', CF | AF | SZP); t('das', CF | AF | SZP)
t('aaa', CF | AF); t('aas', CF | AF)
t('aam', SZP); t('aam 7', SZP); t('aad', SZP); t('aad 13', SZP)
t('cbw', 0); t('cwd', 0); t('cwde', 0); t('cdq', 0)
t('movzx cx, bl', 0); t('movsx cx, bl', 0); t('movzx ecx, bx', 0); t('movsx ecx, bx', 0); t('movsx ecx, bh', 0)
for op in ('bt', 'bts', 'btr', 'btc'):
    t(f'{op} bx, cx', CF); t(f'{op} ebx, ecx', CF); t(f'{op} bx, 13', CF); t(f'{op} ebx, 29', CF)
t('bsf cx, bx', ZF, 'or bx, 0x100'); t('bsr cx, bx', ZF, 'or bx, 4')
t('bsf ecx, ebx', ZF, 'or ebx, 0x10000'); t('bsr ecx, ebx', ZF, 'or ebx, 1')
t('shld bx, cx, 5', SZP | CF); t('shrd bx, cx, 7', SZP | CF)
t('shld ebx, ecx, 13', SZP | CF); t('shrd ebx, ecx, 27', SZP | CF)
t('shld bx, dx, cl', SZP | CF, 'and cl, 15\n or cl, 1')
t('shrd ebx, edx, cl', SZP | CF, 'and cl, 31\n or cl, 1')
t('shld ebx, edx, 1', SZP | CF | OF)
for cc in ('o', 'no', 'b', 'ae', 'z', 'nz', 'be', 'a', 's', 'ns', 'p', 'np', 'l', 'ge', 'le', 'g'):
    t(f'set{cc} cl', ALL)
    t(f'cmp ax, bx\n set{cc} cl', ALL)
    t(f'cmp al, bl\n set{cc} ch', ALL)
    t(f'cmp eax, ebx\n set{cc} dl', ALL)
    t(f'test bx, ax\n set{cc} dh', ALL)
    t(f'sub ax, bx\n set{cc} cl', ALL)
    t(f'dec bx\n set{cc} cl', ALL)
    t(f'add al, bl\n set{cc} cl', ALL)
t('xchg ax, bx', 0); t('xchg cl, dh', 0); t('bswap ecx', 0)
t('xadd bx, cx', ALL); t('cmpxchg bx, cx', ALL); t('cmpxchg ecx, edx', ALL)
t('lahf', ALL); t('sahf', ALL); t('cmc', ALL); t('clc', ALL); t('stc', ALL)
t('salc', ALL)
t('sbb ax, ax', ALL); t('adc cx, cx', ALL); t('neg ax\n sbb ax, ax', ALL)
t('add ax, bx\n adc dx, cx', ALL); t('sub ax, bx\n sbb dx, cx', ALL)
t('inc al\n adc bl, 0', ALL)
t('xor ax, ax\n inc ax\n dec bx\n cmc', ALL)
t('lea cx, [bx+si+7]', 0, 'mov si, 0x1234'); t('lea ecx, [ebx+eax*4+0x100]', 0)
t('mov cl, ah\n mov ch, bh\n mov dh, al', 0)

SPECIAL = [0, 1, 2, 0x7F, 0x80, 0x81, 0xFF, 0x100, 0x7FFF, 0x8000, 0x8001, 0xFFFF, 0x10000,
           0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFF, 0xFFFFFF80, 0xFFFF8000, 0x99, 0x09, 0x0F, 0x9A]


def val(rng):
    r = rng.random()
    if r < 0.3:
        return rng.choice(SPECIAL)
    if r < 0.45:
        return rng.randrange(0, 0x100) * rng.choice([1, 0x101, 0x10001, 0x1000000])
    if r < 0.55:
        return rng.choice(SPECIAL) ^ rng.choice([0, 0xFF00, 0xFFFF0000])
    return rng.getrandbits(32)


def cases(rng, per):
    out = []
    for ti in range(len(T)):
        for _ in range(per):
            fl = rng.choice([0, CF, ZF, CF | ZF, ALL]) if rng.random() < 0.3 else rng.getrandbits(12) & ALL
            out.append((ti, [val(rng) for _ in range(4)], fl | 2))
    return out


def body(mode):
    # the template subroutines, shared by both modes
    s = []
    for i, (name, pre, instr, mask) in enumerate(T):
        s.append(f'pre{i}:\n ' + (pre if pre else 'nop') + '\n ret')
        s.append(f'ins{i}:\n ' + instr + '\n ret')
    return '\n'.join(s)


HEX = '''
; hex8: EAX -> 8 hex digits at [DI]
hexd:   mov cx, 8
.l:     rol eax, 4
        mov bl, al
        and bl, 15
        add bl, '0'
        cmp bl, '9'
        jbe .d
        add bl, 7
.d:     mov [di], bl
        inc di
        loop .l
        ret
'''


def dos_prog(cs, first):
    n = len(cs)
    lines = ['bits 16', 'org 100h', 'cpu 586',
             'start:', ' mov word [pptr], cases', ' mov word [cnt], %d' % n, ' mov di, obuf',
             'next:', ' cmp word [cnt], 0', ' je done', ' mov si, [pptr]',
             ' mov eax, [si]', ' mov ebx, [si+4]', ' mov ecx, [si+8]', ' mov edx, [si+12]',
             ' mov [savedi], di',
             ' movzx di, byte [si+18]', ' shl di, 1', ' mov [tix], di',
             ' push word [si+16]', ' call [pret+di]', ' popf', ' call [inst+di]', ' pushf',
             ' pop word [fres]', ' mov [r0], eax', ' mov [r1], ebx', ' mov [r2], ecx', ' mov [r3], edx',
             ' mov di, [savedi]',
             ' mov si, [tix]', ' mov ax, [si+masks]', ' and [fres], ax',
             ' shr si, 1', ' add si, %d' % first, ' mov ax, si', ' mov [tnum], ax',
             ' movzx eax, word [tnum]', ' shl eax, 16', ' call hex4',
             ' mov byte [di], 32', ' inc di',
             ' mov eax, [r0]', ' call hexd', ' mov byte [di], 32', ' inc di',
             ' mov eax, [r1]', ' call hexd', ' mov byte [di], 32', ' inc di',
             ' mov eax, [r2]', ' call hexd', ' mov byte [di], 32', ' inc di',
             ' mov eax, [r3]', ' call hexd', ' mov byte [di], 32', ' inc di',
             ' movzx eax, word [fres]', ' shl eax, 16', ' call hex4',
             ' mov word [di], 0x0A0D', ' add di, 2',
             ' cmp di, obuf+3000', ' jb .nf', ' call flush', '.nf:',
             ' add word [pptr], 19', ' dec word [cnt]', ' jmp next',
             'done:', ' call flush', ' mov ax, 4C00h', ' int 21h',
             'flush:', ' mov cx, di', ' sub cx, obuf', ' mov dx, obuf', ' mov bx, 1', ' mov ah, 40h', ' int 21h',
             ' mov di, obuf', ' ret',
             'hex4: mov cx, 4', ' jmp hexd.l',
             HEX, body('dos')]
    lines.append('align 2')
    lines.append('pret: dw ' + ','.join(f'pre{i}' for i in range(len(T))))
    lines.append('inst: dw ' + ','.join(f'ins{i}' for i in range(len(T))))
    lines.append('masks: dw ' + ','.join(str(m) for (_, _, _, m) in T))
    lines += ['pptr dw 0', 'cnt dw 0', 'savedi dw 0', 'tix dw 0', 'tnum dw 0', 'fres dw 0',
              'r0 dd 0', 'r1 dd 0', 'r2 dd 0', 'r3 dd 0']
    lines.append('cases:')
    for ti, v, fl in cs:
        lines.append(' dd 0x%08X,0x%08X,0x%08X,0x%08X' % tuple(v) + '\n dw 0x%04X\n db %d' % (fl, ti - first))
    lines += ['obuf: times 3100 db 0']
    return '\n'.join(lines) + '\n'


def elf_prog(cs, first):
    n = len(cs)
    lines = ['bits 32', 'cpu 586', 'global _start', 'section .text', '_start:',
             ' mov dword [pptr], cases', ' mov dword [cnt], %d' % n, ' mov edi, obuf',
             'next:', ' cmp dword [cnt], 0', ' je done', ' mov esi, [pptr]',
             ' mov eax, [esi]', ' mov ebx, [esi+4]', ' mov ecx, [esi+8]', ' mov edx, [esi+12]',
             ' mov [savedi], edi',
             ' movzx edi, byte [esi+18]', ' mov [tix], edi',
             ' push word [esi+16]', ' call [pret+edi*4]', ' popfw', ' call [inst+edi*4]', ' pushfw',
             ' pop word [fres]', ' mov [r0], eax', ' mov [r1], ebx', ' mov [r2], ecx', ' mov [r3], edx',
             ' mov edi, [savedi]',
             ' mov esi, [tix]', ' mov ax, [esi*2+masks]', ' and [fres], ax',
             ' add esi, %d' % first, ' mov eax, esi', ' shl eax, 16', ' call hex4',
             ' mov byte [edi], 32', ' inc edi',
             ' mov eax, [r0]', ' call hexd', ' mov byte [edi], 32', ' inc edi',
             ' mov eax, [r1]', ' call hexd', ' mov byte [edi], 32', ' inc edi',
             ' mov eax, [r2]', ' call hexd', ' mov byte [edi], 32', ' inc edi',
             ' mov eax, [r3]', ' call hexd', ' mov byte [edi], 32', ' inc edi',
             ' movzx eax, word [fres]', ' shl eax, 16', ' call hex4',
             ' mov word [edi], 0x0A0D', ' add edi, 2',
             ' cmp edi, obuf+3000', ' jb .nf', ' call flush', '.nf:',
             ' add dword [pptr], 19', ' dec dword [cnt]', ' jmp next',
             'done:', ' call flush', ' mov eax, 1', ' xor ebx, ebx', ' int 0x80',
             'flush:', ' mov edx, edi', ' sub edx, obuf', ' mov ecx, obuf', ' mov ebx, 1', ' mov eax, 4', ' int 0x80',
             ' mov edi, obuf', ' ret',
             'hex4: mov ecx, 4', ' jmp hexd.l',
             HEX.replace('mov cx, 8', 'mov ecx, 8').replace('[di]', '[edi]').replace('inc di', 'inc edi')
                .replace('loop .l', 'dec ecx\n jnz .l'),
             body('elf')]
    lines.append('section .data')
    lines.append('align 4')
    lines.append('pret: dd ' + ','.join(f'pre{i}' for i in range(len(T))))
    lines.append('inst: dd ' + ','.join(f'ins{i}' for i in range(len(T))))
    lines.append('masks: dw ' + ','.join(str(m) for (_, _, _, m) in T))
    lines += ['pptr dd 0', 'cnt dd 0', 'savedi dd 0', 'tix dd 0', 'fres dw 0',
              'r0 dd 0', 'r1 dd 0', 'r2 dd 0', 'r3 dd 0']
    lines.append('cases:')
    for ti, v, fl in cs:
        lines.append(' dd 0x%08X,0x%08X,0x%08X,0x%08X' % tuple(v) + '\n dw 0x%04X\n db %d' % (fl, ti - first))
    lines += ['section .bss', 'obuf: resb 3100']
    return '\n'.join(lines) + '\n'


def main():
    global T
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    rng = random.Random(1988)
    per = 24
    allc = cases(rng, per)
    # split into programs of at most ~110 templates (the .COM stays < 64 KB)
    group = 90
    names = []
    for g in range(0, len(T), group):
        sub = [c for c in allc if g <= c[0] < g + group]
        # the DOS program indexes templates relative to the group
        full = T
        T = full[g:g + group]
        k = g // group + 1
        with open(os.path.join(out, f'cput{k}.asm'), 'w') as f:
            f.write(dos_prog(sub, g))
        with open(os.path.join(out, f'cput{k}_elf.asm'), 'w') as f:
            f.write(elf_prog(sub, g))
        T = full
        names.append(k)
    with open(os.path.join(out, 'templates.txt'), 'w') as f:
        for i, (name, pre, instr, mask) in enumerate(T):
            f.write('%d\t%s\t%s\n' % (i, instr.replace('\n', ';'), pre.replace('\n', ';')))
    print(' '.join(str(k) for k in names))


if __name__ == '__main__':
    main()
