#include <types.h>
#include <lib.h>
#include <spl.h>
#include <../arch/mips/include/tlb.h>
#include <spinlock.h>
#include <vm.h>
#include <vm_tlb.h>
#include <vmstats.h>


struct vmstats *stats;
static uint32_t tlb_findfree(void); 
static uint32_t tlb_get_rr_victim(void);    

struct spinlock tlb_lock = SPINLOCK_INITIALIZER;
/*
	Find a free entry in the TLB, if the TLB is full
	return the total length
*/
static uint32_t tlb_findfree() {
    uint32_t ehi, elo;

	/* Linear search for a free entry in the TLB */
    for (uint32_t i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		if (elo & TLBLO_VALID) {
			continue;
		}
		return i;
	}
	/* There's no free entry */
	return (uint32_t) NUM_TLB; 
}

/*
	Function called on TLB replacement, finds a victim
	following a round-robin algorithm
*/
static uint32_t tlb_get_rr_victim(void) {
	uint32_t victim;
	static uint32_t next_victim = 0;

	
	/* Simple (and dumb) round robin victim selection */
	victim = next_victim;
	next_victim = (next_victim + 1) % NUM_TLB;
	return victim;
}

/*
	Insert a new entry in the TLB, using an empty position if found
	otherwise calling the replacement algorithm
*/
void tlb_loadentry(vaddr_t vaddr, paddr_t paddr, bool readonly) {
	struct addrspace *as;
	uint32_t ehi, elo, index;
	//int spl;
	(void) as;
	/* 
	 * Disable interrupts on this CPU while frobbing the TLB.
	 * spl = splhigh();
	 */
	

	//kprintf("Before findfree\n");
	/* Debug the content of the tlb */
	/*
	for (uint32_t i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		kprintf("TLB ENTRY %d: %u%u, valid bit %u\n", i, ehi,elo, elo & TLBLO_VALID);
	}
	*/
	
	
	spinlock_acquire(&tlb_lock);
	//spl = splhigh();
	/* Find a free entry or select one to replace */
	index = tlb_findfree();
	/*
	kprintf("Findfree index: %d\n", index);
	tlb_read(&ehi, &elo, index);
	kprintf("Evicted TLB ENTRY %d: %u%u, valid bit %u\n\n", index, ehi,elo, elo & TLBLO_VALID);
	*/
	

	if (index == NUM_TLB) {
		// Count this fault as TLB miss with replacement
		stats->tlb_faults_with_replace++;
		index = tlb_get_rr_victim();
	}else{
		// Count this fault as TLB miss with free
		stats->tlb_faults_with_free++;
	}
	
	ehi = vaddr & TLBHI_VPAGE;
	paddr = paddr & TLBLO_PPAGE;
	/* Set dirty bit only if vpage does not belong to the elf's text segment */
	if (readonly)
		elo = paddr | TLBLO_VALID;
	else
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;

	DEBUG(DB_VM, "TLB new entry: %u: 0x%x -> 0x%x\n", index, ehi, elo);
	tlb_write(ehi, elo, index);	
	//splx(spl);
	spinlock_release(&tlb_lock);

	/* Debug the content of the tlb */
	/*
	for (uint32_t i=0; i<NUM_TLB; i++) {
		tlb_read(&ehi, &elo, i);
		kprintf("TLB ENTRY %d: %u%u, valid bit %u\n", i, ehi,elo, elo & TLBLO_VALID);
	}
	kprintf("\n\n\n");
	
	
	*/
	
	


}

/*
	Invalids a single entry in the TLB, performing a linear search on the virtual address of the page
*/
void tlb_invalid_entry(vaddr_t vaddr){
	
	int spl, i;
	uint32_t ehi, elo;

	KASSERT((vaddr & PAGE_FRAME) == vaddr);
	spinlock_acquire(&tlb_lock);
	spl = splhigh();
	
	for (i = 0; i < NUM_TLB; i++){
		tlb_read(&ehi, &elo, i);
		if (ehi == vaddr) {
			tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
			break;
		}
	}

	splx(spl);
	spinlock_release(&tlb_lock);
}

/*
	Invalids the entire TLB
*/
void tlb_invalid(){
	
	int spl, i;

	spinlock_acquire(&tlb_lock);
	spl = splhigh();
	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}
	splx(spl);
	spinlock_release(&tlb_lock);
}


