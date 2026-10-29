#!/usr/bin/env python3
"""Plots aus Google-Benchmark-JSON-Dateien (alle *.json eines Ordners) fuer die Dispatch-Stall-Counter.

Aufruf:
    python3 plot_stalls.py <json-ordner> -o plots/relin_stalls --rings 14 15 16
                           [--prefix NAME] [--log-time] [--abs] [--linear] [--overview-only]

Abbildung plot_overview.png (alles in einem):
    oben:  Laufzeit pro Iteration und IPC (IPC nur, wenn INSTRUCTIONS und CYCLES im JSON stehen)
    unten: je Ringdimension ein Panel mit ALLEN Stall-Counter, x = Stall-Typ, Balken = Benchmark.
           Alle Stall-Panels teilen sich dieselbe y-Achse (logarithmisch) und die Stall-Typen sind
           nach Groesse sortiert (groesster links) -> direkt vergleichbar.
Zusaetzlich: plot_time.png, plot_ipc.png, plot_stalls.png (nur die Stall-Panels), summary.csv.

Stall-Metrik: Anteil der Zyklen [%] = Stall-Zyklen / CYCLES * 100. Ist CYCLES nicht im JSON (oder
mit --abs): absolute Stall-Zyklen pro Iteration. Die Stall-Counter ueberlappen sich (mehrere
Ressourcen koennen im selben Zyklus blockieren) -> Werte einzeln lesen, nicht aufsummieren.

Balken = Median ueber die Repetitions, Fehlerbalken = Min/Max.
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
STALL_EVENTS = [  # (JSON-Spalte, Kurzname fuer die x-Achse, Langname)
    ("DISPATCH_RESOURCE_STALL_CYCLES_0:ALU_TOKEN_STALL", "ALU-Token\n(ALU-Scheduler)", "ALU-Token-Stall"),
    ("DISPATCH_RESOURCE_STALL_CYCLES_1:STORE_QUEUE_RSRC_STALL", "Store-Queue", "Store-Queue-Stall"),
    ("DISPATCH_RESOURCE_STALL_CYCLES_1:TAKEN_BRANCH_BUFFER_RSRC_STALL", "Taken-Branch-\nBuffer",
     "Taken-Branch-Buffer-Stall"),
    ("DISPATCH_RESOURCE_STALL_CYCLES_1:INT_PHY_REG_FILE_RSRC_STALL", "Int-Phys.-\nRegisterfile",
     "Integer-Physical-Register-File-Stall"),
    ("DISPATCH_RESOURCE_STALL_CYCLES_1:LOAD_QUEUE_RSRC_STALL", "Load-Queue", "Load-Queue-Stall"),
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
    print("Benchmarks:", ", ".join(sorted({b for b, _ in keys})),
          "| log2N:", ", ".join(str(r) for r in sorted({r for _, r in keys})))
    return keys, runs, medians


def getc(e, col):
    """Counter-Wert; exakter Name, sonst Treffer ueber den Teil nach dem ':' (Gross/Klein egal)."""
    if col in e:
        return e[col]
    tail = col.split(":")[-1].lower()
    for k, v in e.items():
        if k.lower().split(":")[-1] == tail and isinstance(v, (int, float)):
            return v
    raise KeyError(col)


def time_ms(e):
    return e["real_time"] * UNIT_TO_MS[e["time_unit"]]


def ipc(e):
    return getc(e, INSTR) / getc(e, CYCLES)


def stall_pct(col):
    return lambda e: getc(e, col) / getc(e, CYCLES) * 100.0


def stall_abs(col):
    return lambda e: getc(e, col)


def stat(key, fn, runs, medians):
    """(Median, Min, Max, Anzahl) einer Metrik ueber die Repetitions, None wenn nicht vorhanden."""
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


# ---------------------------------------------------------------- Zeit / IPC
def collect(fn, keys, runs, medians):
    """x = Ringdimension, Balken = Benchmark; None wenn kein Messwert."""
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
    return ([f"N = 2^{r}" for r in rings], benches, med.T, lo.T, hi.T, min(reps))


def draw(ax, data, ylabel, title, log=False, fontsize=8, wrap=80):
    """Gruppierte Balken auf ax, gibt (Handles, Labels) zurueck."""
    cats, sers, med, lo, hi, _ = data
    ncat, nser = med.shape
    x = np.arange(ncat)
    w = 0.8 / nser
    cols = plt.cm.viridis(np.linspace(0.15, 0.85, nser))
    for k in range(nser):
        xs = x + (k - (nser - 1) / 2) * w
        h = med[:, k]
        ok = np.isfinite(h)
        err = np.vstack([np.nan_to_num(h - lo[:, k]), np.nan_to_num(hi[:, k] - h)])
        ax.bar(xs[ok], h[ok], w, yerr=err[:, ok], capsize=2, color=cols[k], label=sers[k])
        for xi, val in zip(xs[ok], h[ok]):
            ax.text(xi, val, f"{val:.3g}", ha="center", va="bottom", fontsize=fontsize)
    ax.set_xticks(x)
    ax.set_xticklabels(cats)
    ax.set_ylabel(ylabel)
    ax.set_title("\n".join(textwrap.wrap(title, wrap)), fontsize=fontsize + 3)
    if log:
        ax.set_yscale("log")
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)
    ax.margins(y=0.15)
    return ax.get_legend_handles_labels()


# --------------------------------------------------------------------- Stalls
def collect_stalls(keys, runs, medians, use_pct):
    """Matrizen [Benchmark, Ring, Event]; Events nach Groesse sortiert (groesster zuerst)."""
    benches = sorted({b for b, _ in keys})
    rings = sorted({r for _, r in keys})
    shape = (len(benches), len(rings), len(STALL_EVENTS))
    med, lo, hi = (np.full(shape, np.nan) for _ in range(3))
    reps = []
    for i, b in enumerate(benches):
        for j, r in enumerate(rings):
            for k, (col, _, _) in enumerate(STALL_EVENTS):
                s = stat((b, r), stall_pct(col) if use_pct else stall_abs(col), runs, medians)
                if s:
                    med[i, j, k], lo[i, j, k], hi[i, j, k] = s[:3]
                    reps.append(s[3])
    present = [k for k in range(len(STALL_EVENTS)) if np.isfinite(med[:, :, k]).any()]
    if not present:
        return None
    present.sort(key=lambda k: -np.nanmax(med[:, :, k]))
    return dict(benches=benches, rings=rings, order=present, med=med, lo=lo, hi=hi,
                reps=min(reps), use_pct=use_pct)


def stall_row(fig, gs, S, linear=False, fontsize=7):
    """Je Ringdimension ein Panel (gemeinsame y-Achse); gibt (Achsen, Handles, Labels) zurueck."""
    benches, rings, order = S["benches"], S["rings"], S["order"]
    nb = len(benches)
    cols = plt.cm.viridis(np.linspace(0.15, 0.85, nb))
    pos = S["med"][np.isfinite(S["med"]) & (S["med"] > 0)]
    ymax = np.nanmax(S["hi"])
    if linear:
        ylim = (0, ymax * 1.15)
    else:
        ylim = (10 ** np.floor(np.log10(pos.min())) * 0.5, ymax * 6)
    ylabel = "Anteil an allen Zyklen [%]" if S["use_pct"] else "Stall-Zyklen pro Iteration"
    if not linear:
        ylabel += " (log)"
    labels = [STALL_EVENTS[k][1] for k in order]
    x = np.arange(len(order))
    w = 0.8 / nb
    axs = []
    for j, r in enumerate(rings):
        ax = fig.add_subplot(gs[0, j], sharey=axs[0] if axs else None)
        axs.append(ax)
        for i in range(nb):
            xs = x + (i - (nb - 1) / 2) * w
            h = S["med"][i, j, order]
            ok = np.isfinite(h)
            err = np.vstack([np.nan_to_num(h - S["lo"][i, j, order]),
                             np.nan_to_num(S["hi"][i, j, order] - h)])
            ax.bar(xs[ok], h[ok], w, yerr=err[:, ok], capsize=2, color=cols[i], label=benches[i])
            for xi, val in zip(xs[ok], h[ok]):
                txt = f"{val:.3g}" + ("%" if S["use_pct"] else "")
                ax.text(xi, val * (1 if linear else 1.15), txt, ha="center", va="bottom",
                        fontsize=fontsize, rotation=90)
        ax.set_xticks(x)
        ax.set_xticklabels(labels, fontsize=fontsize + 1)
        ax.set_title(f"Stall-Counter bei N = 2^{r}", fontsize=fontsize + 3)
        if not linear:
            ax.set_yscale("log")
        ax.set_ylim(*ylim)
        ax.grid(axis="y", alpha=0.3, which="major")
        ax.set_axisbelow(True)
        if j == 0:
            ax.set_ylabel(ylabel)
        else:
            ax.tick_params(labelleft=False)
    h, l = axs[0].get_legend_handles_labels()
    return axs, h, l


def footnote(reps, use_pct):
    txt = f"Balken = Median ueber {reps} Repetitions, Fehlerbalken = Min/Max."
    txt += (" Stall-Counter ueberlappen sich (nicht aufsummieren); gleiche y-Achse in allen Stall-Panels."
            if use_pct is not None else "")
    return txt


def make_overview(top, S, args):
    n_top = max(1, len(top))
    nr = len(S["rings"])
    fig = plt.figure(figsize=(max(14, 6.2 * max(nr, n_top)), 11))
    gs = fig.add_gridspec(2, 1, height_ratios=[1, 1.25], hspace=0.38)
    gt = gs[0].subgridspec(1, n_top, wspace=0.25)
    gb = gs[1].subgridspec(1, nr, wspace=0.06)
    handles = labels = None
    for j, (d, ylabel, title, log) in enumerate(top):
        h, l = draw(fig.add_subplot(gt[0, j]), d, ylabel, title, log, fontsize=8)
        handles, labels = (h, l) if handles is None else (handles, labels)
    _, h2, l2 = stall_row(fig, gb, S, args.linear)
    handles, labels = (h2, l2) if handles is None else (handles, labels)
    fig.legend(handles, labels, title="Benchmark", loc="lower center", ncol=len(labels),
               fontsize=10, title_fontsize=11, bbox_to_anchor=(0.5, 0.012))
    reps = min([S["reps"]] + [d[5] for d, *_ in top])
    fig.text(0.01, 0.004, footnote(reps, S["use_pct"]), fontsize=8)
    fig.subplots_adjust(left=0.06, right=0.99, top=0.95, bottom=0.14)
    out = os.path.join(args.output, args.prefix + "plot_overview.png")
    fig.savefig(out, dpi=160)
    plt.close(fig)
    print("geschrieben:", out)


def make_stall_plot(S, args):
    nr = len(S["rings"])
    fig = plt.figure(figsize=(max(9, 5.6 * nr), 6.2))
    gs = fig.add_gridspec(1, nr, wspace=0.06)
    axs, h, l = stall_row(fig, gs, S, args.linear, fontsize=8)
    fig.legend(h, l, title="Benchmark", loc="lower center", ncol=len(l), bbox_to_anchor=(0.5, 0.0))
    fig.text(0.01, 0.004, footnote(S["reps"], S["use_pct"]), fontsize=8)
    fig.subplots_adjust(left=0.08, right=0.99, top=0.92, bottom=0.2)
    out = os.path.join(args.output, args.prefix + "plot_stalls.png")
    fig.savefig(out, dpi=160)
    plt.close(fig)
    print("geschrieben:", out)


def make_single(data, ylabel, title, fname, reps, args, log=False):
    fig, ax = plt.subplots(figsize=(8, 5.5))
    h, l = draw(ax, data, ylabel, title, log)
    ax.legend(h, l, title="Benchmark", loc="upper left", bbox_to_anchor=(1.01, 1.0))
    fig.text(0.01, 0.005, f"Balken = Median ueber {reps} Repetitions, Fehlerbalken = Min/Max.", fontsize=8)
    fig.tight_layout(rect=(0, 0.03, 1, 1))
    out = os.path.join(args.output, args.prefix + fname)
    fig.savefig(out, dpi=160)
    plt.close(fig)
    print("geschrieben:", out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("folder")
    ap.add_argument("-o", "--output", default=".")
    ap.add_argument("--prefix", default="")
    ap.add_argument("--log-time", action="store_true", help="logarithmische Zeitachse")
    ap.add_argument("--linear", action="store_true", help="lineare statt logarithmische Stall-Achse")
    ap.add_argument("--abs", action="store_true",
                    help="absolute Stall-Zyklen pro Iteration statt Anteil an CYCLES")
    ap.add_argument("--rings", type=int, nargs="+", metavar="LOG2N",
                    help="log2 N je Config-Index (nur noetig, wenn 'ring-dim' fehlt), z. B. --rings 14 15 16")
    ap.add_argument("--overview-only", action="store_true", help="nur plot_overview.png")
    args = ap.parse_args()
    global RINGS
    RINGS = args.rings
    os.makedirs(args.output, exist_ok=True)

    keys, runs, medians = load(args.folder)
    entries = [e for es in runs.values() for e in es] + list(medians.values())
    use_s = max(time_ms(e) for e in entries) > 1000
    time_fn = (lambda e: time_ms(e) / 1000.0) if use_s else time_ms
    unit = "s" if use_s else "ms"

    have_cycles = any(CYCLES in e for e in entries)
    use_pct = have_cycles and not args.abs
    if not have_cycles:
        print(f"Hinweis: Counter {CYCLES} fehlt -> absolute Stall-Zyklen pro Iteration "
              "(fuer Prozentwerte CYCLES in --benchmark_perf_counters aufnehmen).")

    top_defs = [(time_fn, f"Zeit pro Iteration [{unit}]", "Laufzeit", "plot_time.png", args.log_time),
                (ipc, "Instruktionen pro Zyklus", "IPC", "plot_ipc.png", False)]
    top, singles = [], []
    for fn, ylabel, title, fname, log in top_defs:
        d = collect(fn, keys, runs, medians)
        if d is None:
            print(f"uebersprungen: {title} (Counter nicht in den Dateien)")
            continue
        top.append((d, ylabel, title, log))
        singles.append((d, ylabel, title, fname, log))

    S = collect_stalls(keys, runs, medians, use_pct)
    if S is None:
        raise SystemExit("Keine Stall-Counter in den Dateien gefunden (STALL_EVENTS pruefen).")

    make_overview(top, S, args)
    if not args.overview_only:
        for d, ylabel, title, fname, log in singles:
            make_single(d, ylabel, title, fname, d[5], args, log)
        make_stall_plot(S, args)

    with open(os.path.join(args.output, args.prefix + "summary.csv"), "w", newline="") as f:
        wr = csv.writer(f)
        wr.writerow(["benchmark", "log2N", "metric", "median", "min", "max", "repetitions"])
        metrics = [(f"time_{unit}", time_fn), ("ipc", ipc)]
        for col, _, _ in STALL_EVENTS:
            metrics.append((col + ("_pct_cycles" if use_pct else "_per_iter"),
                            stall_pct(col) if use_pct else stall_abs(col)))
        for (b, r) in keys:
            for name, fn in metrics:
                s = stat((b, r), fn, runs, medians)
                if s:
                    wr.writerow([b, r, name, s[0], s[1], s[2], s[3]])
    print("geschrieben:", os.path.join(args.output, args.prefix + "summary.csv"))


if __name__ == "__main__":
    main()
