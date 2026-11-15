/*
 * Copyright (C) 2025 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Sierra release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "common.h"

#define SYSMEM_MAP_OP 1
#define SYSMEM_INC_OP 2
#define SYSMEM_SWP_OP 3
#define SYSMEM_IO_READ 4
#define SYSMEM_IO_WRITE 5

extern struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid);
int liballoc(struct pcb_t *, addr_t, uint32_t);
int libfree(struct pcb_t *, uint32_t);
int libread(struct pcb_t*, uint32_t, addr_t, uint32_t*);
int libwrite(struct pcb_t*, BYTE, uint32_t, addr_t);
int libkmem_malloc(struct pcb_t*, uint32_t, uint32_t);
int libkmem_copy_from_user(struct pcb_t*, uint32_t, uint32_t, uint32_t, uint32_t);
int libkmem_copy_to_user(struct pcb_t*, uint32_t, uint32_t, uint32_t, uint32_t);
int libmem_dma_memcpy(struct pcb_t*, uint32_t, uint32_t, uint32_t, uint32_t);
int virt_to_phys(struct pcb_t *caller, addr_t vaddr, addr_t *paddr);
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller);
int pg_getval(struct mm_struct *mm, addr_t addr, BYTE *data, struct pcb_t *caller);
int pg_setval(struct mm_struct *mm, addr_t addr, BYTE value, struct pcb_t *caller);