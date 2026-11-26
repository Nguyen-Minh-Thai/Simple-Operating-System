/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

//#ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Virtual memory module mm/mm-vm.c
 */

#include "string.h"
#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  if (mm == NULL || mm->mmap == NULL)
    return NULL;

  struct vm_area_struct *pvma = mm->mmap;
  while (pvma != NULL)
  {
    if ((int)pvma->vm_id == vmaid)
      return pvma;
    pvma = pvma->vm_next;

  }


  return NULL;
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn , addr_t swpfpn)
{
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
    return 0;
}

/*get_vm_area_node_at_brk - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t alignedsz)
{
  if (caller == NULL || caller->mm == NULL)
    return NULL;

  /* TODO retrive current vma to obtain newrg, current comment out due to compiler redundant warning*/
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (cur_vma == NULL)
    return NULL;

  if (cur_vma->sbrk + alignedsz > cur_vma->vm_end)
    return NULL;

  struct vm_rg_struct *newrg = malloc(sizeof(struct vm_rg_struct));
  if (newrg == NULL)
    return NULL;

  /* TODO: update the newrg boundary
   * The new region is planned right at the current break pointer
   * (top of the used area) and spans the aligned requested size.
   */
  newrg->rg_start = cur_vma->sbrk;
  newrg->rg_end = cur_vma->sbrk + alignedsz;
  newrg->rg_next = NULL;
  newrg->vmaid = vmaid;

  /* END TODO */

  return newrg;
}

/*validate_overlap_vm_area
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
int validate_overlap_vm_area(struct pcb_t *caller, int vmaid, addr_t vmastart, addr_t vmaend)
{
  if (caller == NULL || caller->mm == NULL)
    return -1;

  struct vm_area_struct *vma = caller->mm->mmap;

  /* TODO validate the planned memory area is not overlapped */
  if (vmastart >= vmaend || vma == NULL)


  {
    return -1;
  }
    /* TODO validate the planned memory area is not overlapped */

  struct vm_area_struct *cur_area = get_vma_by_num(caller->mm, vmaid);
  if (cur_area == NULL)
  {
    return -1;
  }

  while (vma != NULL)
  {
    /* The planned range [vmastart, vmaend) must not cut into any
     * OTHER memory area of the same process */
    if (vma != cur_area && OVERLAP(vmastart, vmaend, vma->vm_start, vma->vm_end))
    {
      return -1;
    }
    vma = vma->vm_next;
  }
  /* End TODO*/

  return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 */
int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
  if (caller == NULL || caller->mm == NULL || inc_sz == 0)
    return -1;

  /* TOTO with new address scheme, the size need tobe aligned
   *      the raw inc_sz maybe not fit pagesize
   */
#ifdef MM64
  addr_t inc_amt = PAGING64_PAGE_ALIGNSZ(inc_sz);
  int incnumpage = inc_amt / PAGING64_PAGESZ;
#else
  addr_t inc_amt = PAGING_PAGE_ALIGNSZ(inc_sz);
  int incnumpage = inc_amt / PAGING_PAGESZ;
#endif

  if (incnumpage <= 0)
    return -1;

  /* TODO: Obtain the new vm area based on vmaid */
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  if (cur_vma == NULL || cur_vma->sbrk + inc_amt > cur_vma->vm_end)
    return -1;

  struct vm_rg_struct *area = get_vm_area_node_at_brk(caller, vmaid, inc_amt);

  struct vm_rg_struct ret_rg;
  ret_rg.vmaid = vmaid;

  if (area == NULL)
    return -1;

  /* TODO Validate overlap of obtained region */
  if (validate_overlap_vm_area(caller, vmaid, area->rg_start, area->rg_end) < 0)
  {
    free(area);
    return -1; /*Overlap and failed allocation */
  }

  addr_t old_sbrk = cur_vma->sbrk;

  /* Raise the break inside [vm_start, vm_end) before mapping pages */
  cur_vma->sbrk = old_sbrk + inc_amt;

  /* The obtained vm area (only) now will be alloc real ram region */
  if (vm_map_ram(caller, area->rg_start, area->rg_end,
                 old_sbrk, incnumpage, &ret_rg) != 0)
  {
    cur_vma->sbrk = old_sbrk;
    free(area);
    return -1; /* Map the memory to MEMRAM failed */
  }
  

  free(area);
  return 0;
}

// #endif
