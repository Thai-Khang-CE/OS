/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* LamiaAtrium release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;

/* enlist_vm_freerg_list - add new rg to freerg_list */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;

  if (rg_elmt->rg_start >= rg_elmt->rg_end)
    return -1;

  if (rg_node != NULL)
    rg_elmt->rg_next = rg_node;

  mm->mmap->vm_freerg_list = rg_elmt;
  return 0;
}

/* get_symrg_byid - get mem region by region ID */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/* __alloc - allocate a region memory */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  
  /* FIX: Dùng caller->mm */
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  int inc_sz = 0;

  /* 1. Thử tái sử dụng vùng nhớ cũ */
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;
 
    *alloc_addr = rgnode.rg_start;
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }

  /* 2. Nếu không có vùng trống, mở rộng Heap (sbrk) */
#ifdef MM64
  /* Tính số trang cần tăng (Làm tròn lên) */
  inc_sz = (uint32_t)(size / (int)PAGING64_PAGESZ);
  if (size % PAGING64_PAGESZ != 0) inc_sz++;
  
  /* Tính số byte thực tế cần tăng */
  int inc_amt = inc_sz * PAGING64_PAGESZ;
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
  int inc_amt = inc_sz;
#endif
  
  int old_sbrk = cur_vma->sbrk;

  /* WORKAROUND: Gọi trực tiếp hàm kernel (Bypass Syscall) để tránh lỗi tìm PCB */
  /* Thay vì: syscall(caller->krnl, caller->pid, 17, &regs); */
  inc_vma_limit(caller, vmaid, inc_amt);

  /* Cập nhật Symbol Table */
  caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->mm->symrgtbl[rgid].rg_end = old_sbrk + size;

  *alloc_addr = old_sbrk;

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/* __free - remove a region memory */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);

  if (rgid < 0 || rgid > PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* FIX: Dùng caller->mm */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);

  if (rgnode->rg_start == 0 && rgnode->rg_end == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /* FIX: Dùng caller->mm */
  enlist_vm_freerg_list(caller->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/* liballoc - PAGING-based allocate a region memory */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t  addr;

  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1) return -1;

  /* IN RA MÀN HÌNH: liballoc:dòng */
  printf("%s:%d\n", __func__, __LINE__); 

#ifdef IODUMP
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); 
#endif
#endif

  return val;
}

/* libfree - PAGING-based free a region memory */
int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  int val = __free(proc, 0, reg_index);
  if (val == -1) return -1;

  /* IN RA MÀN HÌNH: libfree:dòng */
  printf("%s:%d\n", __func__, __LINE__);

#ifdef IODUMP
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); 
#endif
#endif
  
  return 0;
}

/* pg_getpage - get the page in ram */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{
  uint32_t pte = pte_get_entry(caller, pgn);

  if (!PAGING_PAGE_PRESENT(pte))
  { 
    addr_t vicpgn, swpfpn, vicfpn;
    uint32_t vicpte;

    if (find_victim_page(caller->mm, &vicpgn) == -1) return -1;

    if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1) return -1;

    vicpte = pte_get_entry(caller, vicpgn);
    vicfpn = PAGING_FPN(vicpte);

    __swap_cp_page(caller->krnl->active_mswp, swpfpn, caller->krnl->mram, vicfpn);

    pte_set_swap(caller, vicpgn, 0, swpfpn);

    if (PAGING_PAGE_SWAPPED(pte)) {
        int old_swpfpn = PAGING_SWP(pte);
        __swap_cp_page(caller->krnl->mram, vicfpn, caller->krnl->active_mswp, old_swpfpn);
        MEMPHY_put_freefp(caller->krnl->active_mswp, old_swpfpn);
    } 

    pte_set_fpn(caller, pgn, vicfpn);
    enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
  }

  *fpn = PAGING_FPN(pte_get_entry(caller,pgn));
  return 0;
}

/* pg_getval - read value at given offset */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0) return -1; 

  /* FIX: Tính địa chỉ vật lý cho 64-bit page size */
#ifdef MM64
  int phyaddr = (fpn * PAGING64_PAGESZ) + off;
#else
  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;
#endif

  MEMPHY_read(caller->krnl->mram, phyaddr, data);
  return 0;
}

/* pg_setval - write value to given offset */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = PAGING_PGN(addr);
  int off = PAGING_OFFST(addr);
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0) return -1; 

#ifdef MM64
  int phyaddr = (fpn * PAGING64_PAGESZ) + off;
#else
  int phyaddr = (fpn << PAGING_ADDR_FPN_LOBIT) + off;
#endif

  MEMPHY_write(caller->krnl->mram, phyaddr, value);
  return 0;
}

/* __read - read value in region memory */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* FIX: Dùng caller->mm */
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  if (currg == NULL) return -1;

  /* FIX: Dùng caller->mm */
  pg_getval(caller->mm, currg->rg_start + offset, data, caller);
  return 0;
}

/* libread - PAGING-based read a region memory */
int libread(struct pcb_t *proc, uint32_t source, addr_t offset, uint32_t* destination)
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);

  *destination = data;

  /* IN RA MÀN HÌNH: libread:dòng */
  printf("%s:%d\n", __func__, __LINE__);

#ifdef IODUMP
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
#endif

  return val;
}

/* __write - write a region memory */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);
  /* FIX: Dùng caller->mm */
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  if (currg == NULL || cur_vma == NULL) {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  /* FIX: Dùng caller->mm */
  pg_setval(caller->mm, currg->rg_start + offset, value, caller);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/* libwrite - PAGING-based write a region memory */
int libwrite(struct pcb_t *proc, BYTE data, uint32_t destination, addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1) return -1;

  /* IN RA MÀN HÌNH: libwrite:dòng */
  printf("%s:%d\n", __func__, __LINE__);

#ifdef IODUMP
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1);
#endif
#endif

  return val;
}

/* free_pcb_memphy - collect all memphy of pcb */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  /* Phần này thường không dùng trong bài tập cơ bản, để trống hoặc implement nếu cần */
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/* find_victim_page - find victim page */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;
  if (!pg) return -1;

  /* FIFO Algorithm: Lấy phần tử cuối cùng trong danh sách */
  struct pgn_t *prev = NULL;
  while (pg->pg_next) {
    prev = pg;
    pg = pg->pg_next;
  }
  
  *retpgn = pg->pgn;
  
  if (prev) prev->pg_next = NULL;
  else mm->fifo_pgn = NULL; 

  free(pg);
  return 0;
}

/* get_free_vmrg_area - get a free vm region */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  /* FIX: Dùng caller->mm */
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL) return -1;

  newrg->rg_start = newrg->rg_end = -1;

  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { 
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      if (rgit->rg_start + size < rgit->rg_end) {
        rgit->rg_start = rgit->rg_start + size;
      }
      else { 
        struct vm_rg_struct *nextrg = rgit->rg_next;
        if (nextrg != NULL) {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;
          rgit->rg_next = nextrg->rg_next;
          free(nextrg);
        }
        else {                                
          rgit->rg_start = rgit->rg_end; 
          rgit->rg_next = NULL;
        }
      }
      return 0;
    }
    rgit = rgit->rg_next;
  }

  return -1;
}