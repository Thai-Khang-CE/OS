/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
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
#include "mm.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include "mm64.h"

/* Macro to check overlap of two ranges */
#define OVERLAP(s1, e1, s2, e2) ((s1 < e2) && (s2 < e1))

/*get_vma_by_num - get vm area by numID
 *@mm: memory region
 *@vmaid: ID vm area to alloc memory region
 *
 */
struct vm_area_struct *get_vma_by_num(struct mm_struct *mm, int vmaid)
{
  struct vm_area_struct *pvma = mm->mmap;

  if (mm->mmap == NULL)
    return NULL;

  int vmait = pvma->vm_id;

  while (vmait < vmaid)
  {
    if (pvma == NULL)
      return NULL;

    pvma = pvma->vm_next;
    vmait = pvma->vm_id;
  }

  return pvma;
}

int __mm_swap_page(struct pcb_t *caller, addr_t vicfpn , addr_t swpfpn)
{
    __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, swpfpn);
    return 0;
}

/*get_vm_area_node - get vm area for a number of pages
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 *@vmastart: vma end
 *@vmaend: vma end
 *
 */
struct vm_rg_struct *get_vm_area_node_at_brk(struct pcb_t *caller, int vmaid, addr_t size, addr_t alignedsz)
{
  struct vm_rg_struct * newrg;
 struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  newrg = malloc(sizeof(struct vm_rg_struct));

  /* TODO: update the newrg boundary
  // newrg->rg_start = ...
  // newrg->rg_end = ...
  */
  
  // The new region starts at the current break pointer (sbrk)
  newrg->rg_start = cur_vma->sbrk;
  
  // The region ends at sbrk + requested size
  newrg->rg_end = cur_vma->sbrk + size;

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
  /* TODO validate the planned memory area is not overlapped */
  if (vmastart >= vmaend)
  {
    return -1; // Invalid range
  }

 struct vm_area_struct *vma = caller->mm->mmap;
  if (vma == NULL)
  {
    return 0; // No VMAs to overlap with (though unusual if caller exists)
  }

struct vm_area_struct *cur_area = get_vma_by_num(caller->mm, vmaid);  
if (cur_area == NULL)
  {
    return -1;
  }

  /* Iterate through all VM areas to check for overlap */
  while (vma != NULL)
  {
    // Skip the current area being extended (we only care if it hits OTHERS)
    if (vma != cur_area) 
    {
        // Check intersection: [vmastart, vmaend) vs [vma->vm_start, vma->vm_end)
        if (OVERLAP(vmastart, vmaend, vma->vm_start, vma->vm_end))
        {
            return -1; // Overlap detected
        }
    }
    vma = vma->vm_next;
  }

  return 0;
}

/*inc_vma_limit - increase vm area limits to reserve space for new variable
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@inc_sz: increment size
 *
 */
/* src/mm-vm.c */
/* ... include giữ nguyên ... */

int inc_vma_limit(struct pcb_t *caller, int vmaid, addr_t inc_sz)
{
  struct vm_rg_struct * newrg = malloc(sizeof(struct vm_rg_struct));
  int inc_amt;
  int incnumpage;
  /* FIX: Dùng caller->mm */
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  int old_end;

  if (cur_vma == NULL) {
      free(newrg);
      return -1;
  }

  old_end = cur_vma->vm_end;

#ifdef MM64
  /* Logic 64-bit: inc_sz đã là số bytes (được tính ở libmem) */
  inc_amt = inc_sz;
  incnumpage = inc_amt / PAGING64_PAGESZ;
#else
  /* Logic cũ */
  inc_amt = inc_sz;
  incnumpage = inc_amt / PAGING_PAGESZ;
#endif

  /* Validate overlap */
  if (validate_overlap_vm_area(caller, vmaid, old_end, old_end + inc_amt) < 0)
  {
    free(newrg);
    return -1; 
  }

  /* Map memory */
  if (vm_map_ram(caller, cur_vma->vm_start, cur_vma->vm_end + inc_amt, 
                 old_end, incnumpage, newrg) < 0)
  {
    free(newrg);
    return -1; 
  }

  cur_vma->vm_end += inc_amt;
  cur_vma->sbrk += inc_amt; // Cập nhật sbrk

  free(newrg);
  return 0;
}

// #endif