#!/usr/bin/env python3
"""Draw a Gantt diagram of CPU scheduling PARSED FROM A REAL RUN LOG.

Every bar comes from 'Dispatched/Put/finished' events in the captured
output (output_run/*.output). Nothing is synthesized.

Usage: make_gantt.py <logfile> <out.png> <title>
"""
import re
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

# dataviz validated categorical palette (light mode), fixed slot order
PALETTE = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100",
           "#e87ba4", "#008300", "#8b7ce8", "#8c8c87"]
SURFACE = "#ffffff"
TEXT = "#30302e"
TEXT2 = "#6b6b66"

def parse(path):
    slot = -1
    running = {}            # cpu -> (pid, start_slot)
    bars = []               # (cpu, pid, start, end)
    cpus, pids = set(), []
    with open(path, errors="replace") as f:
        for line in f:
            m = re.match(r"Time slot\s+(\d+)", line)
            if m:
                slot = int(m.group(1))
                continue
            m = re.search(r"CPU (\d+): Dispatched process\s+(\d+)", line)
            if m:
                cpu, pid = int(m.group(1)), int(m.group(2))
                if cpu in running:  # back-to-back dispatch
                    opid, s = running[cpu]
                    bars.append((cpu, opid, s, slot))
                running[cpu] = (pid, slot)
                cpus.add(cpu)
                if pid not in pids:
                    pids.append(pid)
                continue
            m = re.search(r"CPU (\d+): Put process\s+(\d+) to run queue", line)
            if m:
                cpu, pid = int(m.group(1)), int(m.group(2))
                if cpu in running and running[cpu][0] == pid:
                    bars.append((cpu, pid, running[cpu][1], slot))
                    del running[cpu]
                continue
            m = re.search(r"CPU (\d+): Processed\s+(\d+) has finished", line)
            if m:
                cpu, pid = int(m.group(1)), int(m.group(2))
                if cpu in running and running[cpu][0] == pid:
                    end = slot + 1 if slot >= running[cpu][1] else running[cpu][1] + 1
                    bars.append((cpu, pid, running[cpu][1], end))
                    del running[cpu]
                continue
    for cpu, (pid, s) in running.items():
        bars.append((cpu, pid, s, slot + 1))
    return bars, sorted(cpus), pids

def main():
    log, out, title = sys.argv[1], sys.argv[2], sys.argv[3]
    bars, cpus, pids = parse(log)
    maxt = max(e for *_x, e in [(b[0], b[1], b[2], b[3]) for b in bars]) if bars else 1
    maxt = max(b[3] for b in bars)

    rows = {cpu: i for i, cpu in enumerate(cpus)}
    color = {pid: PALETTE[i % len(PALETTE)] for i, pid in enumerate(sorted(pids))}

    fig_h = 1.1 + 0.85 * len(cpus)
    fig, ax = plt.subplots(figsize=(11, fig_h), dpi=150)
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)

    for cpu, pid, s, e in bars:
        y = rows[cpu]
        ax.barh(y, e - s, left=s, height=0.52, color=color[pid],
                edgecolor=SURFACE, linewidth=1.2, zorder=3)
        if e - s >= 1:
            ax.text((s + e) / 2, y, f"P{pid}", ha="center", va="center",
                    color="#ffffff", fontsize=9, fontweight="bold", zorder=4)

    ax.set_yticks([rows[c] for c in cpus])
    ax.set_yticklabels([f"CPU {c}" for c in cpus], fontsize=10, color=TEXT)
    ax.set_xlabel("Time slot", fontsize=10, color=TEXT2)
    ax.set_xlim(0, maxt + 0.5)
    ax.set_ylim(-0.6, len(cpus) - 0.4)
    ax.invert_yaxis()
    ax.xaxis.set_major_locator(plt.MultipleLocator(2))
    ax.grid(axis="x", color="#e8e8e4", linewidth=0.8, zorder=0)
    for sp in ["top", "right", "left"]:
        ax.spines[sp].set_visible(False)
    ax.spines["bottom"].set_color("#d4d4cf")
    ax.tick_params(colors=TEXT2, labelsize=9)

    handles = [Patch(facecolor=color[p], label=f"P{p} (PID {p})")
               for p in sorted(pids)]
    ax.legend(handles=handles, loc="upper center",
              bbox_to_anchor=(0.5, -0.32 if len(cpus) > 1 else -0.52),
              ncol=min(len(pids), 8), frameon=False, fontsize=9)
    ax.set_title(title, fontsize=12, color=TEXT, pad=12, loc="left",
                 fontweight="bold")

    plt.tight_layout()
    plt.savefig(out, bbox_inches="tight", facecolor=SURFACE)
    print(f"saved {out}: {len(bars)} bars, cpus={cpus}, pids={sorted(pids)}")

if __name__ == "__main__":
    main()
