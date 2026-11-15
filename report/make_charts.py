#!/usr/bin/env python3
"""Charts for every numeric section of BAO_CAO.md, PARSED FROM REAL RUN LOGS.

Only files in output_run/ are read (run_tests_console.output + <test>.output).
Nothing is typed in by hand except the theoretical full-tree size, which is
computed (not measured) and labelled as such.

Usage: make_charts.py <output_run_dir> <images_dir>
  -> 16_chart_tests.png       (section 1: processes finished per test)
  -> 17_chart_mm64_stats.png  (section 9: page-table storage + PTE walks)
  -> 18_chart_demand.png      (section 9 / 2.2.2: demand vs full static tree)

Style follows the dataviz skill: one hue (categorical slot 1) per single-series
chart, emphasis gray for the hypothetical bar, bars <= 24px with a 4px rounded
data-end square at the baseline, hairline solid grid, text in ink tokens only,
selective tip labels, no legend for a single series, no dual axis (the two
MM64 measures are two panels).
"""
import math
import os
import re
import sys
from datetime import datetime

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.path import Path
from matplotlib.lines import Line2D
from matplotlib.patches import PathPatch, Patch, Rectangle
from matplotlib.transforms import IdentityTransform
from matplotlib.ticker import FuncFormatter, LogLocator, NullLocator

# ---- dataviz reference palette (light) --------------------------------------
SURFACE = "#fcfcfb"
INK = "#0b0b0b"
INK2 = "#52514e"
MUTED = "#898781"
GRID = "#e1e0d9"
AXIS = "#c3c2b7"
SERIES1 = "#2a78d6"        # categorical slot 1 (blue)
PALETTE = ["#2a78d6", "#eb6834", "#1baf7a", "#eda100",     # categorical, fixed order
           "#e87ba4", "#008300", "#6250d6", "#e34948"]
INK_ON = {"#eda100": "#0b0b0b"}                            # near-black label on yellow
DEEMPH = MUTED             # emphasis form: the "rest" / hypothetical

DPI = 150
PX = DPI / 96.0            # one CSS px in device px
BAR_PX = 22 * PX           # <= 24px bar thickness
ROW_PX = 40 * PX           # row pitch
R_PX = 4 * PX              # rounded data-end radius

plt.rcParams.update({
    "font.family": ["Segoe UI", "DejaVu Sans"],
    "font.size": 10,
    "axes.edgecolor": AXIS,
    "xtick.color": MUTED,
    "ytick.color": INK2,
    "xtick.labelcolor": INK2,
    "ytick.labelcolor": INK,
})


# ---- parsing ----------------------------------------------------------------
def parse_console(run_dir):
    """[(test, exit_code, finished)] in run order + start/finish stamps."""
    rows, cur, stamps = [], None, {}
    with open(os.path.join(run_dir, "run_tests_console.output"), encoding="utf-8") as f:
        for line in f:
            m = re.match(r"# run_tests\.sh (started|finished):\s+(.*)", line)
            if m:
                raw = m.group(2).strip()
                try:   # logged in UTC by WSL -> show local time
                    raw = datetime.strptime(raw, "%Y-%m-%d %H:%M:%S %z") \
                        .astimezone().strftime("%Y-%m-%d %H:%M:%S %z")
                except ValueError:
                    pass
                stamps[m.group(1)] = raw
            m = re.match(r"--- Running (\S+)", line)
            if m:
                cur = m.group(1)
            m = re.search(r"exit=(\d+) processes_finished=(\d+)", line)
            if m and cur:
                rows.append((cur, int(m.group(1)), int(m.group(2))))
                cur = None
    return rows, stamps


def parse_log(run_dir, test):
    with open(os.path.join(run_dir, test + ".output"), encoding="utf-8",
              errors="replace") as f:
        text = f.read()
    loaded = len(re.findall(r"Loaded a process", text))
    finished = len(re.findall(r"has finished", text))
    m = re.search(r"MM64 stats: directory tables allocated=(\d+) "
                  r"\(storage=(\d+) KB\), pte walks=(\d+)", text)
    tables, kb, walks = (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else (0, 0, 0)
    return dict(loaded=loaded, finished=finished, tables=tables, kb=kb, walks=walks)


# ---- marks ------------------------------------------------------------------
def rounded_hbar(fig, ax, y, value, color, x0=0.0):
    """Horizontal bar, square at the baseline, 4px rounded data-end.
    Drawn in device pixels so the radius is exact on linear and log axes."""
    (bx, by), (ex, _) = ax.transData.transform([(x0, y), (value, y)])
    h = BAR_PX / 2
    y0, y1 = by - h, by + h
    r = min(R_PX, max(ex - bx, 0) / 2, h)
    verts = [(bx, y0), (ex - r, y0), (ex, y0), (ex, y0 + r), (ex, y1 - r),
             (ex, y1), (ex - r, y1), (bx, y1), (bx, y0)]
    codes = [Path.MOVETO, Path.LINETO, Path.CURVE3, Path.CURVE3, Path.LINETO,
             Path.CURVE3, Path.CURVE3, Path.LINETO, Path.CLOSEPOLY]
    fig.add_artist(PathPatch(Path(verts, codes), facecolor=color, edgecolor="none",
                             transform=IdentityTransform(), zorder=3))


def style_axes(ax, labels):
    ax.set_facecolor(SURFACE)
    n = len(labels)
    ax.set_ylim(n - 0.5, -0.5)
    ax.set_yticks(range(n))
    ax.set_yticklabels(labels)
    ax.tick_params(axis="y", length=0, pad=8)
    ax.tick_params(axis="x", length=0, pad=6, labelsize=9)
    ax.grid(axis="x", color=GRID, linewidth=1, linestyle="-", zorder=0)
    ax.set_axisbelow(True)
    for sp in ("top", "right", "bottom"):
        ax.spines[sp].set_visible(False)
    ax.spines["left"].set_color(AXIS)
    ax.spines["left"].set_linewidth(1)


def tip_label(ax, y, x, text, log=False):
    ax.annotate(text, (x, y), xytext=(6, 0), textcoords="offset points",
                ha="left", va="center", fontsize=9, color=INK2, zorder=4)


SUP = str.maketrans("0123456789-", "⁰¹²³⁴⁵⁶⁷⁸⁹⁻")


def sci(v):
    """2.75e11 -> '2.75×10¹¹' (readable scientific notation)."""
    m, e = f"{v:.2e}".split("e")
    return f"{m.rstrip('0').rstrip('.')}×10{str(int(e)).translate(SUP)}"


def new_fig(n_rows, width_in, top_px=86, bottom_px=46, row_px=None):
    h_px = (top_px + bottom_px) * PX + n_rows * (row_px or ROW_PX)
    fig = plt.figure(figsize=(width_in, h_px / DPI), dpi=DPI)
    fig.patch.set_facecolor(SURFACE)
    return fig, h_px


def header(fig, title, subtitle, h_px):
    fig.text(16 * PX / (fig.get_figwidth() * DPI), 1 - 14 * PX / h_px, title,
             ha="left", va="top", fontsize=12.5, fontweight="bold", color=INK)
    fig.text(16 * PX / (fig.get_figwidth() * DPI), 1 - 38 * PX / h_px, subtitle,
             ha="left", va="top", fontsize=9, color=INK2)


# ---- chart 16: tests ----------------------------------------------------------
def chart_tests(run_dir, out, rows, stamps):
    data = []
    for test, rc, fin in rows:
        lg = parse_log(run_dir, test)
        data.append((test, rc, fin, lg["loaded"]))
    n = len(data)
    passed = sum(1 for _, rc, fin, ld in data if rc == 0 and fin == ld)
    fig, h_px = new_fig(n, 9.2)
    top, bottom = 86 * PX, 46 * PX
    ax = fig.add_axes([0.26, bottom / h_px, 0.62, 1 - (top + bottom) / h_px])
    style_axes(ax, [d[0] for d in data])
    xmax = max(d[3] for d in data)
    ax.set_xlim(0, xmax * 1.18)
    ax.xaxis.set_major_locator(plt.MultipleLocator(2))
    ax.set_xlabel("Số process hoàn tất (has finished)", fontsize=9, color=INK2,
                  labelpad=6)
    fig.canvas.draw()
    for i, (test, rc, fin, ld) in enumerate(data):
        rounded_hbar(fig, ax, i, fin, SERIES1)
        tip_label(ax, i, fin, f"{fin}/{ld}  ·  exit={rc}")
    header(fig, f"{passed}/{n} test pass — mọi process đã nạp đều kết thúc",
           f"Nguồn: output_run/run_tests_console.output + output_run/<test>.output"
           f"  ·  chạy {stamps.get('started', '?')}", h_px)
    fig.savefig(out, dpi=DPI, facecolor=SURFACE)
    plt.close(fig)
    print(f"saved {out}: {n} tests, {passed} pass")
    return data


# ---- chart 17: MM64 stats (two panels, never a dual axis) ---------------------
def chart_mm64(run_dir, out, rows, stamps):
    data = [(t, parse_log(run_dir, t)) for t, _, _ in rows]
    n = len(data)
    fig, h_px = new_fig(n, 11.5)
    top, bottom = 104 * PX, 46 * PX
    hgt = 1 - (top + bottom) / h_px
    axL = fig.add_axes([0.22, bottom / h_px, 0.36, hgt])
    axR = fig.add_axes([0.66, bottom / h_px, 0.27, hgt])
    labels = [t for t, _ in data]
    style_axes(axL, labels)
    style_axes(axR, labels)
    axR.set_yticklabels([])
    kbmax = max(d["kb"] for _, d in data)
    wmax = max(d["walks"] for _, d in data)
    axL.set_xlim(0, kbmax * 1.42)
    axR.set_xlim(0, wmax * 1.22)
    axL.xaxis.set_major_locator(plt.MultipleLocator(20))
    axR.xaxis.set_major_locator(plt.MultipleLocator(10))
    axL.set_xlabel("Dung lượng bảng trang (KB)", fontsize=9, color=INK2, labelpad=6)
    axR.set_xlabel("Số lần walk PTE", fontsize=9, color=INK2, labelpad=6)
    for ax, ttl in ((axL, "Bảng thư mục đã cấp"), (axR, "Truy cập bảng trang")):
        ax.set_title(ttl, loc="left", fontsize=10, color=INK, fontweight="bold", pad=8)
    fig.canvas.draw()
    for i, (_, d) in enumerate(data):
        rounded_hbar(fig, axL, i, d["kb"], SERIES1)
        tip_label(axL, i, d["kb"], f"{d['kb']} KB · {d['tables']} bảng")
        rounded_hbar(fig, axR, i, d["walks"], SERIES1)
        tip_label(axR, i, d["walks"], f"{d['walks']}")
    header(fig, "MM64 stats — chỉ tốn vài chục KB bảng trang cho cả hệ",
           "Nguồn: dòng \"MM64 stats\" cuối mỗi output_run/<test>.output"
           f"  ·  chạy {stamps.get('started', '?')}", h_px)
    fig.savefig(out, dpi=DPI, facecolor=SURFACE)
    plt.close(fig)
    print(f"saved {out}: {n} tests")
    return data


# ---- chart 18: demand vs full static tree (log scale) -------------------------
def chart_demand(out, mm, stamps):
    tables_full = sum(512 ** k for k in range(5))       # 1+512+...+512^4
    full_kb = tables_full * 4                             # 4 KB / table
    test, best = max(mm, key=lambda td: td[1]["kb"])
    rows = [("Dựng tĩnh cây 5 cấp\n(lý thuyết, MỖI process)", full_kb, DEEMPH,
             f"{sci(full_kb)} KB  ({sci(tables_full)} bảng)"),
            (f"Demand allocation — đo được\n(lớn nhất: {test}, cả hệ)", best["kb"],
             SERIES1, f"{best['kb']} KB  ({best['tables']} bảng)")]
    fig, h_px = new_fig(2, 9.2, top_px=86, bottom_px=50, row_px=72 * PX)
    top, bottom = 86 * PX, 50 * PX
    ax = fig.add_axes([0.30, bottom / h_px, 0.50, 1 - (top + bottom) / h_px])
    style_axes(ax, [r[0] for r in rows])
    ax.set_xscale("log")
    ax.set_xlim(1, 1e14)
    ax.set_xticks([10 ** k for k in range(0, 15, 2)])
    ax.xaxis.set_minor_locator(NullLocator())
    ax.xaxis.set_major_formatter(FuncFormatter(
        lambda v, _: "1" if v == 1 else "10" + str(round(math.log10(v))).translate(SUP)))
    ax.set_xlabel("KB (thang log)", fontsize=9, color=INK2, labelpad=6)
    fig.canvas.draw()
    for i, (_, v, c, lbl) in enumerate(rows):
        rounded_hbar(fig, ax, i, v, c, x0=1)
        tip_label(ax, i, v, lbl)
    ratio = full_kb / best["kb"]
    header(fig, f"Demand allocation nhỏ hơn cây tĩnh ~{sci(float(f'{ratio:.1e}'))} lần",
           f"Thanh xanh: đo từ output_run/{test}.output  ·  thanh xám: tính toán, không đo"
           f"  ·  chạy {stamps.get('started', '?')}", h_px)
    fig.savefig(out, dpi=DPI, facecolor=SURFACE)
    plt.close(fig)
    print(f"saved {out}: full={full_kb} KB vs {best['kb']} KB ({test})")


# ---- chart 21-23: per-process timeline (running / waiting) ------------------
WAIT = "#d4d3cc"            # recessive gray wash for "waiting in ready queue"


def chart_timeline(run_dir, test, out, title, stamps):
    """One row per process: Loaded -> finished. Running intervals come from
    make_gantt.parse (same bars/colours as the Gantt images); everything else
    between load and finish is time spent waiting in a ready queue."""
    sys.dont_write_bytecode = True          # no report/__pycache__ in the repo
    from make_gantt import parse as gantt_parse, PALETTE as GPAL
    path = os.path.join(run_dir, test + ".output")
    bars, _cpus, pids = gantt_parse(path)
    load, prio, fin, slot = {}, {}, {}, -1
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.match(r"Time slot\s+(\d+)", line)
            if m:
                slot = int(m.group(1))
                continue
            m = re.search(r"Loaded a process at .*PID: (\d+) PRIO: (\d+)", line)
            if m:
                load[int(m.group(1))], prio[int(m.group(1))] = slot, int(m.group(2))
            m = re.search(r"Processed\s+(\d+) has finished", line)
            if m:
                fin[int(m.group(1))] = slot
    order = sorted(load)
    color = {p: GPAL[i % len(GPAL)] for i, p in enumerate(sorted(pids))}
    run = {p: sorted((s, e, c) for c, q, s, e in bars if q == p) for p in order}
    xmax = max(fin.values()) + 1

    n = len(order)
    fig, h_px = new_fig(n, 11, top_px=96, bottom_px=104, row_px=36 * PX)
    top, bottom = 96 * PX, 104 * PX
    ax = fig.add_axes([0.13, bottom / h_px, 0.66, 1 - (top + bottom) / h_px])
    style_axes(ax, [f"P{p} · prio {prio[p]}" for p in order])
    ax.set_xlim(0, xmax)
    ax.xaxis.set_major_locator(plt.MultipleLocator(2 if xmax <= 40 else 5))
    ax.set_xlabel("Time slot", fontsize=9, color=INK2, labelpad=6)
    h = 0.56
    stats = []
    for i, p in enumerate(order):
        a, b = load[p], fin[p]
        ax.add_patch(Rectangle((a, i - h / 2), b - a, h, facecolor=WAIT,
                               edgecolor="none", zorder=2))
        busy = 0
        for s, e, cpu in run[p]:
            busy += e - s
            ax.add_patch(Rectangle((s, i - h / 2), e - s, h, facecolor=color[p],
                                   edgecolor=SURFACE, linewidth=1.5, zorder=3))
        ax.plot([a, a], [i - 0.38, i + 0.38], color=INK, linewidth=1.6, zorder=4)
        ax.plot([b], [i], marker="o", markersize=6, color=INK,
                markeredgecolor=SURFACE, markeredgewidth=1.5, zorder=5)
        tat = b - a
        stats.append((p, tat - busy, tat))
        ax.annotate(f"chờ {tat - busy} · turnaround {tat}", (1.0, i),
                    xycoords=("axes fraction", "data"), xytext=(10, 0),
                    textcoords="offset points", va="center", fontsize=9, color=INK2)
    handles = [Patch(facecolor=WAIT, label="đang chờ trong hàng đợi"),
               Line2D([0], [0], color=INK, linewidth=1.6, marker="|", markersize=0,
                      label="nạp (Loaded)"),
               Line2D([0], [0], color="none", marker="o", markerfacecolor=INK,
                      markersize=6, label="kết thúc (finished)")]
    handles = [Patch(facecolor=color[p], label=f"P{p} đang chạy") for p in order] + handles
    fig.legend(handles=handles, loc="lower center", ncol=min(len(handles), 5),
               frameon=False, fontsize=8.5, bbox_to_anchor=(0.46, 0.0))
    header(fig, title,
           f"Nguồn: output_run/{test}.output · chờ = turnaround − số slot chạy · "
           f"turnaround = slot kết thúc − slot nạp · chạy {stamps.get('started', '?')}", h_px)
    fig.savefig(out, dpi=DPI, facecolor=SURFACE)
    plt.close(fig)
    print(f"saved {out}: " + ", ".join(f"P{p} wait={w} tat={t}" for p, w, t in stats))


# ---- chart 24: os_4_swap - who sits in each RAM frame -------------------------
def chart_swap(run_dir, out, stamps):
    """Columns = every page-table dump in the log (state after that operation).
    Rows = RAM frame 0, frame 1, swap offset 0, swap offset 1."""
    path = os.path.join(run_dir, "os_4_swap.output")
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    dumps, slot, i = [], -1, 0
    while i < len(lines):
        m = re.match(r"Time slot\s+(\d+)", lines[i])
        if m:
            slot = int(m.group(1))
        m = re.match(r"===== PHYSICAL MEMORY AFTER (\w+) =====", lines[i])
        if m:
            what, desc, state, j = m.group(1), lines[i + 1].strip(), {}, i + 1
            where = {}
            while j + 1 < len(lines) and not lines[j + 1].startswith(("Time slot", "=====")):
                j += 1
                pm = re.search(r"page=(\d+) -> (frame|swapoff)=(\d+)", lines[j])
                if pm:
                    state[(pm.group(2), int(pm.group(3)))] = int(pm.group(1))
                    where.setdefault("_ln", {})[(pm.group(2), int(pm.group(3)))] = j + 1
            dumps.append(dict(slot=slot, what=what, desc=desc, state=state, line=i + 1,
                              ln=where.get("_ln", {})))
            i = j
        i += 1
    rows = [("frame", 0, "RAM frame 0"), ("frame", 1, "RAM frame 1"),
            ("swapoff", 0, "Swap off 0"), ("swapoff", 1, "Swap off 1")]
    pages = sorted({p for d in dumps for p in d["state"].values()})
    pcol = {p: PALETTE[k] for k, p in enumerate(pages)}
    short = {"ALLOCATION": "alloc", "WRITING": "write", "READING": "read",
             "DEALLOCATION": "free"}

    def label(d):
        m = re.search(r"Region=(\d+)", d["desc"]) or re.search(r"region=(\d+)", d["desc"])
        v = re.search(r"value=(\d+)", d["desc"])
        return f"{short.get(d['what'], d['what'].lower())} r{m.group(1) if m else '?'}" + \
            (f" ={v.group(1)}" if v else "")

    n = len(dumps)
    fig, h_px = new_fig(len(rows), 11, top_px=118, bottom_px=150, row_px=46 * PX)
    top, bottom = 118 * PX, 150 * PX
    ax = fig.add_axes([0.13, bottom / h_px, 0.84, 1 - (top + bottom) / h_px])
    ax.set_facecolor(SURFACE)
    ax.set_xlim(0, n)
    ax.set_ylim(len(rows) - 0.5, -0.5)
    ax.set_yticks(range(len(rows)))
    ax.set_yticklabels([r[2] for r in rows])
    ax.set_xticks([k + 0.5 for k in range(n)])
    ax.set_xticklabels([f"slot {d['slot']}\n(dòng {d['line']})" for d in dumps],
                       fontsize=8.5, color=INK2)
    ax.tick_params(length=0)
    for sp in ax.spines.values():
        sp.set_visible(False)
    ax.axhline(1.5, color=AXIS, linewidth=1, zorder=1)       # RAM | swap divider
    for k, d in enumerate(dumps):
        ax.text(k + 0.5, -0.78, label(d), ha="center", va="bottom", fontsize=9,
                color=INK, fontweight="bold")
        for r, (kind_, idx, _) in enumerate(rows):
            p = d["state"].get((kind_, idx))
            if p is None:
                ax.add_patch(Rectangle((k + 0.04, r - 0.32), 0.92, 0.64, facecolor="none",
                                       edgecolor=GRID, linewidth=1, zorder=2))
                continue
            ax.add_patch(Rectangle((k + 0.04, r - 0.32), 0.92, 0.64, facecolor=pcol[p],
                                   edgecolor="none", zorder=3))
            ax.text(k + 0.5, r, f"page {p}", ha="center", va="center", fontsize=9,
                    fontweight="bold", color=INK_ON.get(pcol[p], "#ffffff"), zorder=4)
    # swap-out events: a page that is in a swap slot now but was not before.
    # FIFO check: among pages resident in RAM just before, the victim must be
    # the one that entered RAM earliest (entry = first dump showing it in a frame).
    entered, notes, ev = {}, [], 0
    for k, d in enumerate(dumps):
        frames_now = {q: f for (kk, f), q in d["state"].items() if kk == "frame"}
        if k:
            prev = dumps[k - 1]["state"]
            in_ram_before = [q for (kk, _), q in prev.items() if kk == "frame"]
            for (kind_, idx), p in sorted(d["state"].items()):
                if kind_ != "swapoff" or prev.get((kind_, idx)) == p:
                    continue
                ev += 1
                mark = "①②③④⑤⑥"[ev - 1]
                ax.text(k + 0.08, 2 + idx - 0.27, mark, fontsize=9, color="#ffffff",
                        fontweight="bold", va="top", ha="left", zorder=5)
                oldest = min(in_ram_before, key=lambda q: entered[q][0])
                newcomer = [q for q in frames_now if q not in in_ram_before]
                fifo = "đúng FIFO" if oldest == p else f"KHÔNG phải FIFO (cũ nhất là page {oldest})"
                txt = (f"{mark} slot {d['slot']}, {label(d)} (dòng {d['ln'][(kind_, idx)]}): "
                       f"page {p} ra swap off {idx} — page {p} vào RAM từ slot {entered[p][0]} "
                       f"(dòng {entered[p][1]}), sớm nhất trong {sorted(in_ram_before)} → {fifo}")
                if newcomer:
                    q = newcomer[0]
                    txt += (f"; page {q} vào frame {frames_now[q]} "
                            f"(dòng {d['ln'][('frame', frames_now[q])]})")
                notes.append(txt)
        for q, f in frames_now.items():
            if q not in entered or (k and q not in
                                    [x for (kk, _), x in dumps[k - 1]["state"].items() if kk == "frame"]):
                entered[q] = (d["slot"], d["ln"][("frame", f)])
    for r, t in enumerate(notes):
        fig.text(0.02, (bottom - 64 * PX - r * 20 * PX) / h_px, t, fontsize=8.8, color=INK,
                 va="top")
    header(fig, "os_4_swap — RAM 2 frame: page nào nằm ở đâu sau mỗi thao tác",
           "Mỗi cột = một bảng trang in trong output_run/os_4_swap.output (PT[...] page -> "
           f"frame/swapoff) · ô viền rỗng = trống · chạy {stamps.get('started', '?')}", h_px)
    fig.legend(handles=[Patch(facecolor=pcol[p], label=f"page {p}") for p in pages],
               loc="lower center", ncol=len(pages), frameon=False, fontsize=9,
               bbox_to_anchor=(0.55, 0.0))
    fig.savefig(out, dpi=DPI, facecolor=SURFACE)
    plt.close(fig)
    print(f"saved {out}: {n} dumps, {ev} swap-out events")
    for t in notes:
        print("   ", t)


def main():
    run_dir, img_dir = sys.argv[1], sys.argv[2]
    rows, stamps = parse_console(run_dir)
    chart_tests(run_dir, os.path.join(img_dir, "16_chart_tests.png"), rows, stamps)
    mm = chart_mm64(run_dir, os.path.join(img_dir, "17_chart_mm64_stats.png"), rows, stamps)
    chart_demand(os.path.join(img_dir, "18_chart_demand.png"), mm, stamps)
    chart_timeline(run_dir, "sched_0", os.path.join(img_dir, "21_timeline_sched_0.png"),
                   "sched_0 — mỗi process: lúc chạy, lúc chờ (1 CPU)", stamps)
    chart_timeline(run_dir, "sched", os.path.join(img_dir, "22_timeline_sched.png"),
                   "sched — mỗi process: lúc chạy, lúc chờ (2 CPU)", stamps)
    chart_timeline(run_dir, "os_0_mlq_paging", os.path.join(img_dir, "23_timeline_os_0.png"),
                   "os_0_mlq_paging — mỗi process: lúc chạy, lúc chờ (2 CPU)", stamps)
    chart_swap(run_dir, os.path.join(img_dir, "24_swap_frames.png"), stamps)


if __name__ == "__main__":
    main()
