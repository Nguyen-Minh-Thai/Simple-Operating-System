/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "os-mm.h"
#include "syscall.h"
#include "libmem.h"
#include "queue.h"
#include "sched.h"
#include <stdlib.h>

#ifdef MM64
#include "mm64.h"
#else
#include "mm.h"
#endif

//typedef char BYTE;

int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs* regs)
{
   int memop = regs->a1;
   BYTE value;

   /*
    * @bksysnet: Please note in the dual spacing design
    *            syscall implementations are in kernel space.
    */

   /* TODO: Traverse proclist to find the matching proc
    *       user process are not allowed to access directly pcb in
    *       kernel space of syscall: only the PID crosses the boundary
    *       (PID passing, not PCB passing). The kernel resolves the
    *       PID in its own running list bookkeeping.
    */
   struct pcb_t *caller = find_running_proc(pid);

   /* TODO Maching and marking the process */
   if (caller == NULL)
   {
      printf("sys_memmap: no running process matches PID %u\n", pid);
      return -1;
   }

   int ret = 0;

   switch (memop) {
   case SYSMEM_MAP_OP:
            /* Reserved process case*/
            ret = vmap_pgd_memset(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_INC_OP:
            ret = inc_vma_limit(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_SWP_OP:
            ret = __mm_swap_page(caller, regs->a2, regs->a3);
            break;
   case SYSMEM_IO_READ:
            MEMPHY_read(krnl->mram, regs->a2, &value);
            regs->a3 = value;
            break;
   case SYSMEM_IO_WRITE:
            MEMPHY_write(krnl->mram, regs->a2, regs->a3);
            break;
   default:
            printf("Memop code: %d\n", memop);
            ret = -1;
            break;
   }

   return ret;
}