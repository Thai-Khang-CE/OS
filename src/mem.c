#include "mem.h"
#include "stdlib.h"
#include "string.h"
#include <pthread.h>
#include <stdio.h>

static BYTE _ram[RAM_SIZE];

static struct {
    uint32_t proc;  // ID of process currently uses this page
    int index;  // Index of the page in the list of pages allocated
            // to the process.
    int next;   // The next page in the list. -1 if it is the last
            // page.
} _mem_stat [NUM_PAGES];

static pthread_mutex_t mem_lock;

void init_mem(void) {
    memset(_mem_stat, 0, sizeof(*_mem_stat) * NUM_PAGES);
    memset(_ram, 0, sizeof(BYTE) * RAM_SIZE);
    pthread_mutex_init(&mem_lock, NULL);
}

/* get offset of the virtual address */
static addr_t get_offset(addr_t addr) {
    return addr & ~((~0U) << OFFSET_LEN);
}

/* get the first layer index */
static addr_t get_first_lv(addr_t addr) {
    return addr >> (OFFSET_LEN + PAGE_LEN);
}

/* get the second layer index */
static addr_t get_second_lv(addr_t addr) {
    return (addr >> OFFSET_LEN) - (get_first_lv(addr) << PAGE_LEN);
}

/* Search for page table table from the a segment table */
static struct trans_table_t * get_trans_table(
        addr_t index,   // Segment level index
        struct page_table_t * page_table) { // first level table
    
    /* Iterate through the segment table to find the segment entry */
    int i;
    for (i = 0; i < page_table->size; i++) {
        if (page_table->table[i].v_index == index) {
            return page_table->table[i].next_lv;
        }
    }
    return NULL;
}

/* Translate virtual address to physical address. If [virtual_addr] is valid,
 * return 1 and write its physical counterpart to [physical_addr].
 * Otherwise, return 0 */
static int translate(
        addr_t virtual_addr,    // Given virtual address
        addr_t * physical_addr, // Physical address to be returned
        struct pcb_t * proc) {  // Process uses given virtual address

    /* Offset of the virtual address */
    addr_t offset = get_offset(virtual_addr);
    
    /* The first layer index */
    addr_t first_lv = get_first_lv(virtual_addr);
    /* The second layer index */
    addr_t second_lv = get_second_lv(virtual_addr);
    
    /* Search in the first level */
    struct trans_table_t * trans_table = NULL;
    trans_table = get_trans_table(first_lv, proc->page_table);
    if (trans_table == NULL) {
        return 0;
    }

    int i;
    for (i = 0; i < trans_table->size; i++) {
        if (trans_table->table[i].v_index == second_lv) {
            /* Found the page entry, calculate physical address */
            /* Physical Address = (Frame Index * Page Size) + Offset */
            *physical_addr = (trans_table->table[i].p_index << OFFSET_LEN) + offset;
            return 1;
        }
    }
    return 0;   
}

addr_t alloc_mem(uint32_t size, struct pcb_t * proc) {
    pthread_mutex_lock(&mem_lock);
    addr_t ret_mem = 0;
    
    /* Calculate number of pages required */
    uint32_t num_pages = (size % PAGE_SIZE) ? size / PAGE_SIZE + 1 :
        size / PAGE_SIZE; 
    
    int mem_avail = 0; 
    int free_pages_count = 0;

    /* 1. Check physical memory availability */
    for (int i = 0; i < NUM_PAGES; i++) {
        if (_mem_stat[i].proc == 0) {
            free_pages_count++;
        }
    }

    /* 2. Check virtual memory availability (Process break pointer limit) */
    /* Assuming simplistic check: just physical availability + 
       standard address space limits (not explicitly defined here but implied) */
    if (free_pages_count >= num_pages && 
        proc->bp + num_pages * PAGE_SIZE < (1 << (OFFSET_LEN + PAGE_LEN + SEGMENT_LEN))) {
        mem_avail = 1;
    }

    if (mem_avail) {
        /* We could allocate new memory region to the process */
        ret_mem = proc->bp;
        int pages_allocated = 0;
        int last_allocated_idx = -1;
        
        for (int i = 0; i < num_pages; i++) {
            /* Find a free physical frame */
            int phy_idx = -1;
            for (int j = 0; j < NUM_PAGES; j++) {
                if (_mem_stat[j].proc == 0) {
                    phy_idx = j;
                    break;
                }
            }

            if (phy_idx != -1) {
                /* Update _mem_stat */
                _mem_stat[phy_idx].proc = proc->pid;
                _mem_stat[phy_idx].index = i;
                if (last_allocated_idx != -1) {
                    _mem_stat[last_allocated_idx].next = phy_idx;
                }
                _mem_stat[phy_idx].next = -1; // Default to last
                last_allocated_idx = phy_idx;

                /* Update Virtual Memory Map (Page Table) */
                addr_t v_addr = proc->bp;
                addr_t seg_idx = get_first_lv(v_addr);
                addr_t pg_idx = get_second_lv(v_addr);

                /* Get or Create Segment Entry (Level 1) */
                struct trans_table_t *trans_tbl = get_trans_table(seg_idx, proc->page_table);
                if (trans_tbl == NULL) {
                    /* Create new segment entry */
                    int idx = proc->page_table->size;
                    proc->page_table->table[idx].v_index = seg_idx;
                    
                    /* Allocate Level 2 table */
                    proc->page_table->table[idx].next_lv = 
                        (struct trans_table_t *) malloc(sizeof(struct trans_table_t));
                    proc->page_table->table[idx].next_lv->size = 0;
                    
                    proc->page_table->size++;
                    trans_tbl = proc->page_table->table[idx].next_lv;
                }

                /* Create Page Entry (Level 2) */
                int p_idx = trans_tbl->size;
                trans_tbl->table[p_idx].v_index = pg_idx;
                trans_tbl->table[p_idx].p_index = phy_idx;
                trans_tbl->size++;

                /* Move Break Pointer forward for next page */
                proc->bp += PAGE_SIZE;
            }
        }
    }
    
    pthread_mutex_unlock(&mem_lock);
    return ret_mem;
}

int free_mem(addr_t address, struct pcb_t * proc) {
    pthread_mutex_lock(&mem_lock);
    
    addr_t physical_addr;
    
    /* Translate virtual address to physical address to find the starting frame */
    if (translate(address, &physical_addr, proc)) {
        int phy_idx = physical_addr >> OFFSET_LEN; // Get Frame Number (FPN)
        
        /* Verify ownership before freeing */
        if (_mem_stat[phy_idx].proc == proc->pid) {
            int curr_idx = phy_idx;
            
            /* Traverse the linked list in _mem_stat to free all associated pages */
            while (curr_idx != -1) {
                int next_idx = _mem_stat[curr_idx].next;
                
                /* Reset status */
                _mem_stat[curr_idx].proc = 0;
                _mem_stat[curr_idx].index = -1;
                _mem_stat[curr_idx].next = -1;
                
                curr_idx = next_idx;
            }
        }
    }

    pthread_mutex_unlock(&mem_lock);
    return 0;
}

int read_mem(addr_t address, struct pcb_t * proc, BYTE * data) {
    addr_t physical_addr;
    if (translate(address, &physical_addr, proc)) {
        *data = _ram[physical_addr];
        return 0;
    }else{
        return 1;
    }
}

int write_mem(addr_t address, struct pcb_t * proc, BYTE data) {
    addr_t physical_addr;
    if (translate(address, &physical_addr, proc)) {
        _ram[physical_addr] = data;
        return 0;
    }else{
        return 1;
    }
}

void dump(void) {
    int i;
    for (i = 0; i < NUM_PAGES; i++) {
        if (_mem_stat[i].proc != 0) {
            printf("%03d: ", i);
            printf("%05x-%05x - PID: %02d (idx %03d, nxt: %03d)\n",
                i << OFFSET_LEN,
                ((i + 1) << OFFSET_LEN) - 1,
                _mem_stat[i].proc,
                _mem_stat[i].index,
                _mem_stat[i].next
            );
            int j;
            for (   j = i << OFFSET_LEN;
                j < ((i+1) << OFFSET_LEN) - 1;
                j++) {
                
                if (_ram[j] != 0) {
                    printf("\t%05x: %02x\n", j, _ram[j]);
                }
                    
            }
        }
    }
}