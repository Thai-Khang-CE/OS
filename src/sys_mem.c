/* src/sys_mem.c */
#include "os-mm.h"
#include "syscall.h"
#include "libmem.h"
#include "queue.h"
#include <stdlib.h>
#include <stdio.h>

#ifdef MM64
#include "mm64.h"
#else
#include "mm.h"
#endif

/* Hàm tìm PCB trong một hàng đợi cụ thể */
struct pcb_t *check_proc_in_queue(struct queue_t *q, uint32_t pid) {
    if (q == NULL) return NULL;
    int i;
    for (i = 0; i < q->size; i++) {
        if (q->proc[i] && q->proc[i]->pid == pid) {
            return q->proc[i];
        }
    }
    return NULL;
}

/* Hàm tìm PCB trong toàn bộ hệ thống */
struct pcb_t *get_proc_by_id(struct krnl_t *krnl, uint32_t pid) {
    struct pcb_t *proc = NULL;

    /* 1. Tìm trong Running List (Quan trọng nhất vì process đang chạy syscall) */
    /* Lưu ý: Tùy implementation mà running_list là con trỏ hoặc struct. 
       Ta check cả 2 trường hợp an toàn */
#ifdef MLQ_SCHED
    proc = check_proc_in_queue(&krnl->running_list, pid);
    if (proc) return proc;
    
    /* 2. Tìm trong MLQ Ready Queues */
    if (krnl->mlq_ready_queue) {
        for (int i = 0; i < MAX_PRIO; i++) {
            proc = check_proc_in_queue(&krnl->mlq_ready_queue[i], pid);
            if (proc) return proc;
        }
    }
#else
    proc = check_proc_in_queue(&krnl->ready_queue, pid);
#endif
    
    return proc;
}

int __sys_memmap(struct krnl_t *krnl, uint32_t pid, struct sc_regs* regs)
{
   int memop = regs->a1;
   BYTE value;
   
   /* Tìm PCB thật sự */
   struct pcb_t *caller = get_proc_by_id(krnl, pid);

   if (!caller) {
       return -1; // Không tìm thấy process, hủy syscall
   }

   switch (memop) {
   case SYSMEM_MAP_OP:
           vmap_pgd_memset(caller, regs->a2, regs->a3);
           break;
   case SYSMEM_INC_OP:
           inc_vma_limit(caller, regs->a2, regs->a3);
           break;
   case SYSMEM_SWP_OP:
           __mm_swap_page(caller, regs->a2, regs->a3);
           break;
   case SYSMEM_IO_READ:
           MEMPHY_read(krnl->mram, regs->a2, &value);
           regs->a3 = (uint32_t)value; 
           break;
   case SYSMEM_IO_WRITE:
           MEMPHY_write(krnl->mram, regs->a2, (BYTE)regs->a3);
           break;
   default:
           break;
   }
   
   return 0;
}