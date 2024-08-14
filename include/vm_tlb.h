#ifndef _VM_TLB_H_
#define _VM_TLB_H_

#include <opt-paging.h>

void tlb_loadentry(vaddr_t vaddr, paddr_t paddr, bool readonly);


#endif