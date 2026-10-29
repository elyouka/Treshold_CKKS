#!/usr/bin/env python3
"""Vereinfachte Plots fuer die Speicherhierarchie-Counter (Google-Benchmark-JSONs eines Ordners).

Counter (Namen unten bei C_* anpassbar):
    DATA_CACHE_REFILLS_FROM_SYSTEM:MABRESP_LCL_L2 / :LS_MABRESP_LCL_CACHE / :LS_MABRESP_LCL_DRAM
    L2_PREFETCH_HIT_L3, L2_PREFETCH_MISS_L3

Aufruf:
    python3 plot_mem.py <json-ordner> -o plots/ntt_mem [--rings 14 15 16]
                                   [--prefix NAME] [--overview-only]

plot_overview.png enthaelt drei Panels:
  1. Woher kommen die L1d-Demand-Refills?  100%-gestapelt: L2 / L3 / DRAM
  2. Aus DRAM geholte Cachelines [GB/s]: Demand (voll) vs. L2-Prefetcher (schraffiert), log. Achse
     (ohne Write-Backs und ohne L1-Prefetcher-Fills)
  3. L2-Prefetcher-Requests pro Iteration [Mio.]: trifft L3 / geht bis DRAM (gestapelt)
Farbe = Ebene (L2 / L3 / DRAM), Schraffur = Prefetcher.
Einzelplots: plot_sources.png, plot_dram_traffic.png, plot_prefetch.png; dazu summary.csv.
Alle Werte = Median ueber die Repetitions. 1 Event = 1 Cacheline = 64 B (Annahme).
"""
import argparse
import csv
import glob
import json
import os
from collections import defaultdict

import matplotlib
import numpy as np
from matplotlib.patches import Patch

matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ---- Anpassen, falls andere Counter/Namen verwendet werden -----------------
RING_KEYS = ("ring-dim", "ring dimension")  # Counter-Namen fuer log2 N
C_L2 = "DATA_CACHE_REFILLS_FROM_SYSTEM:MABRESP_LCL_L2"
C_L3 = "DATA_CACHE_REFILLS_FROM_SYSTEM:LS_MABRESP_LCL_CACHE"
C_DRAM = "DATA_CACHE_REFILLS_FROM_SYSTEM:LS_MABRESP_LCL_DRAM"
C_PF_L3 = "L2_PREFETCH_HIT_L3"
C_PF_DRAM = "L2_PREFETCH_MISS_L3"
LINE_BYTES = 64
# -----------------------------------------------------------------------------

UNIT_TO_MS = {"ns": 1e-6, "us": 1e-3, "ms": 1.0, "s": 1e3}
RINGS = None
VIR = plt.cm.viridis
COL_L2, COL_L3, COL_DRAM = VIR(0.15), VIR(0.55), VIR(0.92)   # Farbe = Ebene
HATCH = "//"                                                 # Schraffur = Prefetcher


def ring_of(entry):
    for k in RING_KEYS:
        if k in entry:
            return int(round(entry[k]))
    try:
        idx = int(entry["run_name"].split(":")[-1])
    except ValueError:
        idx = None
    if RINGS is not None and idx is not None and idx < len(RINGS):
        return RINGS[idx]
    raise SystemExit(f"Ring-Counter {RING_KEYS} fehlt in {entry['run_name']}. "
                     "Mit --rings die Ringdimensionen (log2 N) in der Reihenfolge der "
                     "Config-Indizes angeben, z. B. --rings 14 15 16")


def load(folder):
    runs = defaultdict(list)
    medians = {}
    files = sorted(glob.glob(os.path.join(folder, "*.json")))
    if not files:
        raise SystemExit(f"Keine .json-Dateien in {folder}")
    for path in files:
        try:
            benchmarks = json.load(open(path))["benchmarks"]
        except (json.JSONDecodeError, KeyError):
            print(f"WARNUNG: {path} ist leer/ungueltig -> uebersprungen")
            continue
        for b in benchmarks:
            key = (b["run_name"].split("/")[0], ring_of(b))
            if b["run_type"] == "iteration":
                runs[key].append(b)
            elif b.get("aggregate_name") == "median":
                medians[key] = b
    keys = sorted(set(runs) | set(medians), key=lambda k: (k[1], k[0]))
    print("Benchmarks:", ", ".join(sorted({b for b, _ in keys})),
          "| log2N:", ", ".join(str(r) for r in sorted({r for _, r in keys})))
    return keys, runs, medians


def getc(e, col):
    """Counter-Wert; exakter Name, sonst Treffer ueber den Teil nach dem ':' ('LS_'-Praefix egal)."""
    if col in e:
        return e[col]
    tail = col.split(":")[-1].lower()
    for k, v in e.items():
        if not isinstance(v, (int, float)):
            continue
        kt = k.lower().split(":")[-1]
        if kt == tail or kt.endswith("_" + tail) or tail.endswith("_" + kt):
            return v
    raise KeyError(col)


def time_s(e):
    return e["real_time"] * UNIT_TO_MS[e["time_unit"]] * 1e-3


def stat(key, fn, runs, medians):
    """(Median, Min, Max, Anzahl) von fn(e) ueber die Repetitions; None wenn nicht vorhanden."""
    vals = []
    for e in runs.get(key, []):
        try:
            vals.append(fn(e))
        except (KeyError, ZeroDivisionError):
            pass
    vals = np.array([v for v in vals if np.isfinite(v)], float)
    if vals.size:
        return float(np.median(vals)), float(vals.min()), float(vals.max()), int(vals.size)
    if key in medians:
        try:
            v = fn(medians[key])
            return v, v, v, 1
        except (KeyError, ZeroDivisionError):
            pass
    return None


# ------------------------------------------------------------------- Metriken
def share(col):
    return lambda e: getc(e, col) / (getc(e, C_L2) + getc(e, C_L3) + getc(e, C_DRAM)) * 100.0


def gbs(col):
    return lambda e: getc(e, col) * LINE_BYTES / time_s(e) / 1e9


def count_m(col):
    return lambda e: getc(e, col) / 1e6


def pf_share(e):
    return getc(e, C_PF_DRAM) / (getc(e, C_PF_L3) + getc(e, C_PF_DRAM)) * 100.0


METRICS = {
    "l3_share": share(C_L3), "dram_share": share(C_DRAM),
    "gbs_demand_dram": gbs(C_DRAM), "gbs_prefetch_dram": gbs(C_PF_DRAM),
    "pf_l3_mreq": count_m(C_PF_L3), "pf_dram_mreq": count_m(C_PF_DRAM),
    "pf_dram_share": pf_share,
}


def collect(keys, runs, medians):
    cells = []  # (benchmark, ring, {metric: (med, min, max, n)})
    for key in keys:
        d = {n: stat(key, fn, runs, medians) for n, fn in METRICS.items()}
        cells.append((key[0], key[1], d))
    if not any(v for _, _, d in cells for v in d.values()):
        raise SystemExit("Keine Speicherhierarchie-Counter in den Dateien gefunden (C_* pruefen).")
    return cells


# -------------------------------------------------------------------- Panels
def layout(cells):
    """x-Position je Zelle: Gruppen je Ringdimension, Benchmarks nebeneinander."""
    rings = sorted({r for _, r, _ in cells})
    benches = sorted({b for b, _, _ in cells})
    nb = len(benches)
    pos = [rings.index(r) * (nb + 0.8) + benches.index(b) for b, r, _ in cells]
    labels = [(f"{b}\nN = 2^{r}" if nb > 1 else f"N = 2^{r}") for b, r, _ in cells]
    return np.array(pos, float), labels, nb


def finish(ax, cells, title, ylabel, fontsize=8):
    pos, labels, nb = layout(cells)
    ax.set_xticks(pos)
    ax.set_xticklabels(labels, fontsize=fontsize - (1 if nb > 1 else 0),
                       rotation=30 if nb > 1 else 0, ha="right" if nb > 1 else "center")
    ax.set_title(title, fontsize=fontsize + 3)
    ax.set_ylabel(ylabel)
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)


def panel_sources(ax, cells):
    pos, _, nb = layout(cells)
    w = 0.8
    for x, (_, _, d) in zip(pos, cells):
        if not (d["l3_share"] and d["dram_share"]):
            continue
        l3, dr = d["l3_share"][0], d["dram_share"][0]
        l2 = 100.0 - l3 - dr
        ax.bar(x, l2, w, color=COL_L2)
        ax.bar(x, l3, w, bottom=l2, color=COL_L3)
        ax.bar(x, dr, w, bottom=l2 + l3, color=COL_DRAM)
        ax.text(x, l2 / 2, f"{l2:.1f}%", ha="center", va="center", color="white", fontsize=8)
        ax.text(x, 103, f"L3 {l3:.1f}%\nDRAM {dr:.2g}%", ha="center", va="bottom", fontsize=7)
    ax.set_ylim(0, 132)
    ax.set_yticks(range(0, 101, 20))
    finish(ax, cells, "Woher kommen die L1d-Demand-Refills?", "% aller L1d-Demand-Refills")
    ax.legend(handles=[Patch(color=COL_DRAM, label="DRAM"), Patch(color=COL_L3, label="L3"),
                       Patch(color=COL_L2, label="L2")], loc="upper center", ncol=3, fontsize=8, framealpha=0.9)


def panel_dram(ax, cells):
    pos, _, nb = layout(cells)
    w = 0.38
    vals = []
    for x, (_, _, d) in zip(pos, cells):
        for off, name, hatch in ((-0.2, "gbs_demand_dram", None), (0.2, "gbs_prefetch_dram", HATCH)):
            s = d[name]
            if not s or s[0] <= 0:
                continue
            ax.bar(x + off, s[0], w, color=COL_DRAM, hatch=hatch, edgecolor="white", linewidth=0,
                   yerr=[[s[0] - s[1]], [s[2] - s[0]]], capsize=2)
            ax.text(x + off, s[0] * 1.15, f"{s[0]:.2g}", ha="center", va="bottom", fontsize=7, rotation=90)
            vals.append(s[0])
    if vals:
        ax.set_yscale("log")
        ax.set_ylim(min(vals) / 3, max(vals) * 12)
    finish(ax, cells, "Aus DRAM geholte Cachelines:\nDemand vs. L2-Prefetcher", "Aus DRAM geholte Daten [GB/s] (log)")
    ax.legend(handles=[Patch(facecolor=COL_DRAM, label="Demand (Core wartet)"),
                       Patch(facecolor=COL_DRAM, hatch=HATCH, edgecolor="white", label="L2-Prefetcher")],
              loc="upper left", fontsize=8, framealpha=0.9)


def panel_prefetch(ax, cells):
    pos, _, nb = layout(cells)
    w = 0.8
    top = 0
    for x, (_, _, d) in zip(pos, cells):
        if not (d["pf_l3_mreq"] and d["pf_dram_mreq"]):
            continue
        a, b = d["pf_l3_mreq"][0], d["pf_dram_mreq"][0]
        ax.bar(x, a, w, color=COL_L3, hatch=HATCH, edgecolor="white", linewidth=0)
        ax.bar(x, b, w, bottom=a, color=COL_DRAM, hatch=HATCH, edgecolor="white", linewidth=0)
        sh = d["pf_dram_share"][0] if d["pf_dram_share"] else 100 * b / (a + b)
        ax.text(x, a + b, f"{a + b:.2g} Mio.\nDRAM-Anteil {sh:.0f}%", ha="center", va="bottom", fontsize=7)
        top = max(top, a + b)
    ax.set_ylim(0, top * 1.25 if top else 1)
    finish(ax, cells, "L2-Prefetcher-Requests (die L2 verfehlen)", "Requests pro Iteration [Mio.]")
    ax.legend(handles=[Patch(facecolor=COL_DRAM, hatch=HATCH, edgecolor="white", label="Prefetch geht bis DRAM"),
                       Patch(facecolor=COL_L3, hatch=HATCH, edgecolor="white", label="Prefetch trifft im L3")],
              loc="upper left", fontsize=8, framealpha=0.9)


PANELS = [("plot_sources.png", panel_sources), ("plot_dram_traffic.png", panel_dram),
          ("plot_prefetch.png", panel_prefetch)]
def note_text(cells):
    """Fusszeile inkl. Anzahl der Repetitionen (Minimum/Maximum ueber alle Balken)."""
    reps = [s[3] for _, _, d in cells for s in d.values() if s]
    lo, hi = (min(reps), max(reps)) if reps else (0, 0)
    if hi <= 1:
        what = "Einzelmessung (keine Wiederholungen)"
    else:
        what = f"Median ueber {lo if lo == hi else f'{lo}-{hi}'} Repetitions"
    return (f"Werte = {what} (Fehlerbalken im DRAM-Panel = Min/Max). "
            "Farbe = Ebene, Schraffur = L2-Prefetcher (kein Demand-Traffic). Annahme: 1 Event = 1 Cacheline = 64 B.")


def save(fig, path, note):
    fig.text(0.01, 0.004, note, fontsize=8)
    fig.savefig(path, dpi=160)
    plt.close(fig)
    print("geschrieben:", path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("-o", "--output", default=".")
    ap.add_argument("--prefix", default="")
    ap.add_argument("--rings", type=int, nargs="+", metavar="LOG2N",
                    help="log2 N je Config-Index (nur noetig, wenn der Ring-Counter fehlt)")
    ap.add_argument("--overview-only", action="store_true", help="nur plot_overview.png")
    args = ap.parse_args()
    global RINGS
    RINGS = args.rings
    os.makedirs(args.output, exist_ok=True)

    keys, runs, medians = load(args.folder)
    cells = collect(keys, runs, medians)
    ncell = len(cells)
    note = note_text(cells)

    fig, axs = plt.subplots(1, 3, figsize=(max(16, 1.0 * ncell * 3 + 6), 6.2))
    for ax, (_, fn) in zip(axs, PANELS):
        fn(ax, cells)
    fig.tight_layout(rect=(0, 0.03, 1, 1))
    save(fig, os.path.join(args.output, args.prefix + "plot_overview.png"), note)

    if not args.overview_only:
        for fname, fn in PANELS:
            fig, ax = plt.subplots(figsize=(max(7, 0.9 * ncell + 4), 5.8))
            fn(ax, cells)
            fig.tight_layout(rect=(0, 0.03, 1, 1))
            save(fig, os.path.join(args.output, args.prefix + fname), note)

    path = os.path.join(args.output, args.prefix + "summary.csv")
    with open(path, "w", newline="") as f:
        wr = csv.writer(f)
        wr.writerow(["benchmark", "log2N", "metric", "median", "min", "max", "repetitions"])
        for b, r, d in cells:
            for name, s in d.items():
                if s:
                    wr.writerow([b, r, name, *s])
    print("geschrieben:", path)


if __name__ == "__main__":
    main()
