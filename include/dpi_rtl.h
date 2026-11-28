/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * DPI RTL C prototype to interface with DMA RTL
 */

#ifndef DPI_RTL_WRAPPER_H
#define DPI_RTL_WRAPPER_H

#include <stdint.h>
#include "sysbus.h"

#ifdef __cplusplus
extern "C" {
#endif

// Verilator RTL simulation functions
int rtl_dma_init_simulation(void);
void rtl_dma_cleanup_simulation(void);
int dpi_rtl_dma_copy(uint64_t src_offset, uint64_t dst_offset, uint64_t length);
int dpi_rtl_dma_is_available(void);
void dma_done_callback(void);
int rtl_dma_start_copy(uint64_t src_offset, uint64_t dst_offset, uint64_t length);
int rtl_dma_poll(void);
int rtl_dma_wait(uint64_t timeout_cycles);
int rtl_dma_is_busy(void);
int rtl_dma_is_irq_pending(void);
void rtl_dma_ack_irq(void);

#ifdef __cplusplus
}
#endif

#endif
