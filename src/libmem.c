/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 *
 * demacOprx release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 *
 * System Library
 * Memory Module Library libmem.c
 */

// #ifdef MM_PAGING

#include "string.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include "dma.h"
#include "sysbus.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t kmm_lock = PTHREAD_MUTEX_INITIALIZER;

/* Mode independent paging helpers */
#ifdef MM64
#define OS_PAGESZ          PAGING64_PAGESZ
#define OS_PGN(addr)       ((addr_t)(addr) >> PAGING64_ADDR_PT_SHIFT)
#define OS_OFFST(addr)     ((addr_t)(addr) & (PAGING64_PAGESZ - 1))
#define OS_ALIGNSZ(sz)     PAGING64_PAGE_ALIGNSZ(sz)
#else
#define OS_PAGESZ          PAGING_PAGESZ
#define OS_PGN(addr)       PAGING_PGN(addr)
#define OS_OFFST(addr)     PAGING_OFFST(addr)
#define OS_ALIGNSZ(sz)     PAGING_PAGE_ALIGNSZ(sz)
#endif

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region */
  mm->mmap->vm_freerg_list = rg_elmt;

  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /*Allocate at the toproof */
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  int inc_sz=0;

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;

    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/
  /* No suitable free region: extend the vm area limit (sbrk) via the
   * MMU system call, then carve the new region out of the raised area.
   */

  /*Attempt to increate limit to get space */
#ifdef MM64
  inc_sz = (uint32_t)(size/(int)PAGING64_PAGESZ);
  inc_sz = inc_sz + 1;
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif
  addr_t old_sbrk;
  inc_sz = inc_sz + 1;

  old_sbrk = cur_vma->sbrk;

  /* TODO INCREASE THE LIMIT
   * SYSCALL 17 sys_memmap
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
#ifdef MM64
  regs.a3 = size;
#else
  regs.a3 = PAGING_PAGE_ALIGNSZ(size);
#endif
  if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /*Successful increase limit */
  caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->mm->symrgtbl[rgid].rg_end = old_sbrk + size;

  *alloc_addr = old_sbrk;

  /* The remainder of the newly raised (page aligned) area is collected
   * into the free region list for future allocation requests (Fig.6) */
  if ((addr_t)(old_sbrk + size) < cur_vma->sbrk)
  {
    struct vm_rg_struct *leftover = malloc(sizeof(struct vm_rg_struct));
    leftover->rg_start = old_sbrk + size;
    leftover->rg_end = cur_vma->sbrk;
    leftover->rg_next = NULL;
    enlist_vm_freerg_list(caller->mm, leftover);
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;

}

/* __kfree_region - internal helper releasing a kernel memory region
 * Puts the physically contiguous frames back to MEMRAM free list and
 * clears the kernel page table entries.
 */
static int __kfree_region(struct pcb_t *caller, struct vm_rg_struct *rgnode)
{
  struct krnl_t *krnl = caller->krnl;
  addr_t pgn_start = OS_PGN(rgnode->rg_start);
  addr_t pgn_end = OS_PGN(rgnode->rg_end + OS_PAGESZ - 1);
  addr_t pgit;

  for (pgit = pgn_start; pgit < pgn_end; pgit++)
  {
    addr_t pte = pte_get_entry(krnl->mm, pgit, caller);

    if (PAGING_PAGE_PRESENT(pte))
      MEMPHY_put_freefp(krnl->mram, PAGING_FPN(pte));

    pte_set_entry(krnl->mm, pgit, 0, caller);
  }

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  return 0;
}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);

  if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

#ifdef MM64
  /* A kernel region (kmalloc'd) is released back to the kernel
   * allocator instead of the userspace free region list */
  if (IS_KERNEL_ADDR(rgnode->rg_start))
  {
    __kfree_region(caller, rgnode);
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }
#endif

  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t  addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1)
  {
    printf("liballoc failed: PID=%d region=%d size=" FORMAT_ADDR "\n",
           proc->pid, reg_index, size);
    return -1;
  }

  proc->regs[reg_index] = addr;

#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  printf("===== PHYSICAL MEMORY AFTER ALLOCATION =====\n");
  printf("PID=%d - Region=%d - Address=" FORMAT_ADDR " - Size=" FORMAT_ADDR " byte\n",
         proc->pid, reg_index, addr, size);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  int val = __free(proc, 0, reg_index);
  if (val == -1)
  {
    printf("libfree failed: PID=%d region=%d\n", proc->pid, reg_index);
    return -1;
  }
printf("%s:%d\n",__func__,__LINE__);
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  printf("===== PHYSICAL MEMORY AFTER DEALLOCATION =====\n");
  printf("PID=%d - Region=%d\n", proc->pid, reg_index);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
  return 0;//val;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{

  uint64_t pte = pte_get_entry(mm, pgn, caller);

  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, make it actively living */
    addr_t vicpgn;
    addr_t vicpte;
    addr_t vicfpn, swpfpn;
    struct sc_regs regs;

    if (!(pte & PAGING_PTE_SWAPPED_MASK))
      return -1; /* The page was never mapped: invalid access */

    /* TODO Initialize the target frame storing our variable */
    addr_t tgtfpn = PAGING_SWP(pte); /* swap offset holding the page */

    /* TODO: Play with your paging theory here */
    /* Find victim page */
    if (find_victim_page(mm, &vicpgn) == -1)
    {
      return -1;
    }

    vicpte = pte_get_entry(mm, vicpgn, caller);
    vicfpn = PAGING_FPN(vicpte);

    /* Get free frame in MEMSWP */
    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
    {
      return -1;
    }

    /* TODO: Implement swap frame from MEMRAM to MEMSWP and vice versa*/

    /* TODO copy victim frame to swap
     * SWP(vicfpn <--> swpfpn)
     * SYSCALL 17 sys_memmap with SYSMEM_SWP_OP
     */
    regs.a1 = SYSMEM_SWP_OP;
    regs.a2 = vicfpn;
    regs.a3 = swpfpn;
    _syscall(caller->krnl, caller->pid, 17, &regs);

    /* Bring the target page from MEMSWAP back into the victim frame */
    __swap_cp_page(caller->krnl->active_mswp, tgtfpn,
                   caller->krnl->mram, vicfpn);

    /* The swap slot of the target page is recycled */
    MEMPHY_put_freefp(caller->krnl->active_mswp, tgtfpn);

    /* Update page table */
    pte_set_swap(caller, vicpgn, 0, swpfpn);

    /* Update its online status of the target page */
    pte_set_fpn(mm, pgn, vicfpn, caller);

    enlist_pgn_node(&mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(pte_get_entry(mm, pgn, caller));

  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, addr_t addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = OS_PGN(addr);
  int off = OS_OFFST(addr);
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  addr_t phyaddr = (addr_t)fpn * OS_PAGESZ + off;

  /* TODO
   *  MEMPHY_read(caller->krnl->mram, phyaddr, data);
   *  MEMPHY READ
   *  SYSCALL 17 sys_memmap with SYSMEM_IO_READ
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_READ;
  regs.a2 = phyaddr;
  regs.a3 = 0;

  if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0)
    return -1;

  /* The obtained value is returned in a3 by the syscall handler */
  *data = (BYTE)regs.a3;

  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, addr_t addr, BYTE value, struct pcb_t *caller)
{
  int pgn = OS_PGN(addr);
  int off = OS_OFFST(addr);
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  addr_t phyaddr = (addr_t)fpn * OS_PAGESZ + off;

  /* TODO
   *  MEMPHY_write(caller->krnl->mram, phyaddr, value);
   *  MEMPHY WRITE with SYSMEM_IO_WRITE
   * SYSCALL 17 sys_memmap
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_WRITE;
  regs.a2 = phyaddr;
  regs.a3 = value;

  if (_syscall(caller->krnl, caller->pid, 17, &regs) != 0)
    return -1;

  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  /* TODO Invalid memory identify */
  if (currg == NULL || cur_vma == NULL ||
      (currg->rg_start == 0 && currg->rg_end == 0))
  {
    printf("Invalid memory operation: PID=%d read on unallocated region %d\n",
           caller->pid, rgid);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

#ifdef MM64
  /* READ is userspace only: supervisor-space addresses are prohibited */
  if (IS_KERNEL_ADDR(currg->rg_start + offset))
  {
    printf("Invalid memory operation: PID=%d userspace READ on kernelspace address\n",
           caller->pid);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
#endif

  int val = pg_getval(caller->mm, currg->rg_start + offset, data, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return val;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,    // Source address = [source] + [offset]
    uint32_t* destination)
{
  BYTE data;
printf("%s:%d\n",__func__,__LINE__);
  int val = __read(proc, 0, source, offset, &data);

  if (val != 0)
    return val;

  *destination = (uint32_t)(unsigned char)data;
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  printf("===== PHYSICAL MEMORY AFTER READING =====\n");
  printf("read region=%d offset=" FORMAT_ADDR " value=%d\n",
         source, offset, (int)data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL ||
      (currg->rg_start == 0 && currg->rg_end == 0)) /* Invalid memory identify */
  {
    printf("Invalid memory operation: PID=%d write on unallocated region %d\n",
           caller->pid, rgid);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

#ifdef MM64
  /* WRITE is userspace only: supervisor-space addresses are prohibited */
  if (IS_KERNEL_ADDR(currg->rg_start + offset))
  {
    printf("Invalid memory operation: PID=%d userspace WRITE on kernelspace address\n",
           caller->pid);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
#endif

  int val = pg_setval(caller->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return val;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  printf("===== PHYSICAL MEMORY AFTER WRITING =====\n");
  printf("write region=%d offset=" FORMAT_ADDR " value=%d\n",
         destination, offset, (int)data);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}


/*libkmem_malloc- alloc region memory in kmem
 *@caller: caller
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 */

int libkmem_malloc(struct pcb_t * caller, uint32_t size, uint32_t reg_index)
{
  /*
   * TODO: provide OS level management
   *       and forward the request to helper
   */
  addr_t  addr = 0;
  addr_t  val = __kmalloc(caller, 0, reg_index, size, &addr);

  /*
   * TODO: provide OS kmem allocation validation
   */
  if (val == 0 || addr == 0)
  {
    printf("libkmem_malloc failed: PID=%d region=%d size=%u\n",
           caller->pid, reg_index, size);
    return -1;
  }

  caller->regs[reg_index] = addr;

#ifdef IODUMP
  printf("===== KERNEL MEMORY AFTER KMALLOC =====\n");
  printf("PID=%d - Region=%d - KernelAddress=%#lx - Size=%u byte\n",
         caller->pid, reg_index, (unsigned long)addr, size);
#endif

  return 0;
}


/*kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /* TODO: provide OS kernel memory allocation
   *       update krnl page tables for OS kernel level management */
#ifdef MM64
  pthread_mutex_lock(&kmm_lock);

  struct krnl_t *krnl = caller->krnl;
  struct mm_struct *kmm = krnl->mm;
  struct vm_area_struct *kvma = kmm->mmap;

  int npg = OS_ALIGNSZ(size) / OS_PAGESZ;
  addr_t startfpn = 0;
  addr_t kaddr;
  int pgit;

  if (size == 0 || rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&kmm_lock);
    return 0;
  }

  /* Kernel memory is simple CONTIGUOUS allocation: find a run of
   * physically contiguous frames in MEMRAM */
  if (MEMPHY_get_contiguous_freefp(krnl->mram, npg, &startfpn) != 0)
  {
    pthread_mutex_unlock(&kmm_lock);
    return 0; /* No physically contiguous block available */
  }

  /* The kernel area lives at the canonical high range started at
   * KERNEL_BASE_ADDR; first touch moves the kernel break up there */
  if (kvma->sbrk < KERNEL_BASE_ADDR)
  {
    kvma->vm_start = KERNEL_BASE_ADDR;
    kvma->vm_end = KERNEL_BASE_ADDR;
    kvma->sbrk = KERNEL_BASE_ADDR;
  }

  kaddr = kvma->sbrk;

  /* Map the contiguous frames into the kernel page table
   * (krnl->mm multi-level directory, kernel space is shared) */
  for (pgit = 0; pgit < npg; pgit++)
    pte_set_fpn(kmm, OS_PGN(kaddr) + pgit, startfpn + pgit, caller);

  kvma->sbrk += (addr_t)npg * OS_PAGESZ;
  kvma->vm_end = kvma->sbrk;

  /* Bookkeeping in the caller symbol table: the region carries a
   * kernelspace address, which the userspace READ/WRITE will refuse */
  caller->mm->symrgtbl[rgid].rg_start = kaddr;
  caller->mm->symrgtbl[rgid].rg_end = kaddr + size;

  *alloc_addr = kaddr;

  pthread_mutex_unlock(&kmm_lock);
  return kaddr;
#else
  return 0;
#endif
}



/*libmem_dma_memcpy - DMA memcpy
 *@caller: caller
 *@src: source region
 *@dst: destination region
 *@offset: memory offset
 *@size: copied data size
 */
int libmem_dma_memcpy(struct pcb_t *caller, uint32_t src, uint32_t dst, uint32_t offset, uint32_t size)
{
  /* TODO: provide DMA mempy */
  struct vm_rg_struct *rs = get_symrg_byid(caller->mm, src);
  struct vm_rg_struct *rd = get_symrg_byid(caller->mm, dst);

  if (rs == NULL || rd == NULL ||
      (rs->rg_start == 0 && rs->rg_end == 0) ||
      (rd->rg_start == 0 && rd->rg_end == 0))
  {
    printf("dma_memcpy failed: PID=%d invalid region src=%d dst=%d\n",
           caller->pid, src, dst);
    return -1;
  }

  /*
   *  TODO: provide asynchronous dma memcpy
   *        handling the dma_done_callback
   *
   * The CPU does not copy the data itself. It translates the virtual
   * addresses to BUS (physical) addresses through the MMU page tables
   * and posts a transfer descriptor to the co-processor mailbox. The
   * DMA engine (co_processor thread, see os.c) moves the bytes over
   * the system bus and raises dma_done_callback() as the completion
   * interrupt.
   */
  uint32_t copied = 0;
  while (copied < size)
  {
    addr_t svaddr = rs->rg_start + offset + copied;
    addr_t dvaddr = rd->rg_start + offset + copied;
    addr_t sphys, dphys;

    if (virt_to_phys(caller, svaddr, &sphys) != 0 ||
        virt_to_phys(caller, dvaddr, &dphys) != 0)
    {
      printf("dma_memcpy failed: PID=%d cannot translate bus address\n",
             caller->pid);
      return -1;
    }

    /* A DMA burst must stay inside one frame on both sides */
    uint32_t left = size - copied;
    uint32_t smax = OS_PAGESZ - OS_OFFST(svaddr);
    uint32_t dmax = OS_PAGESZ - OS_OFFST(dvaddr);
    uint32_t chunk = left < smax ? left : smax;
    chunk = chunk < dmax ? chunk : dmax;

    if (dma_submit_and_wait(caller, sphys, dphys, chunk) != 0)
    {
      printf("dma_memcpy failed: PID=%d DMA engine error\n", caller->pid);
      return -1;
    }

    copied += chunk;
  }

#ifdef IODUMP
  printf("===== DMA MEMCPY DONE =====\n");
  printf("PID=%d src_region=%d dst_region=%d offset=%u size=%u\n",
         caller->pid, src, dst, offset, size);
#endif

  return 0;
}


int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  uint32_t i;
  BYTE data;

  for (i = 0; i < size; i++)
  {
    if (__read_user_mem(caller, 0, source, offset + i, &data) != 0)
    {
      printf("copy_from_user failed: PID=%d invalid user region %d\n",
             caller->pid, source);
      return -1;
    }
    if (__write_kernel_mem(caller, 0, destination, offset + i, data) != 0)
    {
      printf("copy_from_user failed: PID=%d invalid kernel region %d\n",
             caller->pid, destination);
      return -1;
    }
  }

#ifdef IODUMP
  printf("===== COPY_FROM_USER DONE =====\n");
  printf("PID=%d user_region=%d -> kernel_region=%d offset=%u size=%u\n",
         caller->pid, source, destination, offset, size);
#endif

  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  uint32_t i;
  BYTE data;

  for (i = 0; i < size; i++)
  {
    if (__read_kernel_mem(caller, 0, source, offset + i, &data) != 0)
    {
      printf("copy_to_user failed: PID=%d invalid kernel region %d\n",
             caller->pid, source);
      return -1;
    }
    if (__write_user_mem(caller, 0, destination, offset + i, data) != 0)
    {
      printf("copy_to_user failed: PID=%d invalid user region %d\n",
             caller->pid, destination);
      return -1;
    }
  }

#ifdef IODUMP
  printf("===== COPY_TO_USER DONE =====\n");
  printf("PID=%d kernel_region=%d -> user_region=%d offset=%u size=%u\n",
         caller->pid, source, destination, offset, size);
#endif

  return 0;
}


/* virt_to_phys - translate a virtual address to the physical (bus) address
 *@caller: caller
 *@vaddr: virtual address (userspace or kernelspace)
 *@paddr: returned physical address
 *
 * CPU access passes through the MMU: user addresses walk the process
 * multi-level page table, kernel addresses walk the kernel table.
 */
int virt_to_phys(struct pcb_t *caller, addr_t vaddr, addr_t *paddr)
{
#ifdef MM64
  if (IS_KERNEL_ADDR(vaddr))
  {
    addr_t pte = pte_get_entry(caller->krnl->mm, OS_PGN(vaddr), caller);
    if (!PAGING_PAGE_PRESENT(pte))
      return -1;
    *paddr = (addr_t)PAGING_FPN(pte) * OS_PAGESZ + OS_OFFST(vaddr);
    return 0;
  }
#endif
  int fpn;
  if (pg_getpage(caller->mm, OS_PGN(vaddr), &fpn, caller) != 0)
    return -1;
  *paddr = (addr_t)fpn * OS_PAGESZ + OS_OFFST(vaddr);
  return 0;
}

/*__read_kernel_mem - read value in kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS memory operator for kernel memory region */
#ifdef MM64
  pthread_mutex_lock(&kmm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  if (currg == NULL ||
      (currg->rg_start == 0 && currg->rg_end == 0) ||
      !IS_KERNEL_ADDR(currg->rg_start))
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  addr_t kaddr = currg->rg_start + offset;
  addr_t pte = pte_get_entry(caller->krnl->mm, OS_PGN(kaddr), caller);

  if (!PAGING_PAGE_PRESENT(pte))
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  addr_t phyaddr = (addr_t)PAGING_FPN(pte) * OS_PAGESZ + OS_OFFST(kaddr);
  MEMPHY_read(caller->krnl->mram, phyaddr, data);

  pthread_mutex_unlock(&kmm_lock);
  return 0;
#else
  return -1;
#endif
}

/*__write_kernel_mem - write a kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS memory operator for kernel memory region */
#ifdef MM64
  pthread_mutex_lock(&kmm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  if (currg == NULL ||
      (currg->rg_start == 0 && currg->rg_end == 0) ||
      !IS_KERNEL_ADDR(currg->rg_start))
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  addr_t kaddr = currg->rg_start + offset;
  addr_t pte = pte_get_entry(caller->krnl->mm, OS_PGN(kaddr), caller);

  if (!PAGING_PAGE_PRESENT(pte))
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }

  addr_t phyaddr = (addr_t)PAGING_FPN(pte) * OS_PAGESZ + OS_OFFST(kaddr);
  MEMPHY_write(caller->krnl->mram, phyaddr, value);

  pthread_mutex_unlock(&kmm_lock);
  return 0;
#else
  return -1;
#endif
}

/*__read_user_mem - read value in user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS level management user memory access */
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  if (currg == NULL ||
      (currg->rg_start == 0 && currg->rg_end == 0))
    return -1;

#ifdef MM64
  if (IS_KERNEL_ADDR(currg->rg_start))
    return -1; /* not a userspace region */
#endif

  return pg_getval(caller->mm, currg->rg_start + offset, data, caller);
}


/*__write_user_mem - write a user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS level management user memory access */
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  if (currg == NULL ||
      (currg->rg_start == 0 && currg->rg_end == 0))
    return -1;

#ifdef MM64
  if (IS_KERNEL_ADDR(currg->rg_start))
    return -1; /* not a userspace region */
#endif

  return pg_setval(caller->mm, currg->rg_start + offset, value, caller);
}


/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum;
  addr_t fpn;
  addr_t pte;
  addr_t maxpgn = OS_PGN(caller->mm->mmap->vm_end + OS_PAGESZ - 1);

  for (pagenum = 0; pagenum < (int)maxpgn; pagenum++)
  {
    pte = pte_get_entry(caller->mm, pagenum, caller);

    if (pte == 0)
      continue;

    if (PAGING_PAGE_PRESENT(pte))
    {
      fpn = PAGING_FPN(pte);
      MEMPHY_put_freefp(caller->krnl->mram, fpn);
    }
    else
    {
      fpn = PAGING_SWP(pte);
      MEMPHY_put_freefp(caller->krnl->active_mswp, fpn);
    }
  }

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}


/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  /* FIFO page replacement: the page list is pushed at the head on
   * every mapping, therefore the OLDEST page is at the TAIL of the
   * list. We traverse to the tail, detach and return it. */
  if (!pg)
  {
    return -1;
  }

  struct pgn_t *prev = NULL;
  while (pg->pg_next)
  {
    prev = pg;
    pg = pg->pg_next;
  }
  *retpgn = pg->pgn;

  if (prev != NULL)
    prev->pg_next = NULL;
  else
    mm->fifo_pgn = NULL; /* single-node list emptied */

  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;

          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        {                                /*End of free list */
          rgit->rg_start = rgit->rg_end; // dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == (addr_t)-1) // new region not found
    return -1;

  return 0;
}

/* ============================================================
 * Asynchronous DMA mailbox: CPU <-> co-processor interaction
 * ============================================================
 * The CPU thread posts a transfer descriptor into a single-slot
 * mailbox and blocks on a condition variable. The co-processor
 * thread (co_processor() in os.c running dma_worker_loop()) picks
 * the descriptor up, drives the DMA engine (RTL or software
 * prototype), raises the completion interrupt dma_done_callback()
 * and finally wakes the CPU up. This models interrupt-driven I/O
 * (Fig.8 of the assignment).
 */

struct dma_request_t {
  struct pcb_t *caller;
  uint64_t src;
  uint64_t dst;
  uint64_t len;
  int done;
  int result;
};

static pthread_mutex_t dma_mb_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dma_mb_newreq = PTHREAD_COND_INITIALIZER;
static pthread_cond_t dma_mb_done = PTHREAD_COND_INITIALIZER;
static struct dma_request_t *dma_mailbox = NULL;

int dma_submit_and_wait(struct pcb_t *caller, uint64_t src_phys,
                        uint64_t dst_phys, uint64_t length)
{
  struct dma_request_t req;

  req.caller = caller;
  req.src = src_phys;
  req.dst = dst_phys;
  req.len = length;
  req.done = 0;
  req.result = -1;

  pthread_mutex_lock(&dma_mb_lock);

  /* Wait for the mailbox slot to be free */
  while (dma_mailbox != NULL)
    pthread_cond_wait(&dma_mb_done, &dma_mb_lock);

  /* Post the descriptor and ring the co-processor */
  dma_mailbox = &req;
  pthread_cond_signal(&dma_mb_newreq);

  /* The CPU now sleeps until the completion interrupt */
  while (!req.done)
    pthread_cond_wait(&dma_mb_done, &dma_mb_lock);

  pthread_mutex_unlock(&dma_mb_lock);

  return req.result;
}

void dma_worker_loop(void)
{
  for (;;)
  {
    struct dma_request_t *req;

    pthread_mutex_lock(&dma_mb_lock);
    while (dma_mailbox == NULL)
      pthread_cond_wait(&dma_mb_newreq, &dma_mb_lock);
    req = dma_mailbox;
    pthread_mutex_unlock(&dma_mb_lock);

    /* Make the OS physical RAM visible to the DMA engine over the
     * system bus (BUS addressing) at the first served request */
    if (!memory_bank_is_available() && req->caller != NULL &&
        req->caller->krnl != NULL && req->caller->krnl->mram != NULL)
    {
      memory_bank_attach((void *)req->caller->krnl->mram->storage,
                         (size_t)req->caller->krnl->mram->maxsz);
    }

    /* Drive the DMA engine */
    int rc = do_dma(req->caller, req->src, req->dst, req->len);

    /* Completion interrupt (IRQ line of the DMA module) */
    dma_done_callback();

    pthread_mutex_lock(&dma_mb_lock);
    req->result = rc;
    req->done = 1;
    dma_mailbox = NULL;
    pthread_cond_broadcast(&dma_mb_done);
    pthread_mutex_unlock(&dma_mb_lock);
  }
}

// #endif
