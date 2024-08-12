#ifndef _VM_TLB_H_
#define _VM_TLB_H_

#include <opt-paging.h>

void tlb_loadpage(vaddr_t vaddr, paddr_t paddr, bool readonly);
// uint32_t tlb_findfree(vaddr_t vaddr); <- Now static in vm_tlb.c
// uint32_t tlb_get_rr_victim(void);     <- Now static in vm_tlb.c

#endif