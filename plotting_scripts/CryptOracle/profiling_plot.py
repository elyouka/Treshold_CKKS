#!/usr/bin/env python3
"""Plots aus einer CryptOracle-Primitive-CSV mit mehreren Runs pro Ringdimension.

Aufruf:  python3 plot_cryptoracle.py <csv> [ausgabeordner]

Die Spaltennamen im Header ("<Primitive> <Metrik>") werden direkt gelesen, die
Reihenfolge der perf-Events in der CSV spielt also keine Rolle. Voraussetzung:
Header und Datenzeilen haben gleich viele Felder.

Darstellung: Balken = Median ueber die Runs einer Ringdimension, Punkte = die
einzelnen Runs. Der Median ist robust gegen einzelne Ausreisser (bei 3 Runs
verfaelscht ein Ausreisser den Mittelwert stark).
"""
import sys
import warnings

import matplotlib
import numpy as np
import pandas as pd

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

CSV = sys.argv[1] if len(sys.argv) > 1 else "primitive-results-profiling_run.csv"
OUT = sys.argv[2] if len(sys.argv) > 2 else "."

# Events fuer "Misses pro 1000 Instruktionen": (Spaltenname, Dateiname, Titel)
MISS_EVENTS = [
    ("L1-dcache-load-misses", "plot_l1d_mpki.png", "L1d-Cache-Load-Misses"),
    ("l2_cache_req_stat.ic_dc_miss_in_l2", "plot_l2_mpki.png", "L2-Cache-Misses (ic_dc_miss_in_l2)"),
    ("dTLB-load-misses", "plot_dtlb_mpki.png", "dTLB-Load-Misses"),
]

df = pd.read_csv(CSV)
prims = []
for c in df.columns:
    if c.startswith("CKKS_"):
        p = c.split(" ", 1)[0]
        if p not in prims:
            prims.append(p)

ns = sorted(df["n"].unique())
runs = {n: df[df["n"] == n] for n in ns}
R = max(len(r) for r in runs.values())


def col(p, metric):
    """Werte als Array (Ringdimensionen x Runs), fehlende Runs/NaN als NaN."""
    out = np.full((len(ns), R), np.nan)
    for i, n in enumerate(ns):
        v = pd.to_numeric(runs[n][f"{p} {metric}"], errors="coerce").to_numpy(float)
        out[i, : len(v)] = v
    return out


def mpki(event):
    def f(p):
        with np.errstate(divide="ignore", invalid="ignore"):
            v = col(p, event) / col(p, "instructions") * 1000
        v[~np.isfinite(v)] = np.nan
        return v
    return f


def grouped_bars(getter, ylabel, title, fname, log=False):
    data = np.array([getter(p) for p in prims])          # Primitive x n x Runs
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", RuntimeWarning)  # "All-NaN slice"
        med = np.nanmedian(data, axis=2)                 # Primitive x n

    labels = [p.replace("CKKS_", "") + ("*" if np.isnan(data[k]).any() else "")
              for k, p in enumerate(prims)]
    x = np.arange(len(prims))
    w = 0.8 / len(ns)
    cols = plt.cm.viridis(np.linspace(0.15, 0.85, len(ns)))

    fig, ax = plt.subplots(figsize=(17, 6.5))
    for i, n in enumerate(ns):
        xs = x + (i - (len(ns) - 1) / 2) * w
        ax.bar(xs, med[:, i], w, label=f"n = {n}", color=cols[i])
        for r in range(R):                               # einzelne Runs als Punkte
            jitter = (r - (R - 1) / 2) * w * 0.22
            ax.scatter(xs + jitter, data[:, i, r], s=14, zorder=3,
                       facecolor="white", edgecolor="black", linewidth=0.6)
    ax.set_xticks(x)
    ax.set_xticklabels(labels, rotation=60, ha="right")
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    if log:
        ax.set_yscale("log")
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)
    handles, _ = ax.get_legend_handles_labels()
    handles.append(Line2D([], [], marker="o", ls="", mfc="white", mec="black",
                          ms=4, label="einzelner Run"))
    ax.legend(handles=handles, title="Ringdimension (log2 N)")

    note = f"Balken = Median ueber {R} Runs, Punkte = einzelne Runs."
    if any("*" in l for l in labels):
        note += "  * = mindestens ein Run ohne Messwert (NaN)."
    no_data = [l for l, m in zip(labels, med) if np.isnan(m).any()]
    if no_data:
        note += "  Fehlende Balken (kein gueltiger Run): " + ", ".join(no_data)
    fig.text(0.01, 0.005, note, fontsize=8)
    fig.tight_layout(rect=(0, 0.03, 1, 1))
    fig.savefig(f"{OUT}/{fname}", dpi=160)
    plt.close(fig)
    print("geschrieben:", fname)


grouped_bars(lambda p: col(p, "Time"), "Zeit [s] (log)",
             "Laufzeit pro Primitive", "plot_time.png", log=True)
grouped_bars(lambda p: col(p, "IPC"), "Instruktionen pro Zyklus",
             "IPC pro Primitive", "plot_ipc.png")
for event, fname, title in MISS_EVENTS:
    grouped_bars(mpki(event), "Misses / 1000 Instruktionen (log)",
                 f"{title} pro 1000 Instruktionen", fname, log=True)
