/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

/*
 * PAGING based Memory Management
 * Memory management unit mm/mm.c
 */

#include "mm64.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <string.h>

#if defined(MM64)

/*
 * 5-level paging design note (design decision)
 * --------------------------------------------
 * The 64-bit virtual address (57-bit canonical) is split into
 *   PGD[56:48] P4D[47:39] PUD[38:30] PMD[29:21] PT[20:12] OFFSET[11:0]
 * Each directory level is a 512-entry (2^9) table. A full static tree
 * would cost 512^5 entries, which is impossible; we therefore use
 * DEMAND (dynamic) ALLOCATION of page tables: a lower-level table is
 * only created at the first time a virtual page covered by it is
 * mapped. Every directory entry stores the host pointer of the next
 * level table (addr_t is 64-bit so a pointer fits in an entry).
 * The leaf PT entry holds the 32-bit style PTE described in Fig.6.
 *
 * With this scheme a process touching only a few pages costs
 *   5 tables x 512 entries x 8 bytes = 20KB
 * instead of hundreds of petabytes, at the price of up to 4 extra
 * memory accesses per translation (see report for statistics).
 */

#define PD_ENTRIES 512

/* Bookkeeping statistics for the report */
static unsigned long mm64_stat_pd_tables = 0;  /* number of directory tables allocated */
static unsigned long mm64_stat_translations = 0; /* number of pte walks */

static addr_t *alloc_pd_table(void)
{
  addr_t *tbl = (addr_t *)calloc(PD_ENTRIES, sizeof(addr_t));
  mm64_stat_pd_tables++;
  return tbl;
}

/* walk the 5-level tree and return the address of the leaf PTE slot.
 * @mm    : memory mapping holding the top level pgd
 * @pgn   : page number (virtual address >> 12)
 * @alloc : when set, missing levels are created on demand,
 *          otherwise NULL is returned on a hole
 */
static addr_t *pte_slot(struct mm_struct *mm, addr_t pgn, int alloc)
{
  addr_t pgd_idx = 0, p4d_idx = 0, pud_idx = 0, pmd_idx = 0, pt_idx = 0;
  addr_t *p4d_tbl, *pud_tbl, *pmd_tbl, *pt_tbl;

  if (mm == NULL)
    return NULL;

  mm64_stat_translations++;

  get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

  if (mm->pgd == NULL)
  {
    if (!alloc)
      return NULL;
    mm->pgd = alloc_pd_table();
  }

  if (mm->pgd[pgd_idx] == 0)
  {
    if (!alloc)
      return NULL;
    mm->pgd[pgd_idx] = (addr_t)alloc_pd_table();
  }
  p4d_tbl = (addr_t *)mm->pgd[pgd_idx];

  if (p4d_tbl[p4d_idx] == 0)
  {
    if (!alloc)
      return NULL;
    p4d_tbl[p4d_idx] = (addr_t)alloc_pd_table();
  }
  pud_tbl = (addr_t *)p4d_tbl[p4d_idx];

  if (pud_tbl[pud_idx] == 0)
  {
    if (!alloc)
      return NULL;
    pud_tbl[pud_idx] = (addr_t)alloc_pd_table();
  }
  pmd_tbl = (addr_t *)pud_tbl[pud_idx];

  if (pmd_tbl[pmd_idx] == 0)
  {
    if (!alloc)
      return NULL;
    pmd_tbl[pmd_idx] = (addr_t)alloc_pd_table();
  }
  pt_tbl = (addr_t *)pmd_tbl[pmd_idx];

  /* Cache the lastly traversed tables for tracing/debug purpose */
  mm->p4d = p4d_tbl;
  mm->pud = pud_tbl;
  mm->pmd = pmd_tbl;
  mm->pt = pt_tbl;

  return &pt_tbl[pt_idx];
}

/* Select macOS-style VMA zone by requested allocation size:
 *   size <= 256B        -> VMA 0 (Small)
 *   256B < size <= 4KB  -> VMA 1 (Medium)
 *   size > 4KB          -> VMA 2 (Large, DMA-friendly)
 */
int mm64_select_vmaid(addr_t size)
{
  if (size <= MM64_SMALL_MAX_SZ)
    return MM64_VMA_SMALL_ID;
  if (size <= MM64_MEDIUM_MAX_SZ)
    return MM64_VMA_MEDIUM_ID;
  return MM64_VMA_LARGE_ID;
}

/* Expose statistics (used in report interpretation) */
void mm64_dump_stats(void)
{
  printf("MM64 stats: directory tables allocated=%lu (storage=%lu KB), pte walks=%lu\n",
         mm64_stat_pd_tables,
         (mm64_stat_pd_tables * PD_ENTRIES * sizeof(addr_t)) / 1024,
         mm64_stat_translations);
}

/*
 * init_pte - Initialize PTE entry
 */
int init_pte(addr_t *pte,
             int pre,    // present
             addr_t fpn,    // FPN
             int drt,    // dirty
             int swp,    // swap
             int swptyp, // swap type
             addr_t swpoff) // swap offset
{
  if (pre != 0) {
    if (swp == 0) { // Non swap ~ page online
      if (fpn == 0)
        return -1;  // Invalid setting

      /* Valid setting with FPN */
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);
    }
    else
    { // page swapped
      SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
      SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);
      CLRBIT(*pte, PAGING_PTE_DIRTY_MASK);

      SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
      SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
  }

  return 0;
}


/*
 * get_pd_from_pagenum - Parse address to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table
 */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Extract page direactories */
	*pgd = (addr&PAGING64_ADDR_PGD_MASK)>>PAGING64_ADDR_PGD_LOBIT;
	*p4d = (addr&PAGING64_ADDR_P4D_MASK)>>PAGING64_ADDR_P4D_LOBIT;
	*pud = (addr&PAGING64_ADDR_PUD_MASK)>>PAGING64_ADDR_PUD_LOBIT;
	*pmd = (addr&PAGING64_ADDR_PMD_MASK)>>PAGING64_ADDR_PMD_LOBIT;
	*pt = (addr&PAGING64_ADDR_PT_MASK)>>PAGING64_ADDR_PT_LOBIT;

	/* TODO: implement the page direactories mapping */
	/* The directory indexing is fully derived from the address bits
	 * above; the mapping itself (directory walking and the on-demand
	 * creation of lower level tables) is realized in pte_slot().
	 */

	return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 * @pgn   : pagenumer
 * @pgd   : page global directory
 * @p4d   : page level directory
 * @pud   : page upper directory
 * @pmd   : page middle directory
 * @pt    : page table
 */
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
	/* Shift the address to get page num and perform the mapping*/
	return get_pd_from_address(pgn << PAGING64_ADDR_PT_SHIFT,
                         pgd,p4d,pud,pmd,pt);
}


/*
 * pte_set_swap - Set PTE entry for swapped page
 * @pte    : target page table entry (PTE)
 * @swptyp : swap type
 * @swpoff : swap offset
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  addr_t *pte;

  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  pte = pte_slot(caller->mm, pgn, 1);
  if (pte == NULL)
    return -1;

  CLRBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 * @pte   : target page table entry (PTE)
 * @fpn   : frame page number (FPN)
 * @caller: caller
 */
int pte_set_fpn(struct mm_struct *mm, addr_t pgn, addr_t fpn, struct pcb_t *caller)
{
  addr_t *pte;
  struct mm_struct *target_mm = (mm != NULL) ? mm :
                                ((caller != NULL) ? caller->mm : NULL);

  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  pte = pte_slot(target_mm, pgn, 1);
  if (pte == NULL)
    return -1;

  *pte = 0;
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}


/* Get PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @caller: caller
 * @ret    : page table entry
 **/
addr_t pte_get_entry(struct mm_struct *mm, addr_t pgn, struct pcb_t *caller)
{
  addr_t *pte;
  struct mm_struct *target_mm = (mm != NULL) ? mm :
                                ((caller != NULL) ? caller->mm : NULL);

  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  pte = pte_slot(target_mm, pgn, 0);
  if (pte == NULL)
    return 0; /* unmapped hole: empty (non present) entry */

  return *pte;
}

/* Set PTE page table entry
 * @caller : caller
 * @pgn    : page number
 * @ret    : page table entry
 **/
int pte_set_entry(struct mm_struct *mm, addr_t pgn, uint64_t pte_val, struct pcb_t *caller)
{
  addr_t *pte;
  struct mm_struct *target_mm = (mm != NULL) ? mm :
                                ((caller != NULL) ? caller->mm : NULL);

  /* Get value from the system */
  /* TODO Perform multi-level page mapping */
  pte = pte_slot(target_mm, pgn, 1);
  if (pte == NULL)
    return -1;

  *pte = (addr_t)pte_val;

  return 0;
}

/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz
                    int pgnum)                      // num of mapping page
{
  int pgit = 0;
  uint64_t pattern = 0; /* clean empty (non-present) entries */

  /* TODO memset the page table with given pattern
   */
  addr_t pgn = addr >> PAGING64_ADDR_PT_SHIFT;

  for (pgit = 0; pgit < pgnum; pgit++)
    pte_set_entry(caller->mm, pgn + pgit, pattern, caller);

  return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address
 */
addr_t vmap_page_range(struct pcb_t *caller,           // process call
                    addr_t addr,                       // start address which is aligned to pagesz // start
                    int pgnum,                      // num of mapping page
                    struct framephy_struct *frames, // list of the mapped frames
                    struct vm_rg_struct *ret_rg)    // return mapped region, the real mapped fp
{                                                   // no guarantee all given pages are mapped
  struct framephy_struct *fpit = frames;
  int pgit = 0;
  addr_t pgn = addr >> PAGING64_ADDR_PT_SHIFT;

  /* TODO: update the rg_end and rg_start of ret_rg */
  if (ret_rg != NULL)
  {
    int vmaid = MM64_VMA_SMALL_ID;
    if (caller != NULL && caller->mm != NULL)
    {
      struct vm_area_struct *vma = caller->mm->mmap;
      while (vma != NULL)
      {
        if (addr >= vma->vm_start && addr < vma->vm_end)
        {
          vmaid = (int)vma->vm_id;
          break;
        }
        vma = vma->vm_next;
      }
    }
    ret_rg->rg_start = addr;
    ret_rg->rg_end = addr + (addr_t)pgnum * PAGING64_PAGESZ;
    ret_rg->vmaid = vmaid;
  }

  /* TODO map range of frame to address space
   *      [addr to addr + pgnum*PAGING_PAGESZ
   *      in page table caller->mm->pgd (5-level walked by pte_set_fpn)
   */
  for (pgit = 0; pgit < pgnum && fpit != NULL; pgit++)
  {
    pte_set_fpn(caller->mm, pgn + pgit, fpit->fpn, caller);

    /* Tracking for later page replacement activities (if needed)
     * Enqueue new usage page */
    enlist_pgn_node(&caller->mm->fifo_pgn, pgn + pgit);

    fpit = fpit->fp_next;
  }

  return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 * @caller    : caller
 * @req_pgnum : request page num
 * @frm_lst   : frame list
 */

addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  addr_t fpn;
  int pgit;
  struct framephy_struct *newfp_str = NULL;
  struct framephy_struct **cur = frm_lst;

  /* TODO: allocate the page */
  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) == 0)
    {
      newfp_str = malloc(sizeof(struct framephy_struct));
      newfp_str->fpn = fpn;
      newfp_str->fp_next = NULL;
      newfp_str->owner = caller->mm;

      *cur = newfp_str;
      cur = &newfp_str->fp_next;
    }
    else
    { /* TODO: ERROR CODE of obtaining somes but not enough frames
       * RAM has no free frame left: swap out a victim page to make room
       */
      addr_t vicpgn, swpfpn, vicfpn;
      addr_t vicpte;

      if (find_victim_page(caller->mm, &vicpgn) < 0)
        return -3000; /* Out of memory: no victim available */

      vicpte = pte_get_entry(caller->mm, vicpgn, caller);
      vicfpn = PAGING_FPN(vicpte);

      if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) < 0)
        return -3000; /* Out of swap space */

      /* Move victim frame content from MEMRAM out to MEMSWAP */
      __swap_cp_page(caller->krnl->mram, vicfpn,
                     caller->krnl->active_mswp, swpfpn);
      pte_set_swap(caller, vicpgn, 0, swpfpn);

      /* The victim frame is now free for the new page */
      newfp_str = malloc(sizeof(struct framephy_struct));
      newfp_str->fpn = vicfpn;
      newfp_str->fp_next = NULL;
      newfp_str->owner = caller->mm;

      *cur = newfp_str;
      cur = &newfp_str->fp_next;
    }
  }
  /* End TODO */

  return 0;
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 * @caller    : caller
 * @astart    : vm area start
 * @aend      : vm area end
 * @mapstart  : start mapping point
 * @incpgnum  : number of mapped page
 * @ret_rg    : returned region
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;

  /*@bksysnet: author provides a feasible solution of getting frames
   *FATAL logic in here, wrong behaviour if we have not enough page
   *i.e. we request 1000 frames meanwhile our RAM has size of 3 frames
   *Don't try to perform that case in this simple work, it will result
   *in endless procedure of swap-off to get frame and we have not provide
   *duplicate control mechanism, keep it simple
   */
  ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);

  if (ret_alloc < 0 && ret_alloc != -3000)
    return -1;

  /* Out of memory */
  if (ret_alloc == -3000)
  {
    return -1;
  }

  /* In macOS-style fixed-cap VMAs, vm_end is the ceiling of the VMA
   * while sbrk grows inside [vm_start, vm_end). Ensure the frames are
   * always mapped at the newly raised break [astart, aend) rather than
   * beyond the VMA ceiling even if a caller passes old vm_end.
   */
  addr_t map_addr = (mapstart >= astart && mapstart < aend) ? mapstart : astart;

  /* it leaves the case of memory is enough but half in ram, half in swap
   * do the swaping all to swapper to get the all in ram */
  vmap_page_range(caller, map_addr, incpgnum, frm_lst, ret_rg);

  return 0;
}

/* Swap copy content page from source frame to destination frame
 * @mpsrc  : source memphy
 * @srcfpn : source physical page number (FPN)
 * @mpdst  : destination memphy
 * @dstfpn : destination physical page number (FPN)
 **/
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
  int cellidx;
  addr_t addrsrc, addrdst;
#ifdef MM64
  for (cellidx = 0; cellidx < PAGING64_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING64_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING64_PAGESZ + cellidx;
#else
  for (cellidx = 0; cellidx < PAGING64_PAGESZ; cellidx++)
  {
    addrsrc = srcfpn * PAGING64_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING64_PAGESZ + cellidx;
#endif

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/*
 * Initialize an empty Memory Management instance (macOS-style layout)
 * @mm:     self mm
 * @caller: mm owner (NULL for kernel mm, non-NULL for user process)
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  /* TODO init page table directory */
  /* Top level directory is allocated here, the lower levels
   * (p4d/pud/pmd/pt) are demand-allocated at the first mapping
   * touching them (sparse multi-level page table design) */
  mm->pgd = (addr_t *)calloc(PD_ENTRIES, sizeof(addr_t));
  mm64_stat_pd_tables++;
  mm->p4d = NULL;
  mm->pud = NULL;
  mm->pmd = NULL;
  mm->pt = NULL;

  if (caller == NULL)
  {
    /* Kernel memory mapping (os.mm): single shared supervisor VMA
     * residing in canonical high space [KERNEL_BASE_ADDR .. 2^57) */
    struct vm_area_struct *kvma = malloc(sizeof(struct vm_area_struct));
    kvma->vm_id = 0;
    kvma->vm_start = KERNEL_BASE_ADDR;
    kvma->vm_end = KERNEL_BASE_ADDR;
    kvma->sbrk = KERNEL_BASE_ADDR;
    kvma->vm_freerg_list = NULL;
    kvma->vm_next = NULL;
    kvma->vm_mm = mm;
    mm->mmap = kvma;
  }
  else
  {
    /* macOS-style user address layout (Section 2.2.1 & Page 31):
     *   [0x00000000 .. 0x00001000) : __PAGEZERO (unmapped null guard)
     *   VMA 0 (Small)  : [0x10000000 .. 0x20000000) - 256MB
     *   VMA 1 (Medium) : [0x20000000 .. 0x40000000) - 512MB
     *   VMA 2 (Large)  : [0x40000000 .. 0x80000000) - 1GB
     */
    struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));
    struct vm_area_struct *vma1 = malloc(sizeof(struct vm_area_struct));
    struct vm_area_struct *vma2 = malloc(sizeof(struct vm_area_struct));

    vma0->vm_id = MM64_VMA_SMALL_ID;
    vma0->vm_start = MM64_SMALL_START;
    vma0->vm_end = MM64_SMALL_END;
    vma0->sbrk = vma0->vm_start;
    vma0->vm_freerg_list = NULL;

    vma1->vm_id = MM64_VMA_MEDIUM_ID;
    vma1->vm_start = MM64_MEDIUM_START;
    vma1->vm_end = MM64_MEDIUM_END;
    vma1->sbrk = vma1->vm_start;
    vma1->vm_freerg_list = NULL;

    vma2->vm_id = MM64_VMA_LARGE_ID;
    vma2->vm_start = MM64_LARGE_START;
    vma2->vm_end = MM64_LARGE_END;
    vma2->sbrk = vma2->vm_start;
    vma2->vm_freerg_list = NULL;

    /* Link the 3 macOS-style size-class VMAs */
    vma0->vm_next = vma1;
    vma1->vm_next = vma2;
    vma2->vm_next = NULL;

    /* Point vma owner backward */
    vma0->vm_mm = mm;
    vma1->vm_mm = mm;
    vma2->vm_mm = mm;

    mm->mmap = vma0;
  }

  memset(mm->symrgtbl, 0, sizeof(mm->symrgtbl));
  mm->fifo_pgn = NULL;
  mm->kcpooltbl = NULL;

  return 0;
}

struct vm_rg_struct *init_vm_rg(addr_t rg_start, addr_t rg_end)
{
  struct vm_rg_struct *rgnode = malloc(sizeof(struct vm_rg_struct));

  rgnode->rg_start = rg_start;
  rgnode->rg_end = rg_end;
  rgnode->rg_next = NULL;

  return rgnode;
}

int enlist_vm_rg_node(struct vm_rg_struct **rglist, struct vm_rg_struct *rgnode)
{
  rgnode->rg_next = *rglist;
  *rglist = rgnode;

  return 0;
}

int enlist_pgn_node(struct pgn_t **plist, addr_t pgn)
{
  struct pgn_t *pnode = malloc(sizeof(struct pgn_t));

  pnode->pgn = pgn;
  pnode->pg_next = *plist;
  *plist = pnode;

  return 0;
}

int print_list_fp(struct framephy_struct *ifp)
{
  struct framephy_struct *fp = ifp;

  printf("print_list_fp: ");
  if (fp == NULL) { printf("NULL list\n"); return -1;}
  printf("\n");
  while (fp != NULL)
  {
    printf("fp[" FORMAT_ADDR "]\n", fp->fpn);
    fp = fp->fp_next;
  }
  printf("\n");
  return 0;
}

int print_list_rg(struct vm_rg_struct *irg)
{
  struct vm_rg_struct *rg = irg;

  printf("print_list_rg: ");
  if (rg == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (rg != NULL)
  {
    printf("rg[" FORMAT_ADDR "->"  FORMAT_ADDR "]\n", rg->rg_start, rg->rg_end);
    rg = rg->rg_next;
  }
  printf("\n");
  return 0;
}

int print_list_vma(struct vm_area_struct *ivma)
{
  struct vm_area_struct *vma = ivma;

  printf("print_list_vma: ");
  if (vma == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (vma != NULL)
  {
    printf("va[" FORMAT_ADDR "->" FORMAT_ADDR "]\n", vma->vm_start, vma->vm_end);
    vma = vma->vm_next;
  }
  printf("\n");
  return 0;
}

int print_list_pgn(struct pgn_t *ip)
{
  printf("print_list_pgn: ");
  if (ip == NULL) { printf("NULL list\n"); return -1; }
  printf("\n");
  while (ip != NULL)
  {
    printf("va[" FORMAT_ADDR "]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("\n");
  return 0;
}

static void print_pgtbl_range(struct pcb_t *caller, struct mm_struct *mm,
                              addr_t start, addr_t end)
{
  addr_t pgn_start = start >> PAGING64_ADDR_PT_SHIFT;
  addr_t pgn_end = (end + PAGING64_PAGESZ - 1) >> PAGING64_ADDR_PT_SHIFT;
  addr_t pgit;

  if (pgn_start >= pgn_end)
    return;

  /* Refresh the traversed 5-level directory caches on the first mapped page */
  pte_slot(mm, pgn_start, 0);

  printf(" PDG=%p P4g=%p PUD=%p PMD=%p\n",
         (void *)mm->pgd, (void *)mm->p4d, (void *)mm->pud, (void *)mm->pmd);

  for (pgit = pgn_start; pgit < pgn_end; pgit++)
  {
    addr_t pte = pte_get_entry(mm, pgit, caller);
    if (pte == 0)
      continue; /* skip holes in the sparse table */

    if (PAGING_PAGE_PRESENT(pte))
      printf("  PT[%03lx] page=%lu -> frame=%u (present)\n",
             (unsigned long)(pgit & 0x1ff), (unsigned long)pgit,
             (unsigned int)PAGING_FPN(pte));
    else
      printf("  PT[%03lx] page=%lu -> swapoff=%u (swapped)\n",
             (unsigned long)(pgit & 0x1ff), (unsigned long)pgit,
             (unsigned int)PAGING_SWP(pte));
  }
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
  struct mm_struct *mm = caller->mm;
  addr_t pgd = 0, p4d = 0, pud = 0, pmd = 0, pt = 0;

  if (mm == NULL || mm->mmap == NULL)
    return -1;

  /* Default dump (start=0, end=-1): dump ONLY active ranges [vm_start, sbrk)
   * across all macOS-style VMAs (Small/Medium/Large), avoiding scanning the
   * huge unmapped gap below MM64_SMALL_START (0x10000000).
   */
  if (start == 0 && end == (addr_t)-1)
  {
    struct vm_area_struct *vma = mm->mmap;
    int printed = 0;

    while (vma != NULL)
    {
      if (vma->sbrk > vma->vm_start)
      {
        const char *zone = (vma->vm_id == MM64_VMA_SMALL_ID)  ? "SMALL"  :
                           (vma->vm_id == MM64_VMA_MEDIUM_ID) ? "MEDIUM" :
                           (vma->vm_id == MM64_VMA_LARGE_ID)  ? "LARGE"  : "VMA";
        printf(" [VMA %lu - %s: 0x%lx .. 0x%lx]\n",
               vma->vm_id, zone,
               (unsigned long)vma->vm_start, (unsigned long)vma->sbrk);
        print_pgtbl_range(caller, mm, vma->vm_start, vma->sbrk);
        printed = 1;
      }
      vma = vma->vm_next;
    }

    if (!printed)
    {
      printf(" PDG=%p P4g=%p PUD=%p PMD=%p\n",
             (void *)mm->pgd, (void *)mm->p4d, (void *)mm->pud, (void *)mm->pmd);
    }
    return 0;
  }

  get_pd_from_address(start, &pgd, &p4d, &pud, &pmd, &pt);
  if (end == (addr_t)-1)
    end = mm->mmap->sbrk;

  print_pgtbl_range(caller, mm, start, end);
  return 0;
}

#endif  //def MM64
