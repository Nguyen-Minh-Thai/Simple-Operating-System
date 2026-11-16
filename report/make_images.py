#!/usr/bin/env python3
"""Regenerate every log-derived image in report/images/ from ONE run in output_run/.

Sources are only the files in output_run/ (produced by run_tests.sh, plus
build.output captured right after it). Line windows are computed from content
anchors in each log (see WINDOWS), so a new run keeps showing the same part of
the log without hand-editing line numbers.

Not regenerated here: 15_addr_translation.png (a schematic drawn by
make_addr_diagram.py, contains no run data).

Usage (from the ossim/ directory): python report/make_images.py
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RUN = "output_run"
IMG = os.path.join("report", "images")
PY = sys.executable

def _find(lines, text, nth=1):
    """1-based line number of the nth line containing text."""
    hits = [i for i, l in enumerate(lines, 1) if text in l]
    if len(hits) < nth:
        raise SystemExit(f"anchor not found: {text!r} (#{nth})")
    return hits[nth - 1]


# image -> f(lines) -> (first, last). Same windows as the original report:
WINDOWS = {
    # first 40 lines: loading, first allocations and page-table dumps
    "04_os_0_alloc.png": lambda L: (1, 40),
    # from the page-table line just before "Time slot 21" to 3 lines before the end
    "05_os_0_rw.png": lambda L: (_find(L, "Time slot  21") - 1, len(L) - 3),
    # 4 lines before the first kmalloc dump .. 4 lines after copy_to_user is refused
    "06_os_2_kmalloc_dma.png": lambda L: (_find(L, "KERNEL MEMORY AFTER KMALLOC") - 4,
                                          _find(L, "copy_to_user failed") + 4),
    # 3 lines before the first write dump .. 3 lines after "read region=3"
    "07_os_3_kernel_copy.png": lambda L: (_find(L, "PHYSICAL MEMORY AFTER WRITING") - 3,
                                          _find(L, "read region=3") + 3),
    "09_sched_0.png": lambda L: (1, 45),
    # last 21 lines (end of run + MM64 stats)
    "10_os_1_stats.png": lambda L: (len(L) - 20, len(L)),
    "12_small_4K.png": lambda L: (1, 40),
}

# (image, log, title, first_line, last_line)  -- None = whole file / WINDOWS
SHOTS = [
    ("01_build.png", "build.output", "make all — build voi chip DMA RTL (Verilator)", None, None),
    ("02_os_sc.png", "os_sc.output", "./os os_sc — syscall 440 (custom system call)", None, None),
    ("03_os_syscall_list.png", "os_syscall_list.output", "./os os_syscall_list — syscall 0 (listsyscall)", None, None),
    ("04_os_0_alloc.png", "os_0_mlq_paging.output", "./os os_0_mlq_paging — alloc + page table dump", 1, 40),
    ("05_os_0_rw.png", "os_0_mlq_paging.output", "./os os_0_mlq_paging — write/read + ket thuc", 100, 138),
    ("06_os_2_kmalloc_dma.png", "os_2_mlq_paging.output", "./os os_2_mlq_paging — kmalloc + DMA async + copy guard", 25, 80),
    ("07_os_3_kernel_copy.png", "os_3_kernel.output", "./os os_3_kernel — copy_from_user -> copy_to_user round-trip", 10, 55),
    ("08_os_4_swap.png", "os_4_swap.output", "./os os_4_swap — swap out/in khi het RAM (2 frame)", None, None),
    ("09_sched_0.png", "sched_0.output", "./os sched_0 — MLQ scheduler (legacy mode)", 1, 45),
    ("10_os_1_stats.png", "os_1_mlq_paging.output", "./os os_1_mlq_paging — ket thuc + MM64 stats", 200, 220),
    ("11_os_syscall.png", "os_syscall.output", "./os os_syscall — xu ly loi alloc khi RAM < 1 trang", None, None),
    ("12_small_4K.png", "os_1_mlq_paging_small_4K.output", "./os os_1_mlq_paging_small_4K — 8 process, RAM nho", 1, 40),
    ("19_sched.png", "sched.output", "./os sched — MLQ 2 CPU, time slice 4 (legacy mode)", None, None),
]

GANTTS = [
    ("13_gantt_sched_0.png", "sched_0.output", "Gantt — sched_0 (1 CPU, MLQ: P1,P2 prio 4; P3,P4 prio 0)"),
    ("14_gantt_os_0.png", "os_0_mlq_paging.output", "Gantt — os_0_mlq_paging (2 CPU, 5 process)"),
    ("20_gantt_sched.png", "sched.output", "Gantt — sched (2 CPU: P1,P4 prio 0; P2,P3 prio 1)"),
]


def run(args):
    subprocess.run([PY] + args, cwd=ROOT, check=True)


def main():
    for img, log, title, a, b in SHOTS:
        if img in WINDOWS:
            with open(os.path.join(ROOT, RUN, log), encoding="utf-8", errors="replace") as f:
                a, b = WINDOWS[img](f.read().splitlines())
            print(f"{img}: {log} lines {a}-{b}")
        args = [os.path.join("report", "make_screenshot.py"), os.path.join(RUN, log),
                os.path.join(IMG, img), title]
        if a is not None:
            args += [str(a), str(b)]
        run(args)
    for img, log, title in GANTTS:
        run([os.path.join("report", "make_gantt.py"), os.path.join(RUN, log),
             os.path.join(IMG, img), title])
    run([os.path.join("report", "make_charts.py"), RUN, IMG])


if __name__ == "__main__":
    main()
