/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * PAGING based Memory Management
 * Memory physical module mm/mm-memphy.c
 */

#include "mm.h"
#include "mm64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 *  MEMPHY_mv_csr - move MEMPHY cursor
 *  @mp: memphy struct
 *  @offset: offset
 */
int MEMPHY_mv_csr(struct memphy_struct *mp, addr_t offset)
{
   addr_t numstep = 0;

   mp->cursor = 0;
   while (numstep < offset && numstep < mp->maxsz)
   {
      /* Traverse sequentially */
      mp->cursor = (mp->cursor + 1) % mp->maxsz;
      numstep++;
   }

   return 0;
}

/*
 *  MEMPHY_seq_read - read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_seq_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
   if (mp == NULL)
      return -1;

   if (!mp->rdmflg)
      return -1; /* Not compatible mode for sequential read */

   MEMPHY_mv_csr(mp, addr);
   *value = (BYTE)mp->storage[addr];

   return 0;
}

/*
 *  MEMPHY_read read MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @value: obtained value
 */
int MEMPHY_read(struct memphy_struct *mp, addr_t addr, BYTE *value)
{
   if (mp == NULL || mp->storage == NULL || value == NULL || addr >= mp->maxsz)
      return -1;

   if (mp->rdmflg)
      *value = mp->storage[addr];
   else /* Sequential access device */
      return MEMPHY_seq_read(mp, addr, value);

   return 0;
}

/*
 *  MEMPHY_seq_write - write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_seq_write(struct memphy_struct *mp, addr_t addr, BYTE value)
{

   if (mp == NULL)
      return -1;

   if (!mp->rdmflg)
      return -1; /* Not compatible mode for sequential read */

   MEMPHY_mv_csr(mp, addr);
   mp->storage[addr] = value;

   return 0;
}

/*
 *  MEMPHY_write-write MEMPHY device
 *  @mp: memphy struct
 *  @addr: address
 *  @data: written data
 */
int MEMPHY_write(struct memphy_struct *mp, addr_t addr, BYTE data)
{
   if (mp == NULL || mp->storage == NULL || addr >= mp->maxsz)
      return -1;

   if (mp->rdmflg)
      mp->storage[addr] = data;
   else /* Sequential access device */
      return MEMPHY_seq_write(mp, addr, data);

   return 0;
}

/*
 *  MEMPHY_format-format MEMPHY device
 *  @mp: memphy struct
 */
int MEMPHY_format(struct memphy_struct *mp, int pagesz)
{
   /* This setting come with fixed constant PAGESZ */
   addr_t numfp = mp->maxsz / pagesz;
   struct framephy_struct *newfst, *fst;
   addr_t iter = 0;

   /* Always leave the lists in a sane state */
   mp->free_fp_list = NULL;
   mp->used_fp_list = NULL;

   if (numfp == 0)
      return -1;

   /* Init head of free framephy list */
   fst = malloc(sizeof(struct framephy_struct));
   fst->fpn = iter;
   fst->fp_next = NULL;
   mp->free_fp_list = fst;

   /* We have list with first element, fill in the rest num-1 element member*/
   for (iter = 1; iter < numfp; iter++)
   {
      newfst = malloc(sizeof(struct framephy_struct));
      newfst->fpn = iter;
      newfst->fp_next = NULL;
      fst->fp_next = newfst;
      fst = newfst;
   }

   return 0;
}

int MEMPHY_get_freefp(struct memphy_struct *mp, addr_t *retfpn)
{
   struct framephy_struct *fp = mp->free_fp_list;

   if (fp == NULL)
      return -1;

   /*
    * TODO manage free frame list
    */
   *retfpn = fp->fpn;
   /* cap nhat lai danh sach free frame */
   mp->free_fp_list = fp->fp_next;

   /*
    * TODO MEMPHY is iteratively used up until its exhausted
    * No garbage collector acting then it not been released
    */
   free(fp);

   return 0;
}

/*
 * MEMPHY_get_contiguous_freefp - grab numfp PHYSICALLY CONTIGUOUS frames
 * @mp      : memphy struct
 * @numfp   : number of requested contiguous frames
 * @retfpn  : returned first frame number of the run
 *
 * Needed by kmalloc(): kernel memory must be physically contiguous.
 * We scan the free frame list for a run of consecutive frame numbers.
 */
int MEMPHY_get_contiguous_freefp(struct memphy_struct *mp, int numfp, addr_t *retfpn)
{
   struct framephy_struct *it;
   int maxfp, i, run;

   if (mp == NULL || mp->free_fp_list == NULL || numfp <= 0)
      return -1;

#ifdef MM64
   maxfp = mp->maxsz / PAGING64_PAGESZ;
#else
   maxfp = mp->maxsz / PAGING_PAGESZ;
#endif
   if (maxfp <= 0)
      return -1;

   /* Build a presence bitmap of the free list */
   char *freemap = (char *)calloc(maxfp, sizeof(char));
   if (freemap == NULL)
      return -1;

   for (it = mp->free_fp_list; it != NULL; it = it->fp_next)
      if (it->fpn < (addr_t)maxfp)
         freemap[it->fpn] = 1;

   /* Find the first run of numfp consecutive free frames */
   run = 0;
   for (i = 0; i < maxfp; i++)
   {
      run = freemap[i] ? run + 1 : 0;
      if (run == numfp)
      {
         addr_t start = i - numfp + 1;
         addr_t f;

         /* Detach the frames of the run from the free list */
         for (f = start; f <= (addr_t)i; f++)
         {
            struct framephy_struct *prev = NULL, *cur = mp->free_fp_list;
            while (cur != NULL && cur->fpn != f)
            {
               prev = cur;
               cur = cur->fp_next;
            }
            if (cur != NULL)
            {
               if (prev == NULL)
                  mp->free_fp_list = cur->fp_next;
               else
                  prev->fp_next = cur->fp_next;
               free(cur);
            }
         }

         *retfpn = start;
         free(freemap);
         return 0;
      }
   }

   free(freemap);
   return -1; /* No contiguous block large enough */
}

int MEMPHY_dump(struct memphy_struct *mp)
{
  /*
   * TODO dump memphy contnt mp->storage
   *     for tracing the memory content
   */
   addr_t i;
   int shown = 0;

   if (mp == NULL || mp->storage == NULL)
      return -1;

   printf("===== PHYSICAL MEMORY DUMP =====\n");
   for (i = 0; i < mp->maxsz; i++)
   {
      if (mp->storage[i] != 0)
      {
         printf("BYTE %08lx: %d\n", (unsigned long)i, mp->storage[i]);
         shown++;
      }
   }
   printf("===== PHYSICAL MEMORY END-DUMP =====\n");
   if (shown == 0)
      printf("(all bytes are zero)\n");

   return 0;
}

int MEMPHY_put_freefp(struct memphy_struct *mp, addr_t fpn)
{
   struct framephy_struct *fp = mp->free_fp_list;
   struct framephy_struct *newnode = malloc(sizeof(struct framephy_struct));

   /* Create new node with value fpn */
   newnode->fpn = fpn;
   newnode->fp_next = fp;
   mp->free_fp_list = newnode;

   return 0;
}

/*
 *  Init MEMPHY struct
 */
int init_memphy(struct memphy_struct *mp, addr_t max_size, int randomflg)
{
   mp->storage = (BYTE *)malloc(max_size * sizeof(BYTE));
   mp->maxsz = max_size;
   mp->free_fp_list = NULL;
   mp->used_fp_list = NULL;
   mp->cursor = 0;
   memset(mp->storage, 0, max_size * sizeof(BYTE));

#ifdef MM64
   MEMPHY_format(mp, PAGING64_PAGESZ);
#else
   MEMPHY_format(mp, PAGING_PAGESZ);
#endif

   mp->rdmflg = (randomflg != 0) ? 1 : 0;

   if (!mp->rdmflg) /* Not Ramdom acess device, then it serial device*/
      mp->cursor = 0;

   return 0;
}

// #endif
