/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * DMA Interface Header
 */

#ifndef __DMA_H__
#define __DMA_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
struct pcb_t;

/*
 * do_dma - perform DMA function
 * caller: process control block
 * source: source memory address
 * destination: destination memory address
 * length: number of bytes to copy
 */
int do_dma(struct pcb_t *caller, uint64_t source, uint64_t destination, uint64_t length);

/*
 * Asynchronous CPU <-> co-processor interaction
 * ---------------------------------------------
 * The CPU posts a transfer descriptor into a mailbox and the DMA
 * co-processor thread (see co_processor() in os.c) consumes it,
 * performs the transfer, and raises dma_done_callback() as the
 * completion interrupt before signalling the waiting CPU.
 */

/* Submit a DMA request (BUS/physical addresses) and wait for the
 * completion interrupt of the co-processor */
int dma_submit_and_wait(struct pcb_t *caller, uint64_t src_phys,
                        uint64_t dst_phys, uint64_t length);

/* Co-processor service loop: consumes DMA descriptors forever.
 * Invoked by the co_processor thread in os.c */
void dma_worker_loop(void);

#ifdef __cplusplus
}
#endif

#endif /* __DMA_H__ */