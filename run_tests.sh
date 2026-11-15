#!/bin/bash
# =============================================================
# run_tests.sh - Build & run every test case of the assignment
# Outputs are saved into output_run/<test>.output
#
# Mode 1 (default os-cfg.h): MM64 paging, explicit memory sizes
#   -> os_* test inputs (have the memory configuration line)
# Mode 2 (MM_FIXED_MEMSZ):   legacy inputs without memory line
#   -> sched, sched_0, sched_1, os_1_singleCPU_mlq
# =============================================================
set -u
cd "$(dirname "$0")"

OUTDIR=output_run
mkdir -p $OUTDIR

PAGING_TESTS="os_sc os_syscall os_syscall_list os_0_mlq_paging os_1_mlq_paging \
os_2_mlq_paging os_1_singleCPU_mlq_paging os_2_singleCPU_mlq_paging \
os_1_mlq_paging_small_1K os_1_mlq_paging_small_4K os_3_kernel os_4_swap"

LEGACY_TESTS="sched sched_0 sched_1 os_1_singleCPU_mlq"

run_batch () {
    for t in $1; do
        echo "--- Running $t"
        timeout 300 ./os "$t" > "$OUTDIR/$t.output" 2>&1
        rc=$?
        fin=$(grep -c "has finished" "$OUTDIR/$t.output")
        echo "    exit=$rc processes_finished=$fin"
    done
}

echo "=============================================="
echo "[1/2] Standard mode (MM64 + explicit mem size)"
echo "=============================================="
sed -i 's|^#define MM_FIXED_MEMSZ|//#define MM_FIXED_MEMSZ|' include/os-cfg.h
make clean > /dev/null 2>&1
make all   > /dev/null 2>&1 || { echo "BUILD FAILED"; exit 1; }
run_batch "$PAGING_TESTS"

echo "=============================================="
echo "[2/2] Legacy mode (MM_FIXED_MEMSZ) for old inputs"
echo "=============================================="
sed -i 's|^//#define MM_FIXED_MEMSZ|#define MM_FIXED_MEMSZ|' include/os-cfg.h
make clean > /dev/null 2>&1
make all   > /dev/null 2>&1 || { echo "BUILD FAILED"; exit 1; }
run_batch "$LEGACY_TESTS"

# Restore default configuration and binary
sed -i 's|^#define MM_FIXED_MEMSZ|//#define MM_FIXED_MEMSZ|' include/os-cfg.h
make clean > /dev/null 2>&1
make all   > /dev/null 2>&1

echo "All done. Outputs are in $OUTDIR/"
