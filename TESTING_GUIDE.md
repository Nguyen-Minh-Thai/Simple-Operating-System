# HƯỚNG DẪN TEST & VERIFICATION — Simple Operating System

Tài liệu này hướng dẫn bạn **tự chạy lại toàn bộ kết quả** trong `BAO_CAO.md` và giải thích
**vì sao từng kết quả là đúng** để bạn kiểm chứng độc lập (không cần tin báo cáo).

> Mọi log tham chiếu ở đây nằm trong `output_run/` (kết quả chạy thật đã lưu).
> Bạn chạy lại sẽ ra kết quả **tương đương về bản chất** — thứ tự dòng có thể xê dịch
> nhẹ vì loader/scheduler/nhiều CPU chạy song song (chính đề cũng ghi chú điều này:
> *"there may be more than one correct answer for each test"*).

---

## 0. Chuẩn bị môi trường

```bash
sudo apt-get install build-essential        # gcc, g++, make
# (Tùy chọn) Verilator >= 5.0 nếu muốn chạy DMA bằng chip RTL thật
```

**Về DMA backend:** Makefile tự phát hiện Verilator:
- Có `verilated.h` → build engine RTL thật (Vdma_top, như đề yêu cầu cho bản nộp nhóm CE).
- Không có → tự dùng **software prototype** (`src/dpi_rtl_sw.c`) viết đúng theo Appendix
  trang 33 của đề (đề cho phép dùng prototype để phát triển/kiểm thử).
- Kiểm tra backend nào đang dùng: dòng đầu log — `Initialized dma software prototype engine`
  (software) hoặc log khởi tạo RTL của Verilator.

## 1. Build & chạy toàn bộ test một lệnh

```bash
make clean && make all          # build, ra binary ./os
./run_tests.sh                  # chạy cả 16 test, lưu log vào output_run/
```

**Kỳ vọng:** mỗi test in `exit=0` và `processes_finished=<đúng số process của input>`:

| Test | Số process phải finished | Test | Số process phải finished |
|---|---|---|---|
| os_sc | 1 | os_2_singleCPU_mlq_paging | 5 |
| os_syscall | 1 | os_1_mlq_paging_small_1K | 8 |
| os_syscall_list | 1 | os_1_mlq_paging_small_4K | 8 |
| os_0_mlq_paging | 5 | os_3_kernel | 1 |
| os_1_mlq_paging | 8 | os_4_swap | 1 |
| os_2_mlq_paging | 6 | sched / sched_0 / sched_1 | 4 / 4 / 5 |
| os_1_singleCPU_mlq_paging | 8 | os_1_singleCPU_mlq | 8 |

**Vì sao "mọi process finished" là tiêu chí đúng đắn số 1:** nếu scheduler sai
(mất process trong hàng đợi, dispatch trùng PCB, deadlock mutex) thì chương trình
hoặc treo (timeout 300s trong script) hoặc thiếu dòng `has finished`. Đếm đủ nghĩa là
chu trình add → get → put → finish của **mọi** process khép kín.

> Lưu ý: 4 test cuối (`sched*`, `os_1_singleCPU_mlq`) dùng **input định dạng legacy không có
> dòng kích thước bộ nhớ**, phải build với `MM_FIXED_MEMSZ` — `run_tests.sh` tự bật/tắt
> cấu hình này ở `include/os-cfg.h` cho bạn (xem phần [2/2] trong output script).

## 2. Verify từng khối chức năng

### 2.1 Scheduler MLQ — test `sched_0`

```bash
# cần build với MM_FIXED_MEMSZ (run_tests.sh đã làm; hoặc tự bật trong include/os-cfg.h)
./os sched_0
```

Input (`input/sched_0` + `input/proc/s0,s1`): P1, P2 vào sớm với **prio 4**; P3, P4 vào
sau với **prio 0**.

**Cách kiểm chứng:**
1. Nhìn các dòng `Loaded a process ... PRIO: x` — xác nhận prio đọc đúng từ input.
2. Giai đoạn đầu chỉ có P1/P2 → hai process phải **luân phiên đều nhau**
   (`Put process 1` → `Dispatched process 2` → ...). *Vì sao đúng:* cùng một hàng đợi
   prio 4, dequeue FIFO ⇒ round-robin chuẩn.
3. Sau khi P3/P4 (prio 0) xuất hiện, chúng **chưa chiếm CPU ngay** mà đợi một lúc rồi mới
   được phục vụ và chạy tới hết. *Vì sao đúng:* chính sách MLQ của đề là
   **ngân sách cố định + duyệt tuần tự** (`slot[i] = MAX_PRIO - i`), bộ định thời đang đứng
   ở mức 4 phải tiêu thụ xong phần vòng hiện tại rồi con trỏ mới quay về mức 0 — đây là
   điểm khác biệt cốt lõi với strict-priority-preemption, và là hành vi được mô tả nguyên
   văn trong đề ("the remaining work is carried out in the future round").
4. Đối chiếu trực quan: mở `report/images/13_gantt_sched_0.png` — Gantt này do
   `report/make_gantt.py` **parse từ log thật**, bạn có thể tự tạo lại:
   ```bash
   python3 report/make_gantt.py output_run/sched_0.output /tmp/gantt.png "Gantt sched_0"
   ```
5. So với `output/sched_0.output` (sample của đề): trình tự có thể khác chút do loader
   chạy song song, nhưng tính chất (round-robin trong mức, prio 0 được phục vụ dồn khi tới
   lượt) phải giống.

### 2.2 Syscall tự thêm (440) — test `os_sc`

```bash
./os os_sc
```

**Kỳ vọng:** dòng `The first system call parameter 1`.

**Vì sao đúng:** chương trình `input/proc/sc1` chứa lệnh `syscall 440 1`. Chuỗi thực thi:
`cpu.c` → `libsyscall(proc, 440, 1, 0)` → `_syscall()` tra bảng sinh từ `src/syscall.tbl`
(dòng chúng tôi thêm: `440 xxx sys_xxxhandler`) → `__sys_xxxhandler()` in `regs->a1 = 1`.
Đây **chính xác là bài kiểm tra ở mục 1.6.3 của đề**, và thông điệp khớp nguyên văn kết quả
mong đợi trong đề. Kiểm tra chéo: `./os os_syscall_list` (syscall 0) phải liệt kê đủ 3 entry
`0 listsyscall / 17 memmap / 440 xxx`; còn `./os os_syscall` gọi `syscall 101` không tồn tại
→ phải ra `ni_syscall ... Not Implemented` (chứng minh dispatch phân biệt đúng syscall có/không có).

### 2.3 Cấp phát bộ nhớ user + tái sử dụng vùng trống — test `os_0_mlq_paging`

```bash
./os os_0_mlq_paging
```

Chương trình `p0s`: `alloc 300 0` → `alloc 300 4` → `free 0` → `alloc 100 1` →
`write/read...`

**Checklist kiểm chứng (so với `output_run/os_0_mlq_paging.output`):**

| Quan sát trong log | Vì sao chứng minh code đúng |
|---|---|
| `Region=0 - Address=0 - Size=300` | Lần alloc đầu: vùng trống rỗng → syscall 17/`SYSMEM_INC_OP` giãn vma từ 0, region bắt đầu tại 0. |
| `Region=4 - Address=300` | **Điểm mấu chốt:** alloc 300B thứ hai KHÔNG mở trang mới mà lấy từ phần dư `[300,4096)` của trang 1 — chứng minh `__alloc` đã trả phần dư sau làm tròn trang vào free-region list (đúng Fig.6 của đề). |
| `Region=1 - Address=0` (sau `free 0`) | `free` đưa `[0,300)` về free list; alloc 100B sau đó cắt từ đầu vùng này — chứng minh `enlist_vm_freerg_list` + `get_free_vmrg_area` hoạt động. |
| Chỉ có `PT[000] page=0 → frame=N (present)` (một dòng PTE) | Cả 3 region (300+300+100 byte) nằm gọn 1 trang 4KB — đúng số học paging: dùng đúng **1 frame** duy nhất. |
| `write region=1 offset=20 value=100` rồi `read region=1 offset=20 value=100` | Giá trị đọc ra **bằng** giá trị ghi vào, qua đầy đủ chuỗi MMU: region→addr ảo→walk 5 cấp→frame→`SYSMEM_IO_READ/WRITE`. Nếu bảng trang/translation sai thì giá trị này sai hoặc crash. |

### 2.4 Swap — test `os_4_swap` (RAM 2 frame)

```bash
./os os_4_swap
```

Input: RAM = **8192 byte = đúng 2 frame 4KB**, swap 16MB; `m4s` cấp 3 trang:

**Trình tự phải thấy trong log (đối chiếu `output_run/os_4_swap.output`):**
1. 2 alloc đầu chiếm frame 0, 1 (`page=0 → frame=0`, `page=1 → frame=1`).
2. `write 11` vào region 1, `write 22` vào region 2.
3. `alloc 100 3` → log pgtbl cho thấy **`page=0 → swapoff=0 (swapped)`**: hết frame, FIFO
   chọn nạn nhân là page 0 (trang **vào RAM sớm nhất** — đúng định nghĩa FIFO), nội dung được
   copy ra swap, frame của nó cấp cho trang mới.
4. `read 1 0 5` → **`value=11`**: trang đang ở swap được nạp về (swap-in), và dump cuối hiện
   **`page=1 → swapoff=1 (swapped)`** (page 1 bị hy sinh nhường chỗ).

**Vì sao `value=11` là bằng chứng mạnh nhất:** con số 11 được ghi vào page 0 **trước khi** nó
bị đẩy ra swap; muốn đọc lại đúng, hệ phải (a) copy nguyên vẹn frame→swap khi swap-out,
(b) cập nhật PTE sang trạng thái swapped với đúng offset, (c) khi chạm lại phải nhận diện
PTE swapped, chọn nạn nhân mới, gọi đúng `SYSCALL 17/SYSMEM_SWP_OP`, copy swap→frame,
và cập nhật cả 2 PTE. Sai bất kỳ khâu nào → đọc ra rác hoặc crash. Giá trị khớp ⇒ cả
chuỗi swap đúng.

### 2.5 Kernel memory + PID passing — test `os_3_kernel`

```bash
./os os_3_kernel
```

Chương trình `m3s` (tự thiết kế để chứng minh round-trip):
`write 65,66,67` vào user region 1 → `kmalloc 256 2` → `copy_from_user 1 2` →
`alloc 100 3` → `copy_to_user 2 3` → `read 3 0`.

**Checklist:**

| Quan sát | Vì sao đúng |
|---|---|
| `KernelAddress=0x100000000000000` | = 2⁵⁶ = `KERNEL_BASE_ADDR` — region kernel nằm ở **không gian địa chỉ cao, tách khỏi user** (user region luôn có địa chỉ nhỏ như 0, 300…). Đây là thiết kế canonical-address: bit 56 bật = kernel space. |
| `COPY_FROM_USER DONE` rồi `COPY_TO_USER DONE` | Dữ liệu băng qua biên user→kernel→user bằng đúng 2 primitive được phép. |
| `read region=3 offset=0 value=65` | **65 chính là byte đầu đã ghi ở user region 1** — về được nguyên vẹn sau 2 lần vượt biên giới địa chỉ ⇒ bảng trang kernel riêng + đọc/ghi kernel memory đúng. |
| Thử truy cập trái phép | Trong `os_2_mlq_paging`, `m2s` của đề `copy_from_user` vào region **chưa kmalloc** → log phải có `copy_from_user failed: ... invalid kernel region 4` (từ chối, **không crash**) — xử lý lỗi graceful đúng tinh thần "invalid memory operation" của đề. |

**Verify PID passing (yêu cầu bắt buộc của đề) bằng cách đọc code:**
```bash
grep -n "pid" src/syscall.c src/sys_mem.c | head
grep -n "find_running_proc" src/sched.c src/sys_mem.c
```
Bạn sẽ thấy: user gọi `_syscall(caller->krnl, caller->pid, ...)` — **chỉ PID (số nguyên) đi vào
kernel**; handler `__sys_memmap(krnl, pid, regs)` tự tra `find_running_proc(pid)` trong
`running_list` của kernel (có mutex). Không có bất kỳ con trỏ PCB nào do user truyền vào
handler. Thử nghiệm phản chứng: nếu sửa PID gửi đi thành số bừa (vd 999), syscall sẽ in
`sys_memmap: no running process matches PID 999` và alloc thất bại — chứng minh kernel
thật sự *dùng* PID để tra chứ không dùng con trỏ có sẵn.

### 2.6 DMA bất đồng bộ — test `os_2_mlq_paging`

```bash
./os os_2_mlq_paging
```

**Checklist (đối chiếu `output_run/os_2_mlq_paging.output`):**
1. `Memory bank [0] attached to OS MEMRAM` — co-processor nhìn RAM của OS qua **bus
   address** (không qua MMU) — đúng phân biệt 2 góc nhìn trong đề.
2. `DMA CALL: bank=0, src=300, dst=4096, len=4` — **tự verify phép dịch địa chỉ:**
   `m2s` làm `alloc 300 0; alloc 100 1` → region 1 bắt đầu tại địa chỉ ảo 300, nằm trong
   page 0 → frame 0 ⇒ địa chỉ vật lý = 0×4096+300 = **300** ✓. `kmalloc 200 2` là kernel
   region đầu tiên → map vào frame kế tiếp (frame 1) ⇒ địa chỉ vật lý = 1×4096 = **4096** ✓.
   Hai con số trong log khớp số học ⇒ MMU translation cho DMA đúng.
3. Thứ tự dòng: `Calling RTL DMA...` → engine copy → **`DMA completed`** (callback
   `dma_done_callback()` — "ngắt" hoàn thành do **thread co-processor** phát, không phải CPU)
   → sau đó CPU mới in `DMA MEMCPY DONE`. Trình tự này chứng minh tương tác **bất đồng bộ**:
   CPU đẩy descriptor vào mailbox rồi ngủ, co-processor làm việc và báo ngắt.
4. Chạy trên máy có Verilator: cùng test này, engine sẽ là chip RTL (log của Verilator thay
   cho `software prototype`) — hành vi và kết quả copy giữ nguyên.

### 2.7 Đồng bộ hóa — test tải nặng

```bash
./os os_1_mlq_paging          # 4 CPU, 8 process, alloc/free/read/write đan xen
```
**Kỳ vọng:** đủ 8 `has finished`, không treo. Chạy lặp để tăng độ tin cậy:
```bash
for i in $(seq 20); do timeout 120 ./os os_1_mlq_paging > /tmp/t.log 2>&1 || echo "FAIL run $i"; grep -c "has finished" /tmp/t.log; done
```
**Vì sao đúng:** 4 CPU + loader + co-processor cùng mutate hàng đợi MLQ, free-region list,
free-frame list, mailbox DMA. 20 lần chạy × 8 process đều finished mà không crash/treo/sai
giá trị read ⇒ các mutex (`queue_lock`, `mmvm_lock`, `kmm_lock`, `dma_mb_lock`) phủ đúng
các vùng găng. Bằng chứng tĩnh bổ sung: trong `os_2` hai process kmalloc nhận
`0x…0000/0x…1000/0x…2000/0x…3000` — bốn địa chỉ kernel **không trùng nhau** (nếu thiếu
`kmm_lock`, race trên `sbrk` kernel sẽ cho địa chỉ trùng).

### 2.8 Trường hợp biên RAM < 1 trang — test `os_syscall`

```bash
./os os_syscall
```
Input khai báo RAM 2048 byte < 4096 (1 trang MM64) ⇒ **0 frame khả dụng**.
**Kỳ vọng:** `liballoc failed`, các `write` sau đó `Invalid memory operation`, chương trình
**không crash** và vẫn chạy tiếp syscall 101 → `ni_syscall`. *Vì sao đây là kết quả đúng:*
một input không đủ tài nguyên phải được từ chối có kiểm soát; mô phỏng chết vì segfault
mới là sai. (Input này vốn thiết kế cho chế độ trang 256B cũ; dưới MM64 nó trở thành
test biên về cạn tài nguyên.)

### 2.9 Thống kê bảng trang (yêu cầu báo cáo của đề)

Cuối **mọi** lần chạy có dòng:
```
MM64 stats: directory tables allocated=<X> (storage=<Y> KB), pte walks=<Z>
```
**Cách tự kiểm chứng số liệu:** với `os_4_swap` (1 process, chạm 3 trang user liên tiếp
page 0..2 — chung một nhánh PGD→P4D→PUD→PMD→PT): kernel mm tạo 1 bảng PGD lúc init +
process tạo 1 PGD + 4 bảng nhánh (P4D,PUD,PMD,PT) = **6 bảng × 4KB = 24KB** — khớp đúng
log `allocated=6 (storage=24 KB)`. Mỗi bảng 512 entry × 8B = 4KB; nếu dùng bảng phẳng cho
địa chỉ 57-bit sẽ cần 2⁴⁵ PTE ≈ hàng trăm TB ⇒ số 24KB chứng minh lợi ích demand allocation.

## 3. So sánh với sample output của đề

```bash
diff <(grep -o "has finished" output_run/os_1_mlq_paging.output | wc -l) \
     <(grep -o "has finished" output/os_1_mlq_paging.output | wc -l)
```
Sample trong `output/` được sinh bởi solution của giảng viên với timing máy khác; đề nói rõ
kết quả không cần khớp từng dòng, chỉ cần **cùng bản chất và giải thích được**:
cùng số process finished, alloc/free/read/write cùng ngữ nghĩa (giá trị read khớp giá trị
write), pgtbl dump cùng cấu trúc. Các mục 2.1–2.9 ở trên chính là bộ tiêu chí bản chất đó.

## 4. Tóm tắt file đã sửa/thêm (để review code)

| File | Nội dung chính |
|---|---|
| `src/queue.c` | enqueue/dequeue (max-priority)/purgequeue |
| `src/sched.c` | MLQ policy + running_list + `find_running_proc`/`remove_proc` |
| `include/sched.h` | sửa include guard trùng (`QUEUE_H`→`SCHED_H` — bug template), prototype mới |
| `src/mm64.c` | 5-level page table demand allocation (`pte_slot`), pte_set_fpn/swap/get/set, vmap_page_range, alloc_pages_range (kèm swap-out khi hết frame), init_mm, print_pgtbl, mm64_dump_stats |
| `src/mm-vm.c` | get_vm_area_node_at_brk, validate_overlap_vm_area, inc_vma_limit |
| `include/mm.h` | macro INCLUDE/OVERLAP; prototype contiguous alloc |
| `include/mm64.h` | `KERNEL_BASE_ADDR` (2⁵⁶) + `IS_KERNEL_ADDR` |
| `src/mm-memphy.c` | MEMPHY_get_freefp, MEMPHY_dump, **MEMPHY_get_contiguous_freefp** (cho kmalloc), fix init list |
| `src/libmem.c` | __alloc (leftover→free list), pg_getpage (swap in/out), pg_getval/setval (SYSMEM_IO qua syscall), __read/__write (+chặn kernel addr), __kmalloc (contiguous + bảng trang kernel), copy_from/to_user, __read/__write_user/kernel_mem, virt_to_phys, **DMA mailbox** (submit/worker), find_victim_page (FIFO) |
| `src/sys_mem.c` | **PID passing**: `find_running_proc(pid)` thay cho dummy PCB; propagate mã lỗi |
| `src/os.c` | co_processor chạy `dma_worker_loop()`; `remove_proc` trước khi free PCB; gọi `mm64_dump_stats()` |
| `src/sys_xxxhandler.c` + `src/syscall.tbl` + `Makefile` | syscall 440 (mục 1.6.3) |
| `src/dpi_rtl_sw.c` | software prototype DMA (Appendix) — fallback khi không có Verilator |
| `src/dma_c_testbench.cpp` | TODO map OS memory (bus addressing qua `krnl->mram->storage`) |
| `src/sysbus.c/.h` | `memory_bank_attach()` — gắn MEMRAM làm bank bus cho DMA |
| `input/os_3_kernel`, `input/proc/m3s` | test tự tạo: round-trip copy_from/to_user |
| `input/os_4_swap`, `input/proc/m4s` | test tự tạo: ép swap với RAM 2 frame |
| `run_tests.sh` | chạy 16 test cả 2 chế độ cấu hình |
| `report/*.py` | render ảnh từ log thật + Gantt parse từ log thật |
