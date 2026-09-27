#!/usr/bin/env python3
"""Annotated spectrogram of a rendered handshake (emu/tests/modemsound/render.mjs output).
usage: python spectrogram.py MOD.wav MOD.phases.json out.png  (needs numpy, scipy, matplotlib)"""
import sys, json, numpy as np, scipy.io.wavfile as wf, scipy.signal as sg
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
w, ph, out = sys.argv[1:4]
sr, x = wf.read(w); x = x.astype(float) / 32768
f, t, S = sg.spectrogram(x, sr, nperseg=256, noverlap=208)
S = 10 * np.log10(S + 1e-12)
fig, ax = plt.subplots(figsize=(20, 6.5))
ax.pcolormesh(t, f, S, vmin=S.max() - 70, vmax=S.max(), cmap='magma', shading='auto')
ax.set_ylabel('Hz'); ax.set_xlabel('seconds after the answering modem goes off hook'); ax.set_xticks(np.arange(0, t[-1] + 1, 1))
for i, p in enumerate(json.load(open(ph))):
    y = 4000 + 170 * (i % 4)
    ax.annotate(p['name'], xy=(p['t0'], 3950), xytext=(p['t0'], y), fontsize=7.5, color='black', annotation_clip=False,
                arrowprops=dict(arrowstyle='-', color='gray', lw=.6))
ax.set_ylim(0, 4000); ax.set_title(w.split('/')[-1] + ' (procedural, 8 kHz)', pad=80)
plt.tight_layout(); plt.savefig(out, dpi=70)
