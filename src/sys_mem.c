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
#include "sched.h"
#include <limits.h>
#include <stdio.h>

#ifdef MM64
#include "mm64.h"
#else
#include "mm.h"
#endif

//typedef char BYTE;

int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs* regs)
{
   struct pcb_t *caller;
   BYTE value;
   int memop;
   int result;

   if (krnl == NULL || regs == NULL || regs->a1 > INT_MAX)
      return -1;

   memop = (int)regs->a1;
   switch (memop) {
   case SYSMEM_MAP_OP:
   case SYSMEM_INC_OP:
   case SYSMEM_SWP_OP:
      caller = find_proc_by_pid(krnl, pid);
      if (caller == NULL)
         return -1;

      if (regs->a2 > INT_MAX)
         return -1;
      if (memop == SYSMEM_MAP_OP) {
         if (regs->a3 > INT_MAX)
            return -1;
         return vmap_pgd_memset(caller, regs->a2, (int)regs->a3);
      }
      if (memop == SYSMEM_INC_OP)
         return inc_vma_limit(caller, (int)regs->a2, regs->a3);
      return __mm_swap_page(caller, regs->a2, regs->a3);

   case SYSMEM_IO_READ:
      result = MEMPHY_read(krnl->mram, regs->a2, &value);
      if (result == 0)
         regs->a3 = (unsigned char)value;
      return result;

   case SYSMEM_IO_WRITE:
      return MEMPHY_write(krnl->mram, regs->a2, (BYTE)regs->a3);

   default:
      fprintf(stderr, "Unsupported memory operation: %d\n", memop);
      return -1;
   }
}
