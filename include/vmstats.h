#ifndef _VMSTATS_H_
#define _VMSTATS_H_

#include <opt-paging.h>

struct vmstats{

    unsigned int tlb_faults;
    unsigned int tlb_faults_with_free;
    unsigned int tlb_faults_with_replace;
    unsigned int tlb_invalidations;
    unsigned int tlb_reloads;
    unsigned int pf_zeroed;
    unsigned int pf_disk;
    unsigned int pf_from_elf;
    unsigned int pf_from_swap;
    unsigned int swapfile_writes;
};




#endif