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


void initialize_vmstats(void);
void increase_pf_disk(void);
void increase_pf_from_elf(void);
void increase_pf_from_swap(void);
void increase_pf_zeroed(void);
void increase_swapfile_writes(void);
void increase_tlb_faults(void);
void increase_tlb_faults_with_free(void);
void increase_tlb_faults_with_replace(void);
void increase_tlb_invalidations(void);
void increase_tlb_reloads(void);
void print_statistics(void);








#endif