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
#include <string.h> /* QUAN TRỌNG: Để dùng memset */

#if defined(MM64)

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
 * get_pd_from_address - Parse address to 5 page directory level
 */
int get_pd_from_address(addr_t addr, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
  /* Extract page directories using mask and shift defined in mm64.h */
  *pgd = (addr & PAGING64_ADDR_PGD_MASK) >> PAGING64_ADDR_PGD_LOBIT;
  *p4d = (addr & PAGING64_ADDR_P4D_MASK) >> PAGING64_ADDR_P4D_LOBIT;
  *pud = (addr & PAGING64_ADDR_PUD_MASK) >> PAGING64_ADDR_PUD_LOBIT;
  *pmd = (addr & PAGING64_ADDR_PMD_MASK) >> PAGING64_ADDR_PMD_LOBIT;
  *pt  = (addr & PAGING64_ADDR_PT_MASK)  >> PAGING64_ADDR_PT_LOBIT;

  return 0;
}

/*
 * get_pd_from_pagenum - Parse page number to 5 page directory level
 */
int get_pd_from_pagenum(addr_t pgn, addr_t* pgd, addr_t* p4d, addr_t* pud, addr_t* pmd, addr_t* pt)
{
  /* Shift the address to get page num and perform the mapping*/
  return get_pd_from_address(pgn << PAGING64_ADDR_PT_LOBIT, 
                             pgd, p4d, pud, pmd, pt);
}

/* Helper function to traverse/create page table hierarchy */
/* Returns a pointer to the PTE entry in the final PT table */
addr_t *__get_pte_ptr(struct mm_struct *mm, int pgn, int alloc_mode) {
    addr_t pgd_idx, p4d_idx, pud_idx, pmd_idx, pt_idx;
    
    get_pd_from_pagenum(pgn, &pgd_idx, &p4d_idx, &pud_idx, &pmd_idx, &pt_idx);

    // Level 5: PGD
    if (mm->pgd == NULL) return NULL;
    uint64_t *p4d_base = (uint64_t *)mm->pgd[pgd_idx];
    
    if (p4d_base == NULL) {
        if (!alloc_mode) return NULL;
        p4d_base = malloc(PAGING64_P4D_SZ * sizeof(uint64_t));
        /* FIX: Phải memset về 0 ngay lập tức */
        memset(p4d_base, 0, PAGING64_P4D_SZ * sizeof(uint64_t));
        mm->pgd[pgd_idx] = (uint64_t)p4d_base;
    }

    // Level 4: P4D
    uint64_t *pud_base = (uint64_t *)p4d_base[p4d_idx];
    if (pud_base == NULL) {
        if (!alloc_mode) return NULL;
        pud_base = malloc(PAGING64_PUD_SZ * sizeof(uint64_t));
        /* FIX: Phải memset về 0 ngay lập tức */
        memset(pud_base, 0, PAGING64_PUD_SZ * sizeof(uint64_t));
        p4d_base[p4d_idx] = (uint64_t)pud_base;
    }

    // Level 3: PUD
    uint64_t *pmd_base = (uint64_t *)pud_base[pud_idx];
    if (pmd_base == NULL) {
        if (!alloc_mode) return NULL;
        pmd_base = malloc(PAGING64_PMD_SZ * sizeof(uint64_t));
        /* FIX: Phải memset về 0 ngay lập tức */
        memset(pmd_base, 0, PAGING64_PMD_SZ * sizeof(uint64_t));
        pud_base[pud_idx] = (uint64_t)pmd_base;
    }

    // Level 2: PMD
    uint64_t *pt_base = (uint64_t *)pmd_base[pmd_idx];
    if (pt_base == NULL) {
        if (!alloc_mode) return NULL;
        pt_base = malloc(PAGING64_PT_SZ * sizeof(uint64_t));
        /* FIX: Phải memset về 0 ngay lập tức */
        memset(pt_base, 0, PAGING64_PT_SZ * sizeof(uint64_t));
        pmd_base[pmd_idx] = (uint64_t)pt_base;
    }

    // Level 1: PT
    return (addr_t *)&pt_base[pt_idx];
}


/*
 * pte_set_swap - Set PTE entry for swapped page
 */
int pte_set_swap(struct pcb_t *caller, addr_t pgn, int swptyp, addr_t swpoff)
{
  struct mm_struct *mm = caller->mm;
  addr_t *pte;

  if (mm == NULL) return -1;

  /* Get the real PTE pointer from the table using helper */
  pte = __get_pte_ptr(mm, pgn, 1); 
  if (pte == NULL) return -1; 

  /* Set bit masks */
  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  SETBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, swptyp, PAGING_PTE_SWPTYP_MASK, PAGING_PTE_SWPTYP_LOBIT);
  SETVAL(*pte, swpoff, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);

  return 0;
}

/*
 * pte_set_fpn - Set PTE entry for on-line page
 */
int pte_set_fpn(struct pcb_t *caller, addr_t pgn, addr_t fpn)
{
  struct mm_struct *mm = caller->mm;
  addr_t *pte;

  if (mm == NULL) return -1;

  /* Get the real PTE pointer from the table */
  pte = __get_pte_ptr(mm, pgn, 1);
  if (pte == NULL) return -1;

  SETBIT(*pte, PAGING_PTE_PRESENT_MASK);
  CLRBIT(*pte, PAGING_PTE_SWAPPED_MASK);

  SETVAL(*pte, fpn, PAGING_PTE_FPN_MASK, PAGING_PTE_FPN_LOBIT);

  return 0;
}


/* Get PTE page table entry */
uint32_t pte_get_entry(struct pcb_t *caller, addr_t pgn)
{
  struct mm_struct *mm = caller->mm;
  addr_t *pte_ptr;
  
  if (mm == NULL) return 0;

  /* Perform multi-level page mapping (no alloc) */
  pte_ptr = __get_pte_ptr(mm, pgn, 0); 
  
  if (pte_ptr == NULL) return 0; 

  return (uint32_t)(*pte_ptr); 
}

/* Set PTE page table entry */
int pte_set_entry(struct pcb_t *caller, addr_t pgn, uint32_t pte_val)
{
  struct mm_struct *mm = caller->mm;
  addr_t *pte_ptr;

  if (mm == NULL) return -1;

  pte_ptr = __get_pte_ptr(mm, pgn, 1);
  if (pte_ptr) {
      *pte_ptr = pte_val;
      return 0;
  }
  return -1;
}


/*
 * vmap_pgd_memset - map a range of page at aligned address
 */
int vmap_pgd_memset(struct pcb_t *caller, 
                    addr_t addr, 
                    int pgnum) 
{
  int i;
  addr_t pgn_start = PAGING_PGN(addr);
  struct mm_struct *mm = caller->mm;

  if (mm == NULL) return -1;

  for (i = 0; i < pgnum; i++) {
      // Just ensure the path exists (allocate intermediate tables)
      __get_pte_ptr(mm, pgn_start + i, 1); 
  }
  return 0;
}

/*
 * vmap_page_range - map a range of page at aligned address
 */
addr_t vmap_page_range(struct pcb_t *caller, 
                     addr_t addr, 
                     int pgnum, 
                     struct framephy_struct *frames, 
                     struct vm_rg_struct *ret_rg) 
{
  struct framephy_struct *fpit = frames;
  int pgit = 0;
  addr_t pgn;
  addr_t fpn;

  /* Update the rg_end and rg_start of ret_rg */
  ret_rg->rg_start = addr;
  /* FIX: Dùng PAGING64_PAGESZ */
  ret_rg->rg_end = addr + pgnum * PAGING64_PAGESZ;

  pgn = PAGING_PGN(addr);
  
  for (pgit = 0; pgit < pgnum; pgit++) {
      if (fpit == NULL) break; 

      fpn = fpit->fpn;
      pte_set_fpn(caller, pgn + pgit, fpn);

      if (caller->mm)
          enlist_pgn_node(&caller->mm->fifo_pgn, pgn + pgit);

      fpit = fpit->fp_next;
  }

  return 0;
}

/*
 * alloc_pages_range - allocate req_pgnum of frame in ram
 */
addr_t alloc_pages_range(struct pcb_t *caller, int req_pgnum, struct framephy_struct **frm_lst)
{
  int pgit;
  addr_t fpn;
  struct framephy_struct *newfp_str;
  
  *frm_lst = NULL;
  struct framephy_struct *tail = NULL;

  for (pgit = 0; pgit < req_pgnum; pgit++)
  {
    newfp_str = malloc(sizeof(struct framephy_struct));
    
    // NOTE: mram is shared via kernel, so caller->krnl->mram is correct here
    if (MEMPHY_get_freefp(caller->krnl->mram, &fpn) == 0)
    {
      newfp_str->fpn = fpn;
      newfp_str->owner = caller->mm; // Fix: Owner is process MM
      newfp_str->fp_next = NULL;

      if (*frm_lst == NULL) {
          *frm_lst = newfp_str;
          tail = newfp_str;
      } else {
          tail->fp_next = newfp_str;
          tail = newfp_str;
      }
    }
    else
    { 
      return -3000; 
    }
  }

  return 0;
}

/*
 * vm_map_ram - do the mapping all vm are to ram storage device
 */
addr_t vm_map_ram(struct pcb_t *caller, addr_t astart, addr_t aend, addr_t mapstart, int incpgnum, struct vm_rg_struct *ret_rg)
{
  struct framephy_struct *frm_lst = NULL;
  addr_t ret_alloc = 0;

  ret_alloc = alloc_pages_range(caller, incpgnum, &frm_lst);

  if (ret_alloc == -3000) return -1; 

  vmap_page_range(caller, mapstart, incpgnum, frm_lst, ret_rg);

  return 0;
}

/* Swap copy content page from source frame to destination frame */
int __swap_cp_page(struct memphy_struct *mpsrc, addr_t srcfpn,
                   struct memphy_struct *mpdst, addr_t dstfpn)
{
 int cellidx;
  addr_t addrsrc, addrdst;
  /* FIX: Dùng PAGING64_PAGESZ */
  for (cellidx = 0; cellidx < PAGING64_PAGESZ; cellidx++)
  {
    /* FIX: Dùng PAGING64_PAGESZ */
    addrsrc = srcfpn * PAGING64_PAGESZ + cellidx;
    addrdst = dstfpn * PAGING64_PAGESZ + cellidx;

    BYTE data;
    MEMPHY_read(mpsrc, addrsrc, &data);
    MEMPHY_write(mpdst, addrdst, data);
  }

  return 0;
}

/*
 * Initialize a empty Memory Management instance
 */
int init_mm(struct mm_struct *mm, struct pcb_t *caller)
{
  struct vm_area_struct *vma0 = malloc(sizeof(struct vm_area_struct));

  /* Init page table directory - 64 bit PGD */
  mm->pgd = malloc(PAGING64_PGD_SZ * sizeof(uint64_t));
  /* FIX: Phải memset về 0 */
  memset(mm->pgd, 0, PAGING64_PGD_SZ * sizeof(uint64_t));
  
  mm->p4d = NULL;
  mm->pud = NULL;
  mm->pmd = NULL;
  mm->pt  = NULL;

  /* By default the owner comes with at least one vma */
  vma0->vm_id = 0;
  vma0->vm_start = 0;
  vma0->vm_end = vma0->vm_start;
  vma0->sbrk = vma0->vm_start;
  struct vm_rg_struct *first_rg = init_vm_rg(vma0->vm_start, vma0->vm_end);
  enlist_vm_rg_node(&vma0->vm_freerg_list, first_rg);

  /* update VMA0 next */
  vma0->vm_next = NULL;

  /* Point vma owner backward */
  vma0->vm_mm = mm; 

  /* update mmap */
  mm->mmap = vma0;
  
  /* Link the process PCB to this specific MM struct */
  if (caller != NULL) {
      caller->mm = mm;
  }

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
    printf("fp[%d]\n", fp->fpn);
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
    printf("rg[%ld->%ld]\n", rg->rg_start, rg->rg_end);
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
    printf("va[%ld->%ld]\n", vma->vm_start, vma->vm_end);
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
    printf("va[%d]-\n", ip->pgn);
    ip = ip->pg_next;
  }
  printf("n");
  return 0;
}

int print_pgtbl(struct pcb_t *caller, addr_t start, addr_t end)
{
  int i, j, k, l;
  /* LƯU Ý: Cần đảm bảo struct pcb_t đã có trường mm (đã sửa ở bước trước) */
  struct mm_struct *mm = caller->mm;

  if (!mm || !mm->pgd) return -1;

  printf("print_pgtbl:\n");

  /* Duyệt qua PGD (Level 5) */
  for (i = 0; i < PAGING64_PGD_SZ; i++) {
      if (mm->pgd[i] != 0) {
          /* Ép kiểu giá trị entry thành con trỏ tới bảng cấp dưới (P4D) */
          uint64_t *p4d = (uint64_t *)mm->pgd[i];
          
          /* Duyệt qua P4D (Level 4) */
          for (j = 0; j < PAGING64_P4D_SZ; j++) {
              if (p4d[j] != 0) {
                  uint64_t *pud = (uint64_t *)p4d[j];

                  /* Duyệt qua PUD (Level 3) */
                  for (k = 0; k < PAGING64_PUD_SZ; k++) {
                      if (pud[k] != 0) {
                          uint64_t *pmd = (uint64_t *)pud[k];

                          /* Duyệt qua PMD (Level 2) */
                          for (l = 0; l < PAGING64_PMD_SZ; l++) {
                              if (pmd[l] != 0) {
                                  /* In ra địa chỉ các bảng theo format đề bài:
                                   * mm->pgd[i] : Địa chỉ bảng P4D
                                   * p4d[j]     : Địa chỉ bảng PUD
                                   * pud[k]     : Địa chỉ bảng PMD
                                   * pmd[l]     : Địa chỉ bảng PT
                                   */
                                  printf(" PDG=%016lx P4g=%016lx PUD=%016lx PMD=%016lx\n",
                                         mm->pgd[i], p4d[j], pud[k], pmd[l]);
                              }
                          }
                      }
                  }
              }
          }
      }
  }

  return 0;
}

#endif  //def MM64