#!/usr/bin/env python3
"""Plots aus Google-Benchmark-JSON-Dateien (alle *.json eines Ordners).

Aufruf:
    python3 plot_benchmarks.py <json-ordner> [-o ausgabeordner] [--prefix NAME]
                               [--by-bench] [--log-time]
python3 plot_primitive.py standardboot_jason -o plots/ckksboot 


Erwartet pro Eintrag die Counter CYCLES, INSTRUCTIONS und die Miss-Events
(siehe MISS_EVENTS) sowie den Counter "ring-dim" (= log2 N). Der Benchmark-Name
ist der Teil von run_name vor dem ersten "/".

Alle Metriken zusammen in einer Abbildung:
    plot_overview.png  (nur diese Datei mit --overview-only)
Zusaetzlich pro Metrik ein Einzelplot:
    plot_time.png      Laufzeit pro Iteration
    plot_ipc.png       Instruktionen pro Zyklus
    plot_l1d_mpki.png  L1d-Load-Misses pro 1000 Instruktionen
    plot_l2_mpki.png   L2-Demand-Misses pro 1000 Instruktionen
    plot_dtlb_mpki.png dTLB-Load-Misses pro 1000 Instruktionen
und eine summary.csv mit allen Medianwerten.

Balken = Median ueber die Repetitions, Fehlerbalken = Min/Max.
Standard-Layout: x = Ringdimension, Balken = Benchmark.
Mit --by-bench: x = Benchmark, Balken = Ringdimension.
"""
import argparse
import csv
import glob
import json
import os
import textwrap
from collections import defaultdict

import matplotlib
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ---- Anpassen, falls andere Counter/Namen verwendet werden -----------------
INSTR = "INSTRUCTIONS"
CYCLES = "CYCLES"
RING_KEY = "ring-dim"
MISS_EVENTS = [  # (JSON-Spalte, Dateiname, Titel)
    ("L1-DCACHE-LOAD-MISSES", "plot_l1d_mpki.png", "L1d-Load-Misses"),
    ("CORE_TO_L2_CACHEABLE_REQUEST_ACCESS_STATUS:LS_RD_BLK_C", "plot_l2_mpki.png",
     "L2-Demand-Misses (LS_RD_BLK_C, ohne Prefetch)"),
    ("DTLB-LOAD-MISSES", "plot_dtlb_mpki.png", "dTLB-Load-Misses"),
]
# -----------------------------------------------------------------------------

UNIT_TO_MS = {"ns": 1e-6, "us": 1e-3, "ms": 1.0, "s": 1e3}


RINGS = None  # wird in main() aus --rings gesetzt


def ring_of(entry):
    """log2 N aus dem Counter "ring-dim"; ohne Counter aus --rings (Index = Config-Argument)."""
    if RING_KEY in entry:
        return int(round(entry[RING_KEY]))
    try:
        idx = int(entry["run_name"].split(":")[-1])
    except ValueError:
        idx = None
    if RINGS is not None and idx is not None and idx < len(RINGS):
        return RINGS[idx]
    raise SystemExit(f"Counter '{RING_KEY}' fehlt in {entry['run_name']}. "
                     "Mit --rings die Ringdimensionen (log2 N) in der Reihenfolge der "
                     "Config-Indizes angeben, z. B. --rings 14 15 16")


def load(folder):
    runs = defaultdict(list)  # (benchmark, ring) -> Iterations-Eintraege
    medians = {}              # (benchmark, ring) -> Median-Aggregat (Fallback)
    files = sorted(glob.glob(os.path.join(folder, "*.json")))
    if not files:
        raise SystemExit(f"Keine .json-Dateien in {folder}")
    for path in files:
        for b in json.load(open(path))["benchmarks"]:
            key = (b["run_name"].split("/")[0], ring_of(b))
            if b["run_type"] == "iteration":
                runs[key].append(b)
            elif b.get("aggregate_name") == "median":
                medians[key] = b
    keys = sorted(set(runs) | set(medians))
    return keys, runs, medians


def time_ms(e):
    return e["real_time"] * UNIT_TO_MS[e["time_unit"]]


def ipc(e):
    return e[INSTR] / e[CYCLES]


def mpki(col):
    return lambda e: e[col] / e[INSTR] * 1000.0


def stat(key, fn, runs, medians):
    """(Median, Min, Max) einer Metrik ueber die Repetitions, None wenn nicht vorhanden."""
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


def collect(fn, keys, runs, medians, args):
    """Matrix aus Median/Min/Max pro (Benchmark, Ringdimension); None wenn kein Messwert."""
    benches = sorted({b for b, _ in keys})
    rings = sorted({r for _, r in keys})
    shape = (len(benches), len(rings))
    med, lo, hi = (np.full(shape, np.nan) for _ in range(3))
    reps = []
    for i, b in enumerate(benches):
        for j, r in enumerate(rings):
            s = stat((b, r), fn, runs, medians)
            if s:
                med[i, j], lo[i, j], hi[i, j] = s[:3]
                reps.append(s[3])
    if np.isnan(med).all():
        return None
    if args.by_bench:
        cats, sers = list(benches), [f"N = 2^{r}" for r in rings]
    else:
        cats, sers = [f"N = 2^{r}" for r in rings], list(benches)
        med, lo, hi = med.T, lo.T, hi.T
    return cats, sers, med, lo, hi, min(reps)


def draw(ax, data, ylabel, title, log=False, fontsize=8, wrap=80):
    """Zeichnet gruppierte Balken auf ax, gibt (Handles, Labels) fuer die Legende zurueck."""
    cats, sers, med, lo, hi, _ = data
    ncat, nser = med.shape
    x = np.arange(ncat)
    w = 0.8 / nser
    cols = plt.cm.viridis(np.linspace(0.15, 0.85, nser))
    annotate = ncat * nser <= 14
    for k in range(nser):
        xs = x + (k - (nser - 1) / 2) * w
        h = med[:, k]
        ok = np.isfinite(h)
        err = np.vstack([np.nan_to_num(h - lo[:, k]), np.nan_to_num(hi[:, k] - h)])
        ax.bar(xs[ok], h[ok], w, yerr=err[:, ok], capsize=2, color=cols[k], label=sers[k])
        if annotate:
            for xi, val in zip(xs[ok], h[ok]):
                ax.text(xi, val, f"{val:.3g}", ha="center", va="bottom", fontsize=fontsize)
    ax.set_xticks(x)
    ax.set_xticklabels(cats, rotation=0 if ncat <= 6 else 60, ha="center" if ncat <= 6 else "right")
    ax.set_ylabel(ylabel)
    ax.set_title("\n".join(textwrap.wrap(title, wrap)), fontsize=fontsize + 3)
    if log:
        ax.set_yscale("log")
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)
    ax.margins(y=0.15)
    return ax.get_legend_handles_labels()


def legend_title(args):
    return "Ringdimension" if args.by_bench else "Benchmark"


def make_plot(fn, ylabel, title, fname, keys, runs, medians, args, log=False):
    data = collect(fn, keys, runs, medians, args)
    if data is None:
        print(f"uebersprungen: {title} (Counter nicht in den Dateien)")
        return
    ncat, nser = data[2].shape
    fig, ax = plt.subplots(figsize=(max(8, 1.6 * ncat * max(1, nser / 2)), 5.5))
    handles, labels = draw(ax, data, ylabel, title, log)
    ax.legend(handles, labels, title=legend_title(args), loc="upper left", bbox_to_anchor=(1.01, 1.0))
    fig.text(0.01, 0.005, f"Balken = Median ueber {data[5]} Repetitions, Fehlerbalken = Min/Max.",
             fontsize=8)
    fig.tight_layout(rect=(0, 0.03, 1, 1))
    out = os.path.join(args.output, args.prefix + fname)
    fig.savefig(out, dpi=160)
    plt.close(fig)
    print("geschrieben:", out)


def make_overview(metrics, keys, runs, medians, args):
    """Alle Metriken als Panels in einer Abbildung; Legende im letzten freien Panel."""
    items = []
    for name, fn, ylabel, title, fname, log in metrics:
        d = collect(fn, keys, runs, medians, args)
        if d is None:
            print(f"uebersprungen: {title} (Counter nicht in den Dateien)")
        else:
            items.append((d, ylabel, title, log))
    if not items:
        return
    ncols = 3
    nrows = int(np.ceil((len(items) + 1) / ncols))      # +1 Panel fuer die Legende
    fig, axs = plt.subplots(nrows, ncols, figsize=(6.2 * ncols, 4.6 * nrows))
    axs = np.atleast_1d(axs).ravel()
    handles = labels = None
    for ax, (d, ylabel, title, log) in zip(axs, items):
        h, l = draw(ax, d, ylabel, title, log, fontsize=7, wrap=40)
        if handles is None:
            handles, labels = h, l
    leg = axs[len(items)]
    leg.axis("off")
    leg.legend(handles, labels, title=legend_title(args), loc="center", fontsize=11, title_fontsize=12)
    for ax in axs[len(items) + 1:]:
        ax.axis("off")
    reps = min(d[5] for d, *_ in items)
    fig.text(0.01, 0.005, f"Balken = Median ueber {reps} Repetitions, Fehlerbalken = Min/Max.", fontsize=8)
    fig.tight_layout(rect=(0, 0.02, 1, 1))
    out = os.path.join(args.output, args.prefix + "plot_overview.png")
    fig.savefig(out, dpi=160)
    plt.close(fig)
    print("geschrieben:", out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("-o", "--output", default=".")
    ap.add_argument("--prefix", default="")
    ap.add_argument("--by-bench", action="store_true",
                    help="x = Benchmark, Balken = Ringdimension")
    ap.add_argument("--log-time", action="store_true", help="logarithmische Zeitachse")
    ap.add_argument("--rings", type=int, nargs="+", metavar="LOG2N",
                    help="log2 N je Config-Index (nur noetig, wenn der Counter 'ring-dim' fehlt), "
                         "z. B. --rings 14 15 16")
    ap.add_argument("--overview-only", action="store_true",
                    help="nur plot_overview.png (alle Metriken in einer Abbildung), keine Einzelplots")
    args = ap.parse_args()
    global RINGS
    RINGS = args.rings
    os.makedirs(args.output, exist_ok=True)

    keys, runs, medians = load(args.folder)

    # Zeiteinheit automatisch: Sekunden, wenn ein Median ueber 1000 ms liegt (z. B. Bootstrapping)
    entries = [e for es in runs.values() for e in es] + list(medians.values())
    use_s = max(time_ms(e) for e in entries) > 1000
    time_fn = (lambda e: time_ms(e) / 1000.0) if use_s else time_ms
    unit = "s" if use_s else "ms"

    metrics = [(f"time_{unit}", time_fn, f"Zeit pro Iteration [{unit}]", "Laufzeit",
                "plot_time.png", args.log_time),
               ("ipc", ipc, "Instruktionen pro Zyklus", "IPC", "plot_ipc.png", False)]
    for col, fname, title in MISS_EVENTS:
        metrics.append((f"{col}_per_1000_instr", mpki(col), "Misses / 1000 Instruktionen",
                        f"{title} pro 1000 Instruktionen", fname, False))

    make_overview(metrics, keys, runs, medians, args)
    if not args.overview_only:
        for name, fn, ylabel, title, fname, log in metrics:
            make_plot(fn, ylabel, title, fname, keys, runs, medians, args, log=log)

    with open(os.path.join(args.output, args.prefix + "summary.csv"), "w", newline="") as f:
        wr = csv.writer(f)
        wr.writerow(["benchmark", "log2N", "metric", "median", "min", "max", "repetitions"])
        for (b, r) in keys:
            for name, fn, *_ in metrics:
                s = stat((b, r), fn, runs, medians)
                if s:
                    wr.writerow([b, r, name, s[0], s[1], s[2], s[3]])
    print("geschrieben:", os.path.join(args.output, args.prefix + "summary.csv"))


if __name__ == "__main__":
    main()
