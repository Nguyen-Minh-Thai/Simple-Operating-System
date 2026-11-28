/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * DPI RTL C++ interface to  DMA RTL
 */

#include <verilated.h>
#include <verilated_vcd_c.h>
#include "Vdma_top.h"
#include "sysbus.h"
#include "dpi_rtl.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>
#include <atomic>

static Vdma_top* top = nullptr;
static VerilatedContext* context = nullptr;
static VerilatedVcdC* tracer = nullptr;

static bool is_tracing = false;
static uint64_t sim_time = 0;
static std::mutex rtl_mutex;
static std::thread* dma_worker = nullptr;
static std::atomic<int> dma_busy{0};
static std::atomic<int> dma_done_status{0};

extern "C" uint64_t dma_completion_count(void);

static int initialize_simulation_locked(void) {
    if (top) return 0;

    context = new VerilatedContext;
    const char* args[] = {};
    context->commandArgs(0, args);
    top = new Vdma_top(context, "dma_top");

    tracer = new VerilatedVcdC;
    context->traceEverOn(true);
    top->trace(tracer, 99);
    tracer->open("rtl_trace.vcd");
    is_tracing = true;
    sim_time = 0;

    return 0;
}

extern "C" int rtl_dma_init_simulation(void) {
    std::lock_guard<std::mutex> lock(rtl_mutex);
    return initialize_simulation_locked();
}

extern "C" void rtl_dma_cleanup_simulation(void) {
    if (dma_worker) {
        if (dma_worker->joinable())
            dma_worker->join();
        delete dma_worker;
        dma_worker = nullptr;
    }
    std::lock_guard<std::mutex> lock(rtl_mutex);
    if (tracer) {
        tracer->close();
        delete tracer;
        tracer = nullptr;
    }
    if (top) {
        delete top;
        top = nullptr;
    }
    if (context) {
        delete context;
        context = nullptr;
    }
    is_tracing = false;
    sim_time = 0;
}

extern "C" int dpi_rtl_dma_copy(uint64_t source, uint64_t destination, uint64_t length) {
    if (rtl_dma_start_copy(source, destination, length) != 0)
        return -1;
    return rtl_dma_wait(10000000U);
}

static void dma_worker_main(uint64_t source, uint64_t destination,
                            uint64_t length, uint64_t rtl_length) {
    uint64_t words = (rtl_length + 3U) / 4U;
    uint64_t max_cycles = 32U + words * 16U;
    if (max_cycles < 100000U)
        max_cycles = 100000U;
    if (max_cycles > 10000000U)
        max_cycles = 10000000U;

    uint64_t cycle = 0;
    int result = rtl_length == 0 ? 0 : -1;
    bool descriptor_accepted = rtl_length == 0;
    while (rtl_length != 0 && cycle < max_cycles) {
        {
            std::lock_guard<std::mutex> lock(rtl_mutex);
            if (!top)
                break;
            top->clk ^= 1;
            top->eval();
            if (is_tracing && tracer) {
                tracer->dump(sim_time);
                sim_time += 10;
            }
            if (!descriptor_accepted && top->desc_ready) {
                top->desc_valid = 0;
                top->eval();
                descriptor_accepted = true;
            }
            if (top->ch_done) {
                top->clk ^= 1;
                top->eval();
                top->clk ^= 1;
                top->eval();
                top->desc_valid = 0;
                top->ch_enable = 0;
                top->ch_pause = 0;
                top->eval();
                result = top->ch_error || !descriptor_accepted ||
                         top->ch_bytes_transferred != rtl_length ? -1 : 0;
                break;
            }
        }
        cycle++;
    }
    if (result == 0) {
        for (uint64_t offset = rtl_length; offset < length; offset++) {
            uint8_t* source_byte =
                static_cast<uint8_t*>(memory_bank_get_ptr(source + offset));
            uint8_t* destination_byte =
                static_cast<uint8_t*>(memory_bank_get_ptr(destination + offset));
            if (source_byte == nullptr || destination_byte == nullptr) {
                result = -1;
                break;
            }
            *destination_byte = *source_byte;
        }
    }
    if (rtl_length == 0)
        dma_done_callback();
    dma_done_status.store(result);
    dma_busy.store(0);
    /* Dispatch the IRQ only after the engine state is visible to the handler. */
    dma_irq_notify();
}

extern "C" int rtl_dma_start_copy(uint64_t source, uint64_t destination, uint64_t length) {
    uint64_t rtl_length;
    if (length == 0)
        return 0;
    if (length > UINT32_MAX ||
        source > UINT64_MAX - ((length + 3U) & ~UINT64_C(3)) ||
        destination > UINT64_MAX - ((length + 3U) & ~UINT64_C(3)))
        return -1;
    rtl_length = length & ~UINT64_C(3);

    if (dma_busy.load())
        return -1;
    if (dma_worker) {
        if (dma_worker->joinable())
            dma_worker->join();
        delete dma_worker;
        dma_worker = nullptr;
    }

    std::lock_guard<std::mutex> lock(rtl_mutex);
    if (dma_busy.load())
        return -1;
    if (initialize_simulation_locked() != 0 || !top)
        return -1;
    dma_done_status.store(-2);
    dma_busy.store(1);

    top->clk = 0;
    top->rst_n = 0;
    top->eval();
    top->clk ^= 1;
    top->eval();
    top->rst_n = 1;
    top->clk ^= 1;
    top->eval();
    top->clk ^= 1;
    top->eval();

    if (rtl_length != 0) {
        top->desc_valid = 1;
        top->desc_data_0 = source;
        top->desc_data_1 = destination;
        top->desc_data_2 = rtl_length;
        top->desc_data_3 = 0;
        top->ch_enable = 1;
        top->ch_pause = 0;
    }
    dma_worker = new std::thread(dma_worker_main, source, destination,
                                 length, rtl_length);
    return 0;
}

extern "C" int rtl_dma_poll(void) {
    if (dma_busy.load())
        return 0;
    return dma_done_status.load() == 0 ? 1 : -1;
}

extern "C" int rtl_dma_wait(uint64_t timeout_cycles) {
    for (uint64_t cycle = 0; cycle < timeout_cycles; cycle++) {
        int status = rtl_dma_poll();
        if (status != 0)
            return status == 1 ? 0 : -1;
    }
    return -1;
}

extern "C" int rtl_dma_is_busy(void) {
    return dma_busy.load() ? 1 : 0;
}

extern "C" int rtl_dma_is_irq_pending(void) {
    return dma_irq_is_pending();
}

extern "C" void rtl_dma_ack_irq(void) {
    dma_irq_ack();
}


extern "C" int dpi_rtl_dma_is_available(void) {
    std::lock_guard<std::mutex> lock(rtl_mutex);
    return top != nullptr;
}



