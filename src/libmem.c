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
#include "dpi_rtl.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t kmm_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t dma_memcpy_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dma_memcpy_done = PTHREAD_COND_INITIALIZER;


struct dma_chunk {
  uint64_t source;
  uint64_t destination;
  uint32_t size;
  int use_dma;
};

struct dma_pending_copy {
  uint32_t pid;
  uint32_t source_region;
  uint32_t destination_region;
  struct memphy_struct *mram;
  struct dma_chunk *chunks;
  size_t chunk_count;
  size_t next_chunk;
  int result;
  int started;
  int complete;
  struct dma_pending_copy *next;
};

static struct dma_pending_copy *dma_pending_copies;
static struct dma_pending_copy *dma_active_copy;

struct kmem_region {
  uint32_t pid;
  uint32_t rgid;
  size_t size;
  addr_t start;
  struct kmem_region *next;
};

static struct kmem_region *kmem_regions;



int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt);

static int dma_start_chunk_locked(struct dma_pending_copy *pending)
{
  if (pending == NULL)
    return -1;

  while (pending->next_chunk < pending->chunk_count)
  {
    struct dma_chunk *chunk = &pending->chunks[pending->next_chunk];
    if (chunk->use_dma)
    {
      if (do_dma_async(NULL, chunk->source, chunk->destination,
                       chunk->size) != 0)
        return -1;
      dma_active_copy = pending;
      return 0;
    }

    for (uint32_t i = 0; i < chunk->size; i++)
    {
      BYTE value;
      if (MEMPHY_read(pending->mram, chunk->source + i, &value) != 0 ||
          MEMPHY_write(pending->mram, chunk->destination + i, value) != 0)
        return -1;
    }
    pending->next_chunk++;
  }

  pending->complete = 1;
  return 0;
}

static void dma_start_next_locked(void)
{
  struct dma_pending_copy *pending;

  if (dma_active_copy != NULL)
    return;

  for (pending = dma_pending_copies; pending != NULL; pending = pending->next)
  {
    if (pending->started || pending->complete)
      continue;
    pending->started = 1;
    if (dma_start_chunk_locked(pending) != 0)
    {
      pending->result = -1;
      pending->complete = 1;
      fprintf(stderr, "DMA could not start a request for PID %u\n",
              pending->pid);
      pthread_cond_broadcast(&dma_memcpy_done);
      continue;
    }
    if (pending->complete)
      pthread_cond_broadcast(&dma_memcpy_done);
    if (dma_active_copy != NULL)
      return;
  }
}

static int dma_has_pending_locked(uint32_t pid)
{
  struct dma_pending_copy *pending;

  for (pending = dma_pending_copies; pending != NULL; pending = pending->next)
  {
    if (pending->pid == pid)
      return 1;
  }
  return 0;
}

static int dma_region_has_pending_locked(uint32_t pid, uint32_t rgid)
{
  struct dma_pending_copy *pending;

  for (pending = dma_pending_copies; pending != NULL; pending = pending->next)
  {
    if (pending->pid == pid && !pending->complete &&
        (pending->source_region == rgid ||
         pending->destination_region == rgid))
      return 1;
  }
  return 0;
}

static void dma_collect_completed_region_locked(uint32_t pid, uint32_t rgid)
{
  struct dma_pending_copy **link = &dma_pending_copies;

  while (*link != NULL)
  {
    struct dma_pending_copy *pending = *link;
    if (pending->pid == pid && pending->complete &&
        (pending->source_region == rgid ||
         pending->destination_region == rgid))
    {
      *link = pending->next;
      free(pending->chunks);
      free(pending);
    }
    else
      link = &pending->next;
  }
}

/* Returns with dma_memcpy_lock held; the caller must then take mmvm_lock. */
static void dma_lock_region(struct pcb_t *caller, uint32_t rgid)
{
  pthread_mutex_lock(&dma_memcpy_lock);
  while (dma_region_has_pending_locked(caller->pid, rgid))
  {
    dma_collect_completed_region_locked(caller->pid, rgid);
    if (dma_region_has_pending_locked(caller->pid, rgid))
      pthread_cond_wait(&dma_memcpy_done, &dma_memcpy_lock);
  }
  dma_collect_completed_region_locked(caller->pid, rgid);
}

static int dma_has_inflight_locked(void)
{
  struct dma_pending_copy *pending;

  for (pending = dma_pending_copies; pending != NULL; pending = pending->next)
  {
    if (!pending->complete)
      return 1;
  }
  return 0;
}

static int dma_collect_completed_locked(uint32_t pid)
{
  struct dma_pending_copy **link = &dma_pending_copies;
  int result = 0;

  while (*link != NULL)
  {
    struct dma_pending_copy *pending = *link;
    if (pending->pid == pid && pending->complete)
    {
      if (pending->result != 0)
        result = -1;
      *link = pending->next;
      free(pending->chunks);
      free(pending);
    }
    else
      link = &pending->next;
  }
  return result;
}

static struct kmem_region *find_kmem_region(uint32_t pid, uint32_t rgid)
{
  struct kmem_region *region;

  for (region = kmem_regions; region != NULL; region = region->next)
  {
    if (region->pid == pid && region->rgid == rgid)
      return region;
  }
  return NULL;
}

static void forget_kmem_region(uint32_t pid, uint32_t rgid)
{
  struct kmem_region **link;

  pthread_mutex_lock(&kmm_lock);
  link = &kmem_regions;
  while (*link != NULL)
  {
    struct kmem_region *region = *link;
    if (region->pid == pid && region->rgid == rgid)
    {
      *link = region->next;
      free(region);
      break;
    }
    link = &region->next;
  }
  pthread_mutex_unlock(&kmm_lock);
}

static int remove_kmem_region(struct pcb_t *caller, uint32_t rgid)
{
  struct kmem_region **link = &kmem_regions;

  while (*link != NULL)
  {
    struct kmem_region *region = *link;
    if (region->pid == caller->pid && region->rgid == rgid)
    {
      struct vm_area_struct *kernel_vma = get_vma_by_num(caller->mm, 1);
      struct vm_rg_struct *freed_region;
      if (kernel_vma == NULL)
        return -1;
      freed_region = calloc(1, sizeof(*freed_region));
      if (freed_region == NULL)
        return -1;
      freed_region->vmaid = 1;
      freed_region->rg_start = region->start;
      freed_region->rg_end = region->start + region->size;
      if (enlist_vm_freerg_list(caller->mm, freed_region) != 0)
      {
        free(freed_region);
        return -1;
      }
      *link = region->next;
      free(region);
      return 0;
    }
    link = &region->next;
  }
  return -1;
}

void libkmem_release_process(uint32_t pid)
{
  struct kmem_region **link;

  pthread_mutex_lock(&kmm_lock);
  link = &kmem_regions;
  while (*link != NULL)
  {
    struct kmem_region *region = *link;
    if (region->pid == pid)
    {
      *link = region->next;
      free(region);
    }
    else
    {
      link = &region->next;
    }
  }
  pthread_mutex_unlock(&kmm_lock);
}

/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */

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

/* Helper: choose VMA ID (Small / Medium / Large) by requested size */
static int macos_select_vmaid_by_size(addr_t size)
{
#ifdef MM64
  return mm64_select_vmaid(size);
#else
  (void)size;
  return 0;
#endif
}

/* Helper: resolve which VMA contains a given virtual address */
static int macos_find_vmaid_by_addr(struct mm_struct *mm, addr_t addr)
{
  if (mm == NULL || mm->mmap == NULL)
    return 0;

#ifdef MM64
  if (addr >= MM64_SMALL_START && addr < MM64_SMALL_END)
    return MM64_VMA_SMALL_ID;
  if (addr >= MM64_MEDIUM_START && addr < MM64_MEDIUM_END)
    return MM64_VMA_MEDIUM_ID;
  if (addr >= MM64_LARGE_START && addr < MM64_LARGE_END)
    return MM64_VMA_LARGE_ID;
#endif

  struct vm_area_struct *vma = mm->mmap;
  while (vma != NULL)
  {
    if (addr >= vma->vm_start && addr < vma->vm_end)
      return (int)vma->vm_id;
    vma = vma->vm_next;
  }

  return 0;
}

/* Helper: safely fetch target VMA for free-region list operations */
static struct vm_area_struct *macos_get_target_vma(struct mm_struct *mm, int vmaid)
{
  if (mm == NULL || mm->mmap == NULL)
    return NULL;

  struct vm_area_struct *vma = mm->mmap;
  while (vma != NULL)
  {
    if ((int)vma->vm_id == vmaid)
      return vma;
    vma = vma->vm_next;
  }

  return mm->mmap;
}

/*enlist_vm_freerg_list - add new rg to freerg_list of its corresponding VMA
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  if (mm == NULL || mm->mmap == NULL || rg_elmt == NULL)
    return -1;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  struct vm_area_struct *target_vma = macos_get_target_vma(mm, rg_elmt->vmaid);
  if (target_vma == NULL)
    return -1;

  struct vm_rg_struct *rg_node = target_vma->vm_freerg_list;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  /* Enlist the new region into the matching VMA's free list */
  target_vma->vm_freerg_list = rg_elmt;

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
  int inc_sz = 0;

  if (caller == NULL || caller->mm == NULL || alloc_addr == NULL ||
      size == 0 || rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  vmaid = macos_select_vmaid_by_size(size);
  struct vm_area_struct *cur_vma = macos_get_target_vma(caller->mm, vmaid);
  if (cur_vma == NULL)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
    caller->mm->symrgtbl[rgid].vmaid = vmaid;

    *alloc_addr = rgnode.rg_start;

    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/
  /* No suitable free region: extend the vm area limit (sbrk) via the
   * MMU system call, then carve the new region out of the raised area.
   */
  addr_t req_inc = OS_ALIGNSZ(size);
  if (cur_vma->sbrk + req_inc > cur_vma->vm_end)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /*Attempt to increate limit to get space */
#ifdef MM64
  inc_sz = (int)(req_inc / OS_PAGESZ);
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif
  (void)inc_sz;

  addr_t old_sbrk = cur_vma->sbrk;

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
  caller->mm->symrgtbl[rgid].vmaid = vmaid;

  *alloc_addr = old_sbrk;

  /* The remainder of the newly raised (page aligned) area is collected
   * into the free region list for future allocation requests (Fig.6) */
  if ((addr_t)(old_sbrk + size) < cur_vma->sbrk)
  {
    struct vm_rg_struct *leftover = malloc(sizeof(struct vm_rg_struct));
    if (leftover != NULL)
    {
      leftover->rg_start = old_sbrk + size;
      leftover->rg_end = cur_vma->sbrk;
      leftover->vmaid = vmaid;
      leftover->rg_next = NULL;
      enlist_vm_freerg_list(caller->mm, leftover);
    }
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
  rgnode->vmaid = 0;
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
  (void)vmaid;
  if (caller == NULL || caller->mm == NULL || rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  dma_lock_region(caller, (uint32_t)rgid);
  pthread_mutex_lock(&mmvm_lock);

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);

  if (rgnode == NULL || (rgnode->rg_start == 0 && rgnode->rg_end == 0))
  {
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }

#ifdef MM64
  /* A kernel region (kmalloc'd) is released back to the kernel
   * allocator instead of the userspace free region list */
  if (IS_KERNEL_ADDR(rgnode->rg_start))
  {
    __kfree_region(caller, rgnode);
    forget_kmem_region(caller->pid, (uint32_t)rgid);
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return 0;
  }
#endif

  int owner_vmaid = macos_find_vmaid_by_addr(caller->mm, rgnode->rg_start);

  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  if (freerg_node == NULL)
  {
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }

  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->vmaid = owner_vmaid;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->vmaid = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  pthread_mutex_unlock(&dma_memcpy_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t addr;
  int vmaid = macos_select_vmaid_by_size(size);
  int val = __alloc(proc, vmaid, reg_index, size, &addr);
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
  int vmaid = 0;
  if (proc != NULL && proc->mm != NULL && reg_index < PAGING_MAX_SYMTBL_SZ)
    vmaid = macos_find_vmaid_by_addr(proc->mm, proc->mm->symrgtbl[reg_index].rg_start);

  int val = __free(proc, vmaid, reg_index);
  if (val == -1)
  {
    printf("libfree failed: PID=%d region=%d\n", proc->pid, reg_index);
    return -1;
  }
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
  printf("===== PHYSICAL MEMORY AFTER DEALLOCATION =====\n");
  printf("PID=%d - Region=%d\n", proc->pid, reg_index);
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
  return 0;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, addr_t pgn, int *fpn, struct pcb_t *caller)
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

static int dma_translate_address(struct pcb_t *caller, addr_t virtual_address,
                                 uint64_t *physical_address)
{
  struct mm_struct *address_mm;
  addr_t page_number;
  addr_t frame_address;
  int frame_number;

  if (caller == NULL || caller->krnl == NULL ||
      caller->krnl->mram == NULL || physical_address == NULL)
    return -1;

  if (IS_KERNEL_ADDR(virtual_address))
  {
    address_mm = caller->krnl->mm;
    if (address_mm == NULL)
      return -1;
  }
  else
  {
    address_mm = caller->mm;
    if (address_mm == NULL)
      return -1;
  }

  page_number = virtual_address / PAGING64_PAGESZ;
  if (pg_getpage(address_mm, page_number, &frame_number, caller) != 0 ||
      frame_number < 0)
    return -1;

  frame_address = (addr_t)frame_number * PAGING64_PAGESZ +
                  (virtual_address & (PAGING64_PAGESZ - 1));
  if (frame_address >= (addr_t)caller->krnl->mram->maxsz)
    return -1;
  *physical_address = (uint64_t)frame_address;
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
  addr_t pgn = OS_PGN(addr);
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
  addr_t pgn = OS_PGN(addr);
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
  if (caller == NULL || caller->mm == NULL ||
      rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ || data == NULL)
    return -1;

  dma_lock_region(caller, (uint32_t)rgid);
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = macos_get_target_vma(caller->mm, vmaid);

  /* TODO Invalid memory identify */
  if (currg == NULL || cur_vma == NULL ||
      (currg->rg_start == 0 && currg->rg_end == 0))
  {
    printf("Invalid memory operation: PID=%d read on unallocated region %d\n",
           caller->pid, rgid);
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }

  if (currg->rg_start + offset >= currg->rg_end)
  {
    printf("Invalid memory operation: PID=%d out-of-bounds offset=" FORMAT_ADDR " on region %d\n",
          caller->pid, offset, rgid);
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }

#ifdef MM64
  /* READ is userspace only: supervisor-space addresses are prohibited */
  if (IS_KERNEL_ADDR(currg->rg_start + offset))
  {
    printf("Invalid memory operation: PID=%d userspace READ on kernelspace address\n",
           caller->pid);
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }
#endif

  int val = pg_getval(caller->mm, currg->rg_start + offset, data, caller);

  pthread_mutex_unlock(&mmvm_lock);
  pthread_mutex_unlock(&dma_memcpy_lock);
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
  int vmaid = 0;
  if (proc != NULL && proc->mm != NULL && source < PAGING_MAX_SYMTBL_SZ)
    vmaid = macos_find_vmaid_by_addr(proc->mm, proc->mm->symrgtbl[source].rg_start);

  int val = __read(proc, vmaid, source, offset, &data);

  if (val != 0)
    return val;

  if (destination != NULL)
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
  if (caller == NULL || caller->mm == NULL ||
      rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  dma_lock_region(caller, (uint32_t)rgid);
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = macos_get_target_vma(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL ||
      (currg->rg_start == 0 && currg->rg_end == 0)) /* Invalid memory identify */
  {
    printf("Invalid memory operation: PID=%d write on unallocated region %d\n",
           caller->pid, rgid);
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }

  if (currg->rg_start + offset >= currg->rg_end)
  {
    printf("Invalid memory operation: PID=%d out-of-bounds offset=" FORMAT_ADDR " on region %d\n",
           caller->pid, offset, rgid);
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }

#ifdef MM64
  /* WRITE is userspace only: supervisor-space addresses are prohibited */
  if (IS_KERNEL_ADDR(currg->rg_start + offset))
  {
    printf("Invalid memory operation: PID=%d userspace WRITE on kernelspace address\n",
           caller->pid);
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }
#endif

  int val = pg_setval(caller->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  pthread_mutex_unlock(&dma_memcpy_lock);
  return val;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int vmaid = 0;
  if (proc != NULL && proc->mm != NULL && destination < PAGING_MAX_SYMTBL_SZ)
    vmaid = macos_find_vmaid_by_addr(proc->mm, proc->mm->symrgtbl[destination].rg_start);

  int val = __write(proc, vmaid, destination, offset, data);
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
int libkmem_malloc(struct pcb_t *caller, uint32_t size, uint32_t reg_index)
{
  struct kmem_region *region;
  addr_t addr = 0;

  if (caller == NULL || caller->mm == NULL || caller->krnl == NULL ||
      size == 0 || reg_index >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  pthread_mutex_lock(&kmm_lock);
  if (find_kmem_region(caller->pid, reg_index) != NULL)
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }
  pthread_mutex_unlock(&kmm_lock);

  region = calloc(1, sizeof(*region));
  if (region == NULL)
    return -1;

  addr_t val = __kmalloc(caller, 0, reg_index, size, &addr);

  if (val == 0 || addr == 0)
  {
    free(region);
    printf("libkmem_malloc failed: PID=%d region=%d size=%u\n",
           caller->pid, reg_index, size);
    return -1;
  }

  region->pid = caller->pid;
  region->rgid = reg_index;
  region->size = size;
  region->start = addr;
  pthread_mutex_lock(&kmm_lock);
  region->next = kmem_regions;
  kmem_regions = region;
  pthread_mutex_unlock(&kmm_lock);

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
  (void)vmaid;
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



/* Collect completed requests for a process; the IRQ handler advances DMA. */
int libmem_dma_poll(struct pcb_t *caller)
{
  int result;

  if (caller == NULL)
    return -1;

  pthread_mutex_lock(&dma_memcpy_lock);
  result = dma_collect_completed_locked(caller->pid);
  pthread_mutex_unlock(&dma_memcpy_lock);
  return result;
}

int libmem_dma_wait(struct pcb_t *caller)
{
  int result = 0;

  if (caller == NULL)
    return -1;

  pthread_mutex_lock(&dma_memcpy_lock);
  while (dma_has_pending_locked(caller->pid))
  {
    if (dma_collect_completed_locked(caller->pid) != 0)
      result = -1;
    if (dma_has_pending_locked(caller->pid))
      pthread_cond_wait(&dma_memcpy_done, &dma_memcpy_lock);
  }
  pthread_mutex_unlock(&dma_memcpy_lock);
  return result;
}

void libmem_dma_handle_interrupt(void)
{
  struct dma_pending_copy *active;
  int dma_status;
  int irq_pending;

  pthread_mutex_lock(&dma_memcpy_lock);
  active = dma_active_copy;
  dma_status = rtl_dma_poll();
  irq_pending = rtl_dma_is_irq_pending();
  rtl_dma_ack_irq();

  if (active != NULL)
  {
    dma_active_copy = NULL;
    if (dma_status != 1 || !irq_pending)
    {
      active->result = -1;
      active->complete = 1;
      fprintf(stderr, "DMA interrupt reported a failed transfer for PID %u\n",
              active->pid);
    }
    else
    {
      active->next_chunk++;
      if (active->next_chunk == active->chunk_count)
        active->complete = 1;
      else if (dma_start_chunk_locked(active) != 0)
      {
        active->result = -1;
        active->complete = 1;
        fprintf(stderr, "DMA could not start the next transfer for PID %u\n",
                active->pid);
      }
    }
  }

  dma_start_next_locked();
  pthread_cond_broadcast(&dma_memcpy_done);
  pthread_mutex_unlock(&dma_memcpy_lock);
}

void libmem_dma_wait_all(void)
{
  struct dma_pending_copy *pending;

  pthread_mutex_lock(&dma_memcpy_lock);
  while (dma_active_copy != NULL || dma_has_inflight_locked())
    pthread_cond_wait(&dma_memcpy_done, &dma_memcpy_lock);

  pending = dma_pending_copies;
  dma_pending_copies = NULL;
  while (pending != NULL)
  {
    struct dma_pending_copy *next = pending->next;
    free(pending->chunks);
    free(pending);
    pending = next;
  }
  pthread_mutex_unlock(&dma_memcpy_lock);
}

int libmem_dma_memcpy(struct pcb_t *caller, uint32_t src, uint32_t dst, uint32_t offset, uint32_t size)
{
  struct vm_rg_struct source_region;
  struct vm_rg_struct *current_source;
  struct vm_rg_struct *current_destination;
  struct memphy_struct *mram;
  struct kmem_region *dst_region;
  addr_t destination_start;
  addr_t source_address;
  addr_t destination_address;
  uint64_t copied = 0;
  struct dma_chunk *chunks = NULL;
  size_t chunk_count = 0;
  size_t chunk_capacity = 0;
  struct dma_pending_copy *pending;
  int result = -1;

  if (caller == NULL || caller->mm == NULL || caller->krnl == NULL ||
      caller->krnl->mram == NULL || size == 0 ||
      src >= PAGING_MAX_SYMTBL_SZ || dst >= PAGING_MAX_SYMTBL_SZ)
    return -1;
  mram = caller->krnl->mram;

  pthread_mutex_lock(&mmvm_lock);
  {
    struct vm_rg_struct *src_region = get_symrg_byid(caller->mm, (int)src);
    if (src_region == NULL || src_region->rg_end <= src_region->rg_start ||
        offset > src_region->rg_end - src_region->rg_start ||
        size > src_region->rg_end - src_region->rg_start - offset)
    {
      pthread_mutex_unlock(&mmvm_lock);
      return -1;
    }
    source_region = *src_region;
  }
  pthread_mutex_unlock(&mmvm_lock);

  pthread_mutex_lock(&kmm_lock);
  dst_region = find_kmem_region(caller->pid, dst);
  if (dst_region == NULL || offset > dst_region->size ||
      size > dst_region->size - offset)
  {
    pthread_mutex_unlock(&kmm_lock);
    return -1;
  }
  destination_start = dst_region->start;
  pthread_mutex_unlock(&kmm_lock);

  pthread_mutex_lock(&dma_memcpy_lock);
  pthread_mutex_lock(&mmvm_lock);
  current_source = get_symrg_byid(caller->mm, (int)src);
  if (current_source == NULL ||
      current_source->rg_start != source_region.rg_start ||
      current_source->rg_end != source_region.rg_end ||
      current_source->rg_end <= current_source->rg_start ||
      offset > current_source->rg_end - current_source->rg_start ||
      size > current_source->rg_end - current_source->rg_start - offset)
  {
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }

  pthread_mutex_lock(&kmm_lock);
  dst_region = find_kmem_region(caller->pid, dst);
  current_destination = get_symrg_byid(caller->mm, (int)dst);
  if (dst_region == NULL || current_destination == NULL ||
      current_destination->rg_start != dst_region->start ||
      current_destination->rg_end <= current_destination->rg_start ||
      offset > dst_region->size || size > dst_region->size - offset)
  {
    pthread_mutex_unlock(&kmm_lock);
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    free(chunks);
    return -1;
  }
  if (offset > current_destination->rg_end - current_destination->rg_start ||
      size > current_destination->rg_end - current_destination->rg_start - offset)
  {
    pthread_mutex_unlock(&kmm_lock);
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    free(chunks);
    return -1;
  }
  destination_start = dst_region->start;
  pthread_mutex_unlock(&kmm_lock);

  if (source_region.rg_start > (addr_t)-1 - offset ||
      destination_start > (addr_t)-1 - offset)
  {
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    free(chunks);
    return -1;
  }
  source_address = source_region.rg_start + offset;
  destination_address = destination_start + offset;

  while (copied < size)
  {
    addr_t source_virtual = source_address + copied;
    addr_t destination_virtual = destination_address + copied;
    uint64_t source_physical;
    uint64_t destination_physical;
    uint64_t chunk_size = size - copied;
    uint64_t source_page_left =
        PAGING64_PAGESZ - (source_virtual & (PAGING64_PAGESZ - 1));
    uint64_t destination_page_left =
        PAGING64_PAGESZ - (destination_virtual & (PAGING64_PAGESZ - 1));
    uint64_t dma_size;
    uint64_t remainder;
    size_t piece;

    if (chunk_size > source_page_left)
      chunk_size = source_page_left;
    if (chunk_size > destination_page_left)
      chunk_size = destination_page_left;
    if (dma_translate_address(caller, source_virtual, &source_physical) != 0 ||
        dma_translate_address(caller, destination_virtual,
                              &destination_physical) != 0 ||
        source_physical > UINT64_MAX - chunk_size ||
        destination_physical > UINT64_MAX - chunk_size ||
        source_physical + chunk_size > (uint64_t)mram->maxsz ||
        destination_physical + chunk_size >
            (uint64_t)mram->maxsz)
    {
      pthread_mutex_unlock(&mmvm_lock);
      pthread_mutex_unlock(&dma_memcpy_lock);
      free(chunks);
      return -1;
    }

    dma_size = chunk_size & ~UINT64_C(3);
    remainder = chunk_size - dma_size;
    for (piece = 0; piece < 2; piece++)
    {
      uint64_t piece_size = piece == 0 ? dma_size : remainder;
      struct dma_chunk *new_chunks;
      if (piece_size == 0)
        continue;
      if (chunk_count == chunk_capacity)
      {
        size_t new_capacity = chunk_capacity == 0 ? 8 : chunk_capacity * 2;
        if (new_capacity < chunk_capacity ||
            new_capacity > SIZE_MAX / sizeof(*chunks))
        {
          pthread_mutex_unlock(&mmvm_lock);
          pthread_mutex_unlock(&dma_memcpy_lock);
          free(chunks);
          return -1;
        }
        new_chunks = realloc(chunks, new_capacity * sizeof(*chunks));
        if (new_chunks == NULL)
        {
          pthread_mutex_unlock(&mmvm_lock);
          pthread_mutex_unlock(&dma_memcpy_lock);
          free(chunks);
          return -1;
        }
        chunks = new_chunks;
        chunk_capacity = new_capacity;
      }

      chunks[chunk_count].source =
          source_physical + (piece == 0 ? 0 : dma_size);
      chunks[chunk_count].destination =
          destination_physical + (piece == 0 ? 0 : dma_size);
      chunks[chunk_count].size = (uint32_t)piece_size;
      chunks[chunk_count].use_dma = piece == 0;
      chunk_count++;
    }
    copied += chunk_size;
  }

  pthread_mutex_unlock(&mmvm_lock);
  if (!memory_bank_is_available() &&
      memory_bank_bind((void *)mram->storage, (size_t)mram->maxsz) != 0)
  {
    pthread_mutex_unlock(&dma_memcpy_lock);
    free(chunks);
    return -1;
  }
  pending = calloc(1, sizeof(*pending));
  if (pending == NULL)
  {
    pthread_mutex_unlock(&dma_memcpy_lock);
    free(chunks);
    return -1;
  }
  pending->pid = caller->pid;
  pending->source_region = src;
  pending->destination_region = dst;
  pending->mram = mram;
  pending->chunks = chunks;
  pending->chunk_count = chunk_count;

  if (dma_pending_copies == NULL)
    dma_pending_copies = pending;
  else
  {
    struct dma_pending_copy *tail = dma_pending_copies;
    while (tail->next != NULL)
      tail = tail->next;
    tail->next = pending;
  }

  dma_start_next_locked();
  if (pending->complete && pending->result != 0)
  {
    struct dma_pending_copy **link = &dma_pending_copies;
    while (*link != pending)
      link = &(*link)->next;
    *link = pending->next;
    free(pending->chunks);
    free(pending);
  }
  else
    result = 0;
  pthread_mutex_unlock(&dma_memcpy_lock);
  return result;
}


int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  uint32_t i;
  BYTE data;

  if (caller == NULL || size == 0)
    return -1;
  if (libmem_dma_wait(caller) != 0)
  {
    fprintf(stderr, "DMA failed before copying from user memory for PID %u\n",
            caller->pid);
    return -1;
  }
  for (i = 0; i < size; i++)
  {
    if (__read_user_mem(caller, 0, (int)source, (addr_t)offset + i, &data) != 0 ||
        __write_kernel_mem(caller, 1, (int)destination,
                           (addr_t)offset + i, data) != 0)
      return -1;
  }
  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  uint32_t i;
  BYTE data;

  if (caller == NULL || size == 0)
    return -1;
  if (libmem_dma_wait(caller) != 0)
  {
    fprintf(stderr, "DMA failed before copying to user memory for PID %u\n",
            caller->pid);
    return -1;
  }
  for (i = 0; i < size; i++)
  {
    if (__read_kernel_mem(caller, 1, (int)source,
                          (addr_t)offset + i, &data) != 0 ||
        __write_user_mem(caller, 0, (int)destination, (addr_t)offset + i,
                         data) != 0)
      return -1;
  }
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
  (void)vmaid;
#ifdef MM64
  if (caller == NULL || caller->mm == NULL || caller->krnl == NULL ||
      rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ || data == NULL)
    return -1;

  dma_lock_region(caller, (uint32_t)rgid);
  pthread_mutex_lock(&mmvm_lock);
  pthread_mutex_lock(&kmm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  if (currg == NULL ||
      (currg->rg_start == 0 && currg->rg_end == 0) ||
      !IS_KERNEL_ADDR(currg->rg_start))
  {
    pthread_mutex_unlock(&kmm_lock);
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }

  addr_t kaddr = currg->rg_start + offset;
  addr_t pte = pte_get_entry(caller->krnl->mm, OS_PGN(kaddr), caller);

  if (!PAGING_PAGE_PRESENT(pte))
  {
    pthread_mutex_unlock(&kmm_lock);
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }

  addr_t phyaddr = (addr_t)PAGING_FPN(pte) * OS_PAGESZ + OS_OFFST(kaddr);
  int result = MEMPHY_read(caller->krnl->mram, phyaddr, data);

  pthread_mutex_unlock(&kmm_lock);
  pthread_mutex_unlock(&mmvm_lock);
  pthread_mutex_unlock(&dma_memcpy_lock);
  return result;
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
  (void)vmaid;
#ifdef MM64
  if (caller == NULL || caller->mm == NULL || caller->krnl == NULL ||
      rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  dma_lock_region(caller, (uint32_t)rgid);
  pthread_mutex_lock(&mmvm_lock);
  pthread_mutex_lock(&kmm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  if (currg == NULL ||
      (currg->rg_start == 0 && currg->rg_end == 0) ||
      !IS_KERNEL_ADDR(currg->rg_start))
  {
    pthread_mutex_unlock(&kmm_lock);
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }

  addr_t kaddr = currg->rg_start + offset;
  addr_t pte = pte_get_entry(caller->krnl->mm, OS_PGN(kaddr), caller);

  if (!PAGING_PAGE_PRESENT(pte))
  {
    pthread_mutex_unlock(&kmm_lock);
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }

  addr_t phyaddr = (addr_t)PAGING_FPN(pte) * OS_PAGESZ + OS_OFFST(kaddr);
  int result = MEMPHY_write(caller->krnl->mram, phyaddr, value);

  pthread_mutex_unlock(&kmm_lock);
  pthread_mutex_unlock(&mmvm_lock);
  pthread_mutex_unlock(&dma_memcpy_lock);
  return result;
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
  (void)vmaid;
  if (caller == NULL || caller->mm == NULL ||
      rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ || data == NULL)
    return -1;

  dma_lock_region(caller, (uint32_t)rgid);
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  if (currg == NULL ||
      (currg->rg_start == 0 && currg->rg_end == 0))
  {
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }

#ifdef MM64
  if (IS_KERNEL_ADDR(currg->rg_start))
  {
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1; /* not a userspace region */
  }
#endif

  int result = pg_getval(caller->mm, currg->rg_start + offset, data, caller);
  pthread_mutex_unlock(&mmvm_lock);
  pthread_mutex_unlock(&dma_memcpy_lock);
  return result;
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
  (void)vmaid;
  if (caller == NULL || caller->mm == NULL ||
      rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return -1;

  dma_lock_region(caller, (uint32_t)rgid);
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  if (currg == NULL ||
      (currg->rg_start == 0 && currg->rg_end == 0))
  {
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1;
  }

#ifdef MM64
  if (IS_KERNEL_ADDR(currg->rg_start))
  {
    pthread_mutex_unlock(&mmvm_lock);
    pthread_mutex_unlock(&dma_memcpy_lock);
    return -1; /* not a userspace region */
  }
#endif

  int result = pg_setval(caller->mm, currg->rg_start + offset, value, caller);
  pthread_mutex_unlock(&mmvm_lock);
  pthread_mutex_unlock(&dma_memcpy_lock);
  return result;
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
  addr_t max_end = 0;

  struct vm_area_struct *vma = caller->mm->mmap;
  while (vma != NULL)
  {
    if (vma->vm_end > max_end)
      max_end = vma->vm_end;
    vma = vma->vm_next;
  }
  addr_t maxpgn = OS_PGN(max_end + OS_PAGESZ - 1);

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
    mm->fifo_pgn = NULL;

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
  struct vm_area_struct *cur_vma = macos_get_target_vma(caller->mm, vmaid);
  if (cur_vma == NULL)
    return -1;

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    {
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;
      newrg->vmaid = vmaid;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      {
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;
          rgit->vmaid = nextrg->vmaid;
          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        {
          rgit->rg_start = rgit->rg_end;
          rgit->rg_next = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next;
    }
  }

  if (newrg->rg_start == (addr_t)-1)
    return -1;

  return 0;
}

// #endif
