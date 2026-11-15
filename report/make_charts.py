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
from matplotlib.patches import PathPatch
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


def main():
    run_dir, img_dir = sys.argv[1], sys.argv[2]
    rows, stamps = parse_console(run_dir)
    chart_tests(run_dir, os.path.join(img_dir, "16_chart_tests.png"), rows, stamps)
    mm = chart_mm64(run_dir, os.path.join(img_dir, "17_chart_mm64_stats.png"), rows, stamps)
    chart_demand(os.path.join(img_dir, "18_chart_demand.png"), mm, stamps)


if __name__ == "__main__":
    main()
