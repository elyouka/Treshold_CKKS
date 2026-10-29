import json
import re
import numpy as np
import matplotlib.pyplot as plt

# ------------------------------------------------------------
# 1. Load the benchmark JSON
# ------------------------------------------------------------
with open("NTT.json", "r") as f:
    data = json.load(f)

rows = []

# ------------------------------------------------------------
# 2. Extract mean aggregates for each ring dimension
# ------------------------------------------------------------
for bench in data["benchmarks"]:
    # We only want the aggregate "mean" entries
    if bench.get("run_type") != "aggregate":
        continue
    if bench.get("aggregate_name") != "mean":
        continue

    name = bench.get("run_name", bench.get("name", ""))
    m = re.search(r"ringdm:(\d+)", name)
    if not m:
        continue

    ringdim = int(m.group(1))

    cpu_time = bench.get("cpu_time", np.nan)
    real_time = bench.get("real_time", np.nan)

    instructions = bench.get("INSTRUCTIONS", np.nan)
    cycles       = bench.get("CYCLES", np.nan)
    l1_misses    = bench.get("L1-DCACHE-LOAD-MISSES", np.nan)
    dtlb_misses  = bench.get("DTLB-LOAD-MISSES", np.nan)
    l2_req       = bench.get("CORE_TO_L2_CACHEABLE_REQUEST_ACCESS_STATUS:LS_RD_BLK_C", np.nan)

    rows.append({
        "ringdim": ringdim,
        "cpu_time_ms": cpu_time,
        "real_time_ms": real_time,
        "instructions": instructions,
        "cycles": cycles,
        "l1_misses": l1_misses,
        "dtlb_misses": dtlb_misses,
        "l2_req": l2_req,
        "ipc": instructions / cycles if cycles else np.nan,
        "l1_misses_per_1000": (l1_misses / instructions * 1000.0) if instructions else np.nan,
        "dtlb_misses_per_1000": (dtlb_misses / instructions * 1000.0) if instructions else np.nan,
        "l2_req_per_1000": (l2_req / instructions * 1000.0) if instructions else np.nan,
    })

# Sort by ring dimension
rows.sort(key=lambda r: r["ringdim"])

# Convert to numpy arrays for plotting
ringdim = np.array([r["ringdim"] for r in rows])
cpu_time = np.array([r["cpu_time_ms"] for r in rows])
ipc = np.array([r["ipc"] for r in rows])
l1_miss_per_1000 = np.array([r["l1_misses_per_1000"] for r in rows])
dtlb_miss_per_1000 = np.array([r["dtlb_misses_per_1000"] for r in rows])
l2_req_per_1000 = np.array([r["l2_req_per_1000"] for r in rows])

# ------------------------------------------------------------
# 3. Plot
# ------------------------------------------------------------
fig, axes = plt.subplots(1, 3, figsize=(18, 5))

# ---- Plot 1: Time ----
axes[0].plot(ringdim, cpu_time, "o-", color="tab:blue", label="CPU time")
axes[0].set_xscale("log", base=2)
axes[0].set_xlabel("Ring dimension")
axes[0].set_ylabel("CPU time (ms)")
axes[0].set_title("NTT time vs ring dimension")
axes[0].grid(True, which="both", ls="--", alpha=0.6)

# ---- Plot 2: IPC ----
axes[1].plot(ringdim, ipc, "o-", color="tab:orange")
axes[1].set_xscale("log", base=2)
axes[1].set_xlabel("Ring dimension")
axes[1].set_ylabel("IPC (instructions / cycle)")
axes[1].set_title("IPC vs ring dimension")
axes[1].grid(True, which="both", ls="--", alpha=0.6)

# ---- Plot 3: Cache / TLB misses per 1000 instructions ----
axes[2].plot(ringdim, l1_miss_per_1000, "o-", color="tab:red", label="L1D load misses")
axes[2].plot(ringdim, dtlb_miss_per_1000, "s-", color="tab:green", label="DTLB load misses")
axes[2].plot(ringdim, l2_req_per_1000, "^-", color="tab:purple", label="L2 cacheable requests")
axes[2].set_xscale("log", base=2)
axes[2].set_yscale("log")
axes[2].set_xlabel("Ring dimension")
axes[2].set_ylabel("Events per 1000 instructions")
axes[2].set_title("Cache / TLB events per 1000 instructions")
axes[2].grid(True, which="both", ls="--", alpha=0.6)
axes[2].legend()

plt.tight_layout()
plt.savefig("ntt_benchmarks.png", dpi=200)
plt.show()