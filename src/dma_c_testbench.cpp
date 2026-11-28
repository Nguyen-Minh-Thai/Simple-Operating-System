/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * DMA C Testbench 
 */

/**
 * Build modes:
 *   DMA_MAIN=1 - Build as standalone executable with main()
 *   DMA_MAIN=0 - Build as library module with do_dma()
 */
   
#ifndef DMA_MAIN
#define DMA_MAIN 0
#endif

#include "sysbus.h"
#include "dpi_rtl.h"
#include "common.h"
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <cstdbool>
#include <thread>
#include <atomic>
#include <chrono>

#include <verilated.h>
#include <verilated_vcd_c.h>
#include "Vdma_top.h"
#include "Vdma_top__Dpi.h"  

extern "C" int memory_bank_reserve(size_t size_bytes);

#if DMA_MAIN == 1
extern "C" {
#include "common.h"
#include "os-mm.h"
}
#endif

extern "C" int dpi_rtl_dma_copy(uint64_t src_offset, uint64_t dst_offset, uint64_t length);
extern "C" int dpi_rtl_dma_is_available(void);

int dpi_dma_copy(struct pcb_t *caller, uint64_t src_offset, uint64_t dst_offset, uint64_t length) {
    uint64_t bus_length;
    size_t bank_size;

    if (caller == NULL || length > UINT64_MAX - 3U)
        return -1;
    if (length == 0)
        return 0;
    bus_length = (length + 3U) & ~UINT64_C(3);
    if (!memory_bank_is_available() ||
        src_offset > UINT64_MAX - bus_length ||
        dst_offset > UINT64_MAX - bus_length ||
        src_offset + bus_length > SIZE_MAX ||
        dst_offset + bus_length > SIZE_MAX ||
        memory_bank_reserve((size_t)((src_offset + bus_length >
                                     dst_offset + bus_length)
                                        ? src_offset + bus_length
                                        : dst_offset + bus_length)) != 0)
        return -1;

    bank_size = memory_bank_get_size();
    if (src_offset > bank_size || bus_length > bank_size - (size_t)src_offset ||
        dst_offset > bank_size || bus_length > bank_size - (size_t)dst_offset)
        return -1;

    return dpi_rtl_dma_copy(src_offset, dst_offset, length);
}

extern "C" int do_dma(struct pcb_t *caller,uint64_t source, uint64_t destination, uint64_t length) {
    return dpi_dma_copy(caller, source, destination, length);
}

extern "C" int do_dma_async(struct pcb_t *caller, uint64_t source, uint64_t destination, uint64_t length) {
    uint64_t bus_length;
    size_t bank_size;

    if (length > UINT64_MAX - 3U)
        return -1;
    if (length == 0)
        return 0;
    if (!memory_bank_is_available()) {
        if (caller == NULL || caller->krnl == NULL ||
            caller->krnl->mram == NULL)
            return -1;
        if (memory_bank_bind((void *)caller->krnl->mram->storage,
                             (size_t)caller->krnl->mram->maxsz) != 0)
            return -1;
    }
    bus_length = (length + 3U) & ~UINT64_C(3);
    if (!memory_bank_is_available() ||
        source > UINT64_MAX - bus_length ||
        destination > UINT64_MAX - bus_length ||
        source + bus_length > SIZE_MAX ||
        destination + bus_length > SIZE_MAX ||
        memory_bank_reserve((size_t)((source + bus_length >
                                      destination + bus_length)
                                         ? source + bus_length
                                         : destination + bus_length)) != 0)
        return -1;

    bank_size = memory_bank_get_size();
    if (source > bank_size || bus_length > bank_size - (size_t)source ||
        destination > bank_size || bus_length > bank_size - (size_t)destination ||
        rtl_dma_is_busy())
        return -1;
    return rtl_dma_start_copy(source, destination, length);
}

#if DMA_MAIN == 1
int main(int argc, char** argv) {
    uint64_t source = 0x1000;
    uint64_t destination = 0x2000;
    uint64_t length = 512;

    if (argc >= 4) {
        source = strtoull(argv[1], NULL, 0);
        destination = strtoull(argv[2], NULL, 0);
        length = strtoull(argv[3], NULL, 0);
    }
    
    printf("Running dma_testbench source=0x%llx, destination=0x%llx, length=%llu\n",
           (unsigned long long)source, (unsigned long long)destination, (unsigned long long)length);

    if (memory_bank_init(1024 * 1024) == MEMORY_BANK_INVALID) {
        fprintf(stderr, "Cannot initialize DMA memory bank\n");
        return 1;
    }

    if (length > UINT64_MAX - 3U) {
        fprintf(stderr, "DMA length is too large\n");
        memory_bank_cleanup();
        return 1;
    }
    uint64_t padded_length = (length + 3U) & ~UINT64_C(3);
    uint64_t required_size = source + padded_length;
    if (required_size < source) {
        memory_bank_cleanup();
        return 1;
    }
    if (destination + padded_length < destination) {
        memory_bank_cleanup();
        return 1;
    }
    if (destination + padded_length > required_size)
        required_size = destination + padded_length;
    if (required_size > SIZE_MAX ||
        memory_bank_reserve((size_t)required_size) != 0) {
        fprintf(stderr, "DMA memory range is too large\n");
        memory_bank_cleanup();
        return 1;
    }

    uint8_t* source_data = (uint8_t*)memory_bank_get_ptr(source);
    uint8_t* destination_data = (uint8_t*)memory_bank_get_ptr(destination);
    if (length != 0 && source_data == NULL) {
        fprintf(stderr, "Invalid DMA source range\n");
        memory_bank_cleanup();
        return 1;
    }
    if (length != 0 && destination_data == NULL) {
        fprintf(stderr, "Invalid DMA destination range\n");
        memory_bank_cleanup();
        return 1;
    }
    for (uint64_t i = 0; i < length; i++)
        source_data[i] = (uint8_t)((i % 255U) + 1U);

    bool check_tail_guard = (length & 3U) != 0 &&
        (source + padded_length <= destination ||
         destination + padded_length <= source);
    if (check_tail_guard) {
        for (uint64_t i = length; i < padded_length; i++)
            destination_data[i] = 0xa5U;
    }

    if (rtl_dma_init_simulation() != 0) {
        fprintf(stderr, "Cannot initialize RTL simulation\n");
        memory_bank_cleanup();
        return 1;
    }

    struct pcb_t *caller = (struct pcb_t *)calloc(1, sizeof(struct pcb_t));
    if (caller == NULL) {
        rtl_dma_cleanup_simulation();
        memory_bank_cleanup();
        return 1;
    }
    caller->krnl = (struct krnl_t *)calloc(1, sizeof(struct krnl_t));
    if (caller->krnl == NULL) {
        free(caller);
        rtl_dma_cleanup_simulation();
        memory_bank_cleanup();
        return 1;
    }
    caller->krnl->mm = (struct mm_struct *)calloc(1, sizeof(struct mm_struct));
    caller->krnl->mram = (struct memphy_struct *)calloc(1, sizeof(struct memphy_struct));
    caller->krnl->active_mswp = (struct memphy_struct *)calloc(1, sizeof(struct memphy_struct));
    caller->mm = caller->krnl->mm;
    caller->pid = 0;

    int result = 0;
    if (length != 0) {
        rtl_dma_ack_irq();
        result = do_dma_async(caller, source, destination, length);
    }
    if (result == 0 && length != 0) {
        unsigned int wait_ms = 0;
        while (rtl_dma_is_busy() && wait_ms < 10000U) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            wait_ms++;
        }
        if (rtl_dma_is_busy() || rtl_dma_poll() != 1 ||
            !rtl_dma_is_irq_pending()) {
            result = 1;
        } else {
            rtl_dma_ack_irq();
        }
    }
    if (result == 0 && length != 0) {
        if (destination_data == NULL ||
            memcmp(source_data, destination_data, (size_t)length) != 0)
            result = 1;
        if (result == 0 && check_tail_guard) {
            for (uint64_t i = length; i < padded_length; i++) {
                if (destination_data[i] != 0xa5U) {
                    result = 1;
                    break;
                }
            }
        }
    }

    free(caller->krnl->active_mswp);
    free(caller->krnl->mram);
    free(caller->krnl->mm);
    free(caller->krnl);
    free(caller);
    rtl_dma_cleanup_simulation();
    memory_bank_cleanup();

    return result;
}
#endif
