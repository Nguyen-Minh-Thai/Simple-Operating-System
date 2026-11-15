# ĐỀ BÀI — Assignment: Simple Operating System (ossim)

> **Nguồn:** `hk261_ossim_demacOprx.pdf` (33 trang, HK261 — CO2018 Operating Systems, HCMUT).
> File PDF gốc được giữ nguyên trong repo (`assignment.pdf`). Tài liệu này là bản tóm lược
> có cấu trúc **đầy đủ mọi yêu cầu** để tra cứu nhanh; chi tiết nguyên văn xem PDF.

## 0. Mục tiêu chung

Hoàn thiện một **hệ điều hành mô phỏng (ossim)** viết bằng C trên Linux, gồm 3 trụ cột:

1. **Scheduler (bộ định thời)** — Multi-Level Queue (MLQ) với priority.
2. **Memory Management** — cấp phát bộ nhớ ảo phân trang (paging), **64-bit multi-level paging (5 cấp)**, tách **user space / kernel space**, swap.
3. **System calls + Co-processor (DMA)** — giao tiếp user/kernel qua syscall (**PID passing, không truyền PCB**), tương tác **bất đồng bộ** CPU ↔ DMA engine.

Đồng bộ hóa (synchronization) bằng mutex là yêu cầu xuyên suốt mọi thành phần.

## 1. Tổng quan hệ thống & Syscall

- Kiến trúc mô phỏng: nhiều **CPU chạy song song (pthread)** + **loader** nạp process theo thời điểm + **timer** đồng bộ time slot + **co-processor (DMA)**.
- PCB (`common.h`): pid, prio, code segment, thanh ghi `regs[10]`, `mm` (memory mapping), con trỏ `krnl` (kernel object).
- Tập lệnh process: `calc`, `alloc`, `free`, `read`, `write`, `syscall`, `kmalloc`, `copy_from_user`, `copy_to_user`, `dma_memcpy`.
- **Syscall framework**: bảng `src/syscall.tbl` → sinh `syscall_tbl.lst`; dispatch trong `syscall.c`.
  - `syscall 0` — listsyscall: liệt kê mọi syscall.
  - `syscall 17` — memmap: các op `SYSMEM_MAP_OP` (vmap_pgd_memset), `SYSMEM_INC_OP` (inc_vma_limit), `SYSMEM_SWP_OP` (__mm_swap_page), `SYSMEM_IO_READ` (MEMPHY_read), `SYSMEM_IO_WRITE` (MEMPHY_write).
- **Mục 1.6.3 — Thêm syscall mới**: tạo `sys_xxxhandler.c` in ra `"The first system call parameter <a1>"`, đăng ký index **440** trong `syscall.tbl`, thêm vào `Makefile`; chạy `./os os_sc` để kiểm chứng.

**Câu hỏi (Q1):** Ưu/nhược điểm của **unified system call interface** (read/write/free dùng chung cho file, memory, I/O device) — xét ảnh hưởng tới thiết kế OS, hiệu năng, xử lý lỗi, tính khả chuyển.

**Câu hỏi (Q2):** Khi một syscall chạy quá lâu, OS **phát hiện và xử lý** thế nào?

## 2. Scheduler — MLQ

- Hoàn thiện `queue.c`: `enqueue()`, `dequeue()` (lấy proc có priority cao nhất), `purgequeue()`.
- Hoàn thiện `sched.c`: `get_mlq_proc()`, `put_mlq_proc()`, `add_mlq_proc()`.
- **Chính sách MLQ**: `MAX_PRIO = 140` hàng đợi; hàng đợi prio `i` có ngân sách cố định `slot[i] = MAX_PRIO - i`; duyệt **theo thứ tự cố định**, dùng hết ngân sách (hoặc hàng đợi rỗng) thì chuyển sang hàng đợi kế; phần việc còn lại chờ **vòng duyệt sau**.
- `prio` đọc từ file input mô tả process (khác `priority` mặc định trong PCB).
- Cần bảo vệ hàng đợi bằng **mutex** (nhiều CPU truy cập đồng thời).
- Lưu ý đề: `run_queue` là **legacy đã obsolete**, không dùng trong cả lý thuyết lẫn code.
- Kết quả có thể khác sample output vì loader & scheduler chạy song song — **chỉ cần giải thích được**.

**Câu hỏi (Q3):** Với các chính sách chi tiết của MLQ, **lợi ích của từng chính sách** là gì?

## 3. Memory Management

### 3.1 Process memory layout & cấu trúc dữ liệu
- Mỗi process có `mm_struct` riêng: bảng trang (`pgd/p4d/pud/pmd/pt`), danh sách `vm_area_struct` (vma), bảng ký hiệu vùng nhớ `symrgtbl[]` (region id = chỉ số thanh ghi), danh sách vùng trống `vm_freerg_list`, FIFO `fifo_pgn` phục vụ thay trang.
- `alloc size reg` → cấp region; hết chỗ thì **syscall 17 SYSMEM_INC_OP** nâng `sbrk`/`vm_end` (giãn vma) rồi map trang vật lý.
- `free reg` → thu hồi region vào free list.
- Hoàn thiện: `__alloc`, `__free`, `inc_vma_limit`, `get_vm_area_node_at_brk`, `validate_overlap_vm_area`, macro `INCLUDE`/`OVERLAP`, `find_victim_page` (FIFO), `get_free_vmrg_area`.

### 3.2 Multi-level paging 64-bit (MM64) — trọng tâm
- Địa chỉ ảo 57-bit, trang 4KB, 5 cấp thư mục **PGD[56:48] → P4D[47:39] → PUD[38:30] → PMD[29:21] → PT[20:12] → OFFSET[11:0]**, mỗi bảng 512 entry.
- Hoàn thiện trong `mm64.c`: `init_mm`, `pte_set_fpn`, `pte_set_swap`, `pte_get_entry`, `pte_set_entry`, `vmap_pgd_memset`, `vmap_page_range`, `alloc_pages_range`, `vm_map_ram`, `print_pgtbl`, `get_pd_from_address`.
- PTE 32-bit: bit PRESENT(31), SWAPPED(30), DIRTY(29); FPN [12:0] khi online; SWPTYP+SWPOFF khi ở swap.
- **Khuyến khích ra quyết định thiết kế** (so sánh trade-off thời gian duyệt bảng / số lần truy cập bộ nhớ / dung lượng lưu trữ) — **bắt buộc in thống kê** số lần truy cập bộ nhớ & dung lượng lưu trữ bảng trang đa cấp trong output và báo cáo.
- Page replacement là **tùy chọn** (do giảng viên quyết định) vì 64-bit phức tạp.

**Câu hỏi (Q4):** Động cơ chính của việc **kết hợp segmentation với paging**? Cách tiếp cận lai khắc phục hạn chế của từng kỹ thuật riêng lẻ ra sao?

**Câu hỏi (Q5):** **Lợi ích của việc mở rộng hierarchical paging lên N cấp**?

### 3.3 Physical memory
- Một RAM (`MEMRAM`) + tối đa 4 SWAP (`MEMSWP`), dùng chung cho mọi process.
- Hoàn thiện `mm-memphy.c`: `MEMPHY_get_freefp()` (quản lý free frame list), `MEMPHY_dump()`.
- Truy cập RAM qua syscall `SYSMEM_IO_READ/WRITE`; swap qua `SYSMEM_SWP_OP`.

**Câu hỏi (Q6):** Ưu/nhược điểm của **paging** so với **contiguous memory allocation**?

### 3.4 Kernel memory & dual-space
- **Tách user space / kernel space**: user KHÔNG được truy cập trực tiếp PCB/kernel; trong kernel space (syscall handler) chỉ dùng `struct krnl_t` + **PID passing**.
- `kmalloc size reg` — cấp bộ nhớ kernel **vật lý liên tục** (contiguous; cho phép chiến lược cấp phát liên tục đơn giản).
- `copy_from_user` / `copy_to_user` — chuyển dữ liệu qua biên user/kernel.
- Kernel space phải **giống nhau cho mọi process** (có thể dùng bảng trang riêng cho kernel).
- Lệnh READ/WRITE của user trên địa chỉ kernel phải bị **từ chối (invalid memory operation)**.

### 3.5 Cấu hình (`os-cfg.h`)
`MLQ_SCHED`, `MM_PAGING`, `MM64` (bật 64-bit 5 cấp), `MM_FIXED_MEMSZ` (chế độ legacy không đọc kích thước bộ nhớ từ input), `IODUMP`, `PAGETBL_DUMP`, `VDBG`.

## 4. Đồng bộ hóa

Bảo vệ mọi tài nguyên chia sẻ (hàng đợi scheduler, cấu trúc MM, MEMPHY) bằng mutex.

**Câu hỏi (Q7):** **Điều gì xảy ra nếu không xử lý đồng bộ hóa** trong Simple OS? Minh họa bằng output (đặc biệt ở các thao tác kernel memory mới thêm).

## 5. Co-processor — DMA engine

- DMA engine được cung cấp dưới dạng **chip RTL (Verilator)** + **software prototype** (Appendix, trang 33). Được phép dùng software prototype để phát triển/thử nghiệm; bản nộp nhóm (CE) dùng chip RTL.
- Hoàn thiện: `dma_memcpy` (lệnh của process), TODO map **OS memory address** trong `dpi_dma_copy` (`dma_c_testbench.cpp`), và **cơ chế tương tác bất đồng bộ** CPU ↔ co-processor với `dma_done_callback()` (ngắt hoàn thành) trong `co_processor()` (`os.c`).
- Hai góc nhìn truy cập bộ nhớ: **CPU đi qua MMU** (dịch địa chỉ ảo → vật lý), **co-processor truy cập thẳng bộ nhớ qua bus address**.

## 6. Input / Output format

```
[time slice] [N = số CPU] [M = số process]
[RAM_SZ] [SWPSZ0] [SWPSZ1] [SWPSZ2] [SWPSZ3]     ← bỏ dòng này nếu MM_FIXED_MEMSZ
[time 0] [path 0] [prio 0]
...
```
File process: dòng 1 = `[priority] [số lệnh]`, tiếp theo là từng lệnh.
So sánh với sample trong `output/` (không bắt buộc khớp 100%, chỉ cần **giải thích được**).

## 7. Yêu cầu nộp bài & Báo cáo (3 điểm)

Báo cáo phải:
- Trả lời **tất cả câu hỏi** trong đề (Q1–Q7 ở trên).
- **Scheduling:** vẽ **Gantt diagram** mô tả tiến trình chạy trên CPU.
- **Memory:** cho thấy trạng thái cấp phát bộ nhớ các data segment; chứng minh giao tiếp kernel/user qua syscall dùng **PID passing, không truyền PCB trực tiếp**.
- **Multi-level paging:** trình bày **sơ đồ dịch địa chỉ** đa cấp.
- **Thống kê**: số lần truy cập bộ nhớ + dung lượng lưu trữ bảng trang đa cấp, giải thích lợi ích thiết kế.
- Nén toàn bộ source + báo cáo thành `Assignment_<nhóm>.zip` nộp theo quy định.

## 8. Appendix

- Hướng dẫn cài Verilator ≥ 5.0 (build từ source), cấu trúc project RTL.
- **Software prototype của DMA engine**: hàm `dpi_rtl_dma_copy(src, dst, len)` copy byte-by-byte qua `memory_bank_get_ptr()` (bus addressing) — cơ sở cho fallback khi không có Verilator.
