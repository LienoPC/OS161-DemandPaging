#include <types.h>
#include <lib.h>
#include <spl.h>
#include <../arch/mips/include/tlb.h>
#include <vm_tlb.h>

static uint32_t tlb_findfree(); 
static uint32_t tlb_get_rr_victim(void);    



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

static uint32_t tlb_get_rr_victim(void) {
	uint32_t victim;
	static uint32_t next_victim = 0;

	/* Simple (and dumb) round robin victim selection */
	victim = next_victim;
	next_victim = (next_victim + 1) % NUM_TLB;
	return victim;
}

void tlb_loadentry(vaddr_t vaddr, paddr_t paddr, bool readonly) {
	struct addrspace *as;
	uint32_t ehi, elo, index;
	int spl;
	(void) as;
	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	/* Find a free entry or select one to replace */
	index = tlb_findfree();
	if (index == NUM_TLB) {
		index = tlb_get_rr_victim();
	}

	ehi = vaddr & TLBHI_VPAGE;
	/* Set dirty bit only if vpage does not belong to the elf's text segment */
	if (readonly)
		elo = paddr | TLBLO_VALID;
	else
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;

	DEBUG(DB_VM, "TLB new entry: %u: 0x%x -> 0x%x\n", index, ehi, elo);
	tlb_write(ehi, elo, index);	
	splx(spl);
}

