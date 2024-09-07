#include <types.h>
#include <lib.h>
#include <vmstats.h>

static struct vmstats *stats;

void
initialize_vmstats(){
    // Initialize vmstats struct
    stats = kmalloc(sizeof(struct vmstats));
    stats->pf_disk = 0;
    stats->pf_from_elf = 0;
    stats->pf_from_swap = 0;
    stats->pf_zeroed = 0;
    stats->swapfile_writes = 0;
    stats->tlb_faults = 0;
    stats->tlb_faults_with_free = 0;
    stats->tlb_faults_with_replace = 0;
    stats->tlb_invalidations = 0;
    stats->tlb_reloads = 0;
}

void increase_pf_disk(void){
    stats->pf_disk++;
}

void increase_pf_from_elf(void){
    stats->pf_from_elf++;
}

void increase_pf_from_swap(void){
    stats->pf_from_swap++;
}

void increase_pf_zeroed(void){
    stats->pf_zeroed++;
}

void increase_swapfile_writes(void){
    stats->swapfile_writes++;
}

void increase_tlb_faults(void){
    stats->tlb_faults++;
}

void increase_tlb_faults_with_free(void){
    stats->tlb_faults_with_free++;
}

void increase_tlb_faults_with_replace(void){
    stats->tlb_faults_with_replace++;
}

void increase_tlb_invalidations(void){
    stats->tlb_invalidations++;
}

void increase_tlb_reloads(void){
    stats->tlb_reloads++;
}

void print_statistics(void){
    // We print here the vm statistics
	kprintf("************ VM STATISTICS ***********\n");
	kprintf("TLB faults: %d\n", stats->tlb_faults);
	kprintf("TLB faults with free: %d\n", stats->tlb_faults_with_free);
	kprintf("TLB faults with replace: %d\n", stats->tlb_faults_with_replace);
	kprintf("TLB invalidations: %d\n", stats->tlb_invalidations);
	kprintf("TLB reloads: %d\n", stats->tlb_reloads);
	kprintf("Page faults (Zeroed): %d\n", stats->pf_zeroed);
	kprintf("Page faults (Disk): %d\n", stats->pf_disk);
	kprintf("Page faults from ELF: %d\n", stats->pf_from_elf);
	kprintf("Page faults from Swapfile: %d\n", stats->pf_from_swap);
	kprintf("Swapfile writes: %d\n", stats->swapfile_writes);


	if ((stats->tlb_faults_with_free+stats->tlb_faults_with_replace) != stats->tlb_faults &&
		(stats->tlb_reloads + stats->pf_disk + stats->pf_zeroed) != stats->tlb_faults &&
		(stats->pf_from_elf + stats->pf_from_swap) != stats->pf_disk){
		kprintf("\nWARNING: wrong stats equalities\n\n");
	}
}




