/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * SOFTWARE PROTOTYPE of the DMA engine (see assignment Appendix,
 * "Software prototype of the DMA engine"). This file provides the
 * same dpi_rtl.h interface as the Verilator RTL backend, so the whole
 * OS builds and runs on machines where Verilator is not installed.
 *
 * Build selection is automatic in the Makefile:
 *   - verilated.h found   -> real RTL engine (dpi_rtl.cpp + Vdma_top*)
 *   - verilated.h missing -> this software prototype
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "sysbus.h"
#include "dpi_rtl.h"

#ifndef DEBUG_PRINT
#define DEBUG_PRINT(...) printf(__VA_ARGS__)
#endif

static int sw_dma_ready = 0;

/*
 * rtl_dma_init_simulation - Initialize the (software) DMA engine
 */
int rtl_dma_init_simulation(void)
{
    if (sw_dma_ready)
        return 0;

    sw_dma_ready = 1;
    printf("Initialized dma software prototype engine (no Verilator RTL)\n");
    return 0;
}

/*
 * rtl_dma_cleanup_simulation - Cleanup the (software) DMA engine
 */
void rtl_dma_cleanup_simulation(void)
{
    sw_dma_ready = 0;
    printf("Clean up dma software prototype engine\n");
}

/*
 * dpi_rtl_dma_copy - Execute a DMA copy with the software prototype.
 * Assume that BUS addressing is provided through the memory bank
 * get_ptr() routine (see assignment Appendix listing).
 * source:      source memory offset (bus address)
 * destination: destination memory offset (bus address)
 * length:      number of bytes to copy
 */
int dpi_rtl_dma_copy(uint64_t source, uint64_t destination, uint64_t length)
{
    if (!sw_dma_ready) {
        printf("Cannot perform DMA copy: software DMA not initialized\n");
        return -1;
    }

    DEBUG_PRINT("DMA CALL: bank=0, src=%llu, dst=%llu, len=%llu\n",
                (unsigned long long)source,
                (unsigned long long)destination,
                (unsigned long long)length);

    /* Assume that BUS addressing provided through get_ptr() routine */
    void *src = memory_bank_get_ptr((size_t)source);
    void *dst = memory_bank_get_ptr((size_t)destination);

    if (src && dst) {
        /* Byte-by-byte transfer as encouraged by the assignment to
         * avoid endianness/ABI mismatches */
        uint8_t *s = (uint8_t *)src;
        uint8_t *d = (uint8_t *)dst;
        uint64_t i;

        for (i = 0; i < length; i++)
            d[i] = s[i];
    }

    return (src && dst) ? 0 : -1;
}

/*
 * dpi_rtl_dma_is_available - Check if the DMA engine is available
 */
int dpi_rtl_dma_is_available(void)
{
    return sw_dma_ready;
}
