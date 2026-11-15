# Simple Operating System (ossim) — CO2018 Assignment HK261

Hoàn thiện hệ điều hành mô phỏng theo đề `hk261_ossim_demacOprx` (PDF gốc: `assignment.pdf`,
bản tóm lược: [`DE_BAI.md`](DE_BAI.md)):

- **Scheduler MLQ** — 140 hàng đợi priority, ngân sách slot cố định, round-robin trong mức.
- **Memory Management 64-bit** — bảng trang **5 cấp** (PGD→P4D→PUD→PMD→PT) cấp phát theo
  demand, swap FIFO, tách **user/kernel space** (kernel base = 2⁵⁶), `kmalloc` contiguous,
  `copy_from_user`/`copy_to_user`.
- **System calls** — framework bảng syscall, **PID passing** (không truyền PCB), syscall 440
  tự thêm theo mục 1.6.3.
- **Co-processor DMA** — tương tác **bất đồng bộ** CPU ↔ DMA qua mailbox + completion
  callback; chạy được cả **chip RTL (Verilator)** lẫn **software prototype** (tự phát hiện
  trong Makefile).

## Build & chạy

```bash
make all                 # tự chọn DMA backend: RTL nếu có Verilator, ngược lại software prototype
./os os_1_mlq_paging     # chạy 1 test
./run_tests.sh           # chạy cả 16 test (2 chế độ cấu hình), log vào output_run/
```

## Tài liệu

| File | Nội dung |
|---|---|
| [`BAO_CAO.md`](BAO_CAO.md) | **Báo cáo đầy đủ**: Gantt chart, sơ đồ dịch địa chỉ 5 cấp, ảnh chụp kết quả chạy thật, trả lời toàn bộ câu hỏi Q1–Q7, thống kê bảng trang |
| [`TESTING_GUIDE.md`](TESTING_GUIDE.md) | **Hướng dẫn tự test & verify từng kết quả** + giải thích vì sao từng kết quả đúng |
| [`DE_BAI.md`](DE_BAI.md) | Tóm lược có cấu trúc toàn bộ yêu cầu của đề |
| `output_run/` | Log chạy thật của 16 test case |
| `report/` | Script render ảnh/Gantt từ log thật + toàn bộ hình của báo cáo |

## Kết quả

**16/16 test case pass** (mọi process `has finished`, exit 0) — chi tiết trong
[`BAO_CAO.md`](BAO_CAO.md).
