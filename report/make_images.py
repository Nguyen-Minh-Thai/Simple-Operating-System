#!/usr/bin/env python3
"""Regenerate every log-derived image in report/images/ from ONE run in output_run/.

Sources are only the files in output_run/ (produced by run_tests.sh, plus
build.output captured right after it). Line windows are the same as the
original report images; they were re-checked against the logs of this run.

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

# (image, log, title, first_line, last_line)  -- None = whole file
SHOTS = [
    ("01_build.png", "build.output", "make all — build voi software DMA fallback", None, None),
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
