"""Summarize a capture-sweep-profile CSV and draw its measured timing plot."""
import csv
import json
import math
import sys
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

csv_path = Path(sys.argv[1])
prefix = csv_path.with_suffix("")
with csv_path.open(encoding="utf-8-sig", newline="") as source:
    rows = list(csv.DictReader(source))
summary = json.loads(Path(str(prefix) + "-summary.json").read_text(encoding="utf-8-sig"))
profile = summary["profile"]
multicore = profile.get("multicore", False)
valid = [r for r in rows if r["valid"].lower() == "true"]
failed = [r for r in rows if r["valid"].lower() != "true"]

def percentile(values, fraction):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * fraction) - 1)] if values else float("nan")

phases = ["tune", "path", "settle", "lock_wait", "adc", "processing"]
totals = {phase: sum(float(r[phase + "_us"]) for r in rows) / 1e6 for phase in phases}
valid_lock_ms = [float(r["lock_acquire_us"]) / 1000 for r in valid]
lines = [
    "# Measured sweep timing profile", "",
    f"Attempted: {profile['attempted']}; valid: {profile['valid']}; timed out: {profile['failed']}.",
    f"Sweep wall time: {profile['wall_us']/1e6:.6f} s.",
    f"Startup: {profile['startup_us']/1e6:.6f} s; shutdown: {profile['shutdown_us']/1e6:.6f} s; point USB transmit: {profile['transmit_us']/1e6:.6f} s.",
    "", "| Exclusive phase | Total (s) | Mean per attempted point (ms) |", "|---|---:|---:|",
]
for phase in phases:
    lines.append(f"| {phase} | {totals[phase]:.6f} | {totals[phase]*1000/len(rows):.6f} |")
point_total = sum(float(r["total_us"]) for r in rows) / 1e6
accounted = point_total + (profile["startup_us"] + profile["shutdown_us"] + profile["transmit_us"]) / 1e6
if multicore:
    lines += ["", "Dual-core pipeline: point hardware, processing and USB work overlap. Phase totals are accumulated work and do not sum to wall time.",
              f"Producer acquired {profile.get('producer_acquired', '?')} points (may include discarded read-ahead on Abort); hardware work {profile.get('producer_hardware_us',0)/1e6:.6f} s; result queue wait {profile.get('producer_queue_wait_us',0)/1e6:.6f} s; consumer receive/service wait {profile.get('consumer_wait_us',0)/1e6:.6f} s."]
else:
    lines += ["", f"Point measurement total: {point_total:.6f} s; remaining protocol/bookkeeping: {profile['wall_us']/1e6-accounted:.6f} s."]
if valid_lock_ms:
    lines += ["", f"Successful lock acquisition: mean {sum(valid_lock_ms)/len(valid_lock_ms):.3f} ms; median {percentile(valid_lock_ms,.5):.3f} ms; p95 {percentile(valid_lock_ms,.95):.3f} ms; maximum {max(valid_lock_ms):.3f} ms."]
lines += ["", "Lock acquisition includes initial routing/settling and is not added to the phase totals. Path and settle are software command/wait times, not measured analog settling. Timed-out lock durations are censored by the watchdog, not successful acquisition times."]
if failed:
    lines += ["", "Timed-out frequencies (Hz): " + ", ".join(r["frequency_hz"] for r in failed) + "."]
report = "\n".join(lines) + "\n"
Path(str(prefix) + "-report.md").write_text(report, encoding="utf-8")
print(report)

fig, axes = plt.subplots(2, 1, figsize=(10, 8), constrained_layout=True)
for group, label, marker, color in [(valid,"Locked",".","#087e8b"),(failed,"Timed out","x","#c23b22")]:
    if group:
        axes[0].scatter([float(r["frequency_hz"])/1e9 for r in group],
                        [float(r["lock_acquire_us"])/1000 for r in group],
                        label=label, marker=marker, color=color)
axes[0].axhline(20, color="gray", linestyle="--", linewidth=1, label="Normal 20 ms deadline")
axes[0].set(xlabel="Frequency (GHz)", ylabel="Lock acquisition (ms)", yscale="log", title="Measured lock acquisition across one sweep")
axes[0].grid(True, which="both", alpha=.2)
axes[0].legend()
labels = phases + ["startup", "USB transmit", "shutdown"] + ([] if multicore else ["other"])
values = list(totals.values()) + [profile["startup_us"]/1e6,profile["transmit_us"]/1e6,profile["shutdown_us"]/1e6] + ([] if multicore else [max(0,profile["wall_us"]/1e6-accounted)])
axes[1].barh(labels, values, color="#087e8b")
axes[1].invert_yaxis()
axes[1].set(xlabel="Total elapsed time (s)", title="Accumulated work (cores overlap)" if multicore else "Exclusive timing categories; lock overlap counted once")
for index, value in enumerate(values):
    axes[1].text(value,index,f" {value:.4f}",va="center",fontsize=9)
fig.savefig(str(prefix) + "-timings.png", dpi=180)
fig.savefig(str(prefix) + "-timings.pdf")
plt.close(fig)
