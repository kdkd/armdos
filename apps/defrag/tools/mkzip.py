#!/usr/bin/env python3
"""apps/defrag/tools/mkzip.py - build the BBS distribution archive.

mkzip.py OUT.ZIP file...  -> a DEFLATE zip (what ARM-DOS's UNZIP reads) with
8.3 upper-case names, text files (.DOC/.DIZ/.TXT) stored with CR LF, and one
fixed period timestamp so the archive is reproducible.
"""
import os, sys, zipfile

STAMP = (1990, 11, 12, 1, 10, 0)     # "v1.10"

def main():
    out, files = sys.argv[1], sys.argv[2:]
    tmp = out + '.tmp'
    with zipfile.ZipFile(tmp, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for f in files:
            name = os.path.basename(f).upper()
            data = open(f, 'rb').read()
            if name.rsplit('.', 1)[-1] in ('DOC', 'DIZ', 'TXT'):
                data = data.replace(b'\r\n', b'\n').replace(b'\n', b'\r\n')
            zi = zipfile.ZipInfo(name, STAMP)
            zi.compress_type = zipfile.ZIP_DEFLATED
            zi.create_system = 0            # MS-DOS
            zi.external_attr = 0x20         # archive
            z.writestr(zi, data)
    os.replace(tmp, out)

main()
