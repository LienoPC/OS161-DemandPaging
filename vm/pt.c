/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <cpu.h>
#include <spinlock.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <elf.h>
#include <vnode.h>
#include <segments.h>
#include <addrspace.h>
#include <vm_tlb.h>
#include <vm.h>
#include <vfs.h>
#include <swapfile.h>
#include <coremap.h>
#include <pt.h>
#include <kern/fcntl.h>


/* under dumbvm, always have 72k of user stack */
/* (this must be > 64K so argument blocks of size ARG_MAX will fit) */
#define PAGING_STACKPAGES    18


/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

/*
*
*	Configure at boot the kernel memory allocation structure (call coremap to manage 
	memory frames) and prepare data structures for page table
*
*/

static
void
as_zero_region(paddr_t paddr, unsigned npages);

static int
get_pt_index(struct addrspace *as, vaddr_t vaddr);

static off_t
get_elf_offset(struct addrspace *as, vaddr_t vaddr);

static void
pt_read_from_swap(vaddr_t vaddr, paddr_t paddr);



/*
	Translate the vaddr to the page addr in the pt
*/
static int
get_pt_index(struct addrspace *as, vaddr_t vaddr){
	int index = -1;
	if(vaddr >= as->segs.as_vbase1 && vaddr < (as->segs.as_vbase1 + PAGE_SIZE*as->segs.as_npages1)) {
		index = (vaddr - as->segs.as_vbase1)/PAGE_SIZE;
	}
	else if (vaddr >= as->segs.as_vbase2 && vaddr < (as->segs.as_vbase2 + PAGE_SIZE*as->segs.as_npages2)) {
		index = (vaddr - as->segs.as_vbase2 + (as->segs.as_npages1*PAGE_SIZE))/PAGE_SIZE;
	}else if (vaddr >= as->segs.as_stackvbase && vaddr < as->segs.as_stackvtop) {
		index = (as->segs.as_stackptbase + (as->segs.as_stackvtop - vaddr))/PAGE_SIZE;
	}else{
		// Invalid vaddr
		index = -1;
	}
	return index;
}

/*
	Compute the physical offset of the page in the elf file
*/
static off_t
get_elf_offset(struct addrspace *as, vaddr_t vaddr){
	off_t offset;
	/* Compute virtual page address offset in the file */
	if (vaddr >= as->segs.as_vbase1 && vaddr < (as->segs.as_vbase1 + PAGE_SIZE*as->segs.as_npages1)) {
		offset = (vaddr - as->segs.as_vbase1) + as->segs.text_ph.p_offset;
		KASSERT((offset & PAGE_FRAME) == offset);
	}
	else if (vaddr >= as->segs.as_vbase2 && vaddr < (as->segs.as_vbase2 + PAGE_SIZE*as->segs.as_npages2)) {
		offset = (vaddr - as->segs.as_vbase2) + as->segs.data_ph.p_offset;
		KASSERT((offset & PAGE_FRAME) == offset);
	}
	else if (vaddr >= as->segs.as_stackvbase && vaddr < as->segs.as_stackvtop) {
		offset = (off_t) -1;
	}else{
		offset = (off_t) -1;
	}
	return offset;
}

/* Imported from dumbvm, should check if it works fine */

static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}


/*
	Reads the page from the swapfile
*/
static void
pt_read_from_swap(vaddr_t vaddr, paddr_t paddr){
		/* Swap-in: read from the swap file */
		switch (sf_pagein(vaddr, paddr))
		{
		case 0:
			/* Everything alright */
			break;
		case -1:
			/* Everything alright */
			panic("Empty swapfile on swap-in");
			break;
		case -2:
			/* Page not found, try read the elf file (?) */
			panic("Bad addr on swapfile");
			break;
		default:
			break;
		}			

}


void
vm_bootstrap(void)
{
  coremap_bootstrap();
}

/*
 * Check if we're in a context that can sleep. While most of the
 * operations in dumbvm don't in fact sleep, in a real VM system many
 * of them would. In those, assert that sleeping is ok. This helps
 * avoid the situation where syscall-layer code that works ok with
 * dumbvm starts blowing up during the VM assignment.
 */
static void
vm_can_sleep(void)
{
	if (CURCPU_EXISTS()) {
		/* must not hold spinlocks */
		KASSERT(curcpu->c_spinlocks == 0);

		/* must not be in an interrupt handler */
		KASSERT(curthread->t_in_interrupt == 0);
	}
}

/* HERE SHOULD GO ALL METHODS TO ALLOCATE/FREE KERNEL-SPACE MEMORY*/

/* TODO: allocate npages for the kernel (as contiguous allocation)*/
vaddr_t
alloc_kpages(unsigned npages)
{

	(void) npages;
	paddr_t pa, first_steal, search;
	struct addrspace *as;
	int i,j;
	vm_can_sleep();
	if (isCoremapActive()) {
		/* Use standard paging methods */
		pa = getcontinuousalloc((int)npages);
		if (pa == (paddr_t) NULL){
			/* Could not find enough continuous pages, we should steal user pages  */
			/* TODO */
			as = proc_getas();
			/* First search for contiguous nframes to steal from the user */
			first_steal = findfirsttosteal(npages);
			
			if(first_steal == (paddr_t)NULL){
				/* Didn't found enough memory on contiguous steal, out of memory*/
				panic("Completely out of memory!");
			}

			/* Starting from the first frame, execute the page-out for every frame in the interval */
			for(i = 0; i < (int)npages; i++){
				search = first_steal + PAGE_SIZE*i;
				for(j = 0; j < as->n_entry; j++){
					if (as->frames[j] == search){
						// Found the frame, free it
						if(!pt_pageout(j)){
							panic("Could not steal a user frame");
						}
						
					}
				}
			}
			
			/* Effectively assign to kernel the freed pages */
			pa = stealcontinuousalloc(npages, first_steal);

		}

	}else{
		/* Use ram_stealmem */
		spinlock_acquire(&stealmem_lock);
		pa = ram_stealmem(npages);
		spinlock_release(&stealmem_lock);
	}
	return PADDR_TO_KVADDR((paddr_t) pa);
	
}

void 
free_kpages(vaddr_t addr){
(void) addr;
(void) stealmem_lock;
	
}

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}



/* HERE SHOULD GO ALL METHODS TO MANAGE THE ADDRESSPACE OF A PROCESS
	AND ALSO ALL THE METHODS TO MANAGE THE ACTUAL PAGE TABLE 
*/


int
vm_fault(int faulttype, vaddr_t faultaddress)
{ 
	vaddr_t vbase1, vtop1, vbase2, vtop2, stackbase, stacktop;
	paddr_t paddr = (paddr_t)NULL;
	struct addrspace *as;
	bool readonly;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* Text segment pages must be readonly, so this can happen */
		DEBUG(DB_VM, "VM_FAULT_READONLY\n");
		// TODO: CLEAN PROCESS TERMINATION
		// How do we terminate all proc threads?
		break;
	    case VM_FAULT_READ:
		DEBUG(DB_VM, "VM_FAULT_READ\n");
		break;
	    case VM_FAULT_WRITE:
		DEBUG(DB_VM, "VM_FAULT_WRITE\n");
		break;
	    default:
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Assert that the address space has been set up properly. */
	KASSERT(as->segs.as_vbase1 != 0);
	KASSERT(as->segs.as_vbase2 != 0);
	KASSERT(as->segs.as_stackvbase != 0);
	KASSERT(as->segs.as_stackvtop  != 0);
	KASSERT((as->segs.as_vbase1 & PAGE_FRAME) == as->segs.as_vbase1);
	KASSERT((as->segs.as_vbase2 & PAGE_FRAME) == as->segs.as_vbase2);
	KASSERT((as->segs.as_stackvbase & PAGE_FRAME) == as->segs.as_stackvbase);

	vbase1 = as->segs.as_vbase1;
	vtop1 = vbase1 + as->segs.as_npages1 * PAGE_SIZE;

	vbase2 = as->segs.as_vbase2;
	vtop2 = as->segs.as_vbase2 + as->segs.as_npages2 * PAGE_SIZE;
	//kprintf("%d", as->segs.as_npages2);
	stackbase = as->segs.as_stackvbase;
	stacktop = as->segs.as_stackvtop;
	
	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		/* 
		 * Check if faultaddress belongs to the program's text segment.
		 * If so, the new TLB entry will have the dirty bit cleared
		 */
		readonly = true;
	}
	else if ((faultaddress >= vbase2 && faultaddress < vtop2) || (faultaddress >= stackbase && faultaddress < stacktop)) {
		readonly = false;
	}
	else {
		/* fauladdress is not a virtual address in the current process's address space */
		DEBUG(DB_VM, "faultaddress not in curproc as\n");
		return EFAULT;
	}

	

	/* 
	 * If faultaddress is already in memory, load the appropriate paddr in the TLB,
	 * setting the dirty bit according to faultaddress' segment.
	 * Otherwise the page must be loaded from the elf to ram, the PT must be updated
	 * and then the newly mapped paddr must be loaded in the TLB
	 */
	paddr = pt_getframe(faultaddress);

/* Disable interrupts while handling possible page faults and frobbing the TLB */
	spl = splhigh();
	/* make sure paddr is page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	tlb_loadentry(faultaddress, paddr, readonly);
	/* Re-enable interrupts */
	splx(spl);
	
	return 0;
}

/*
	ACCESS THE PAGE TABLE
*/

/* Tries to get a frame for a logical page */

paddr_t
pt_getframe(vaddr_t addr){
	paddr_t paddr = (paddr_t)NULL;
	int i;
	struct addrspace *as;
	as = proc_getas();

	KASSERT((addr & PAGE_FRAME) == addr);

	/* Get the index of the PT */
	i = get_pt_index(as, addr);
	/* Verify if the entry is valid */
	if (as->control_bits[i] & PT_VALID_BIT){
		/* Page is in memory, return the physical frame */
		paddr = as->frames[i];
	}else{
		/* PAGE FAULT: frame not in memory */
		paddr = pt_pagefault(addr);
		/* update the PT */
		as->frames[i] = paddr;
		/* now the pt entry is valid */ 
		as->control_bits[i] |= PT_VALID_BIT;
		

	}
	return paddr;

}

/* 
	Manages a page fault 

	Tries to take a frame from memory and reads from the elf/swap file basing
	on the swap bit of the entry
*/

paddr_t
pt_pagefault(vaddr_t addr){
	paddr_t paddr = (paddr_t)NULL;
	int index;
	off_t offset = (off_t) 0;
	struct addrspace *as;
	as = proc_getas();

	/* First get the physical frame and then start the page replacement if there are no available frames */
	paddr = getfreeframe();
	if (paddr == (paddr_t) NULL){
		/* Page Replacement */
	}else{
		/* Zeroing the frame */
		as_zero_region(paddr,1);
		index = get_pt_index(as, addr);
		if (as->control_bits[index] & PT_SWAP_BIT){
			pt_read_from_swap(index*PAGE_SIZE, paddr);
		}else{
			/* Compute the offset in the elf file of the page we want to read*/
			/* If the address is stack (not in the swap file), allocate an empty frame */
			offset = get_elf_offset(as, addr);
			if (offset != (off_t)-1){
				/* Read the page from the elf file */
				if (load_from_elf(as, paddr, offset)){
					panic("Error during the load of a page from the elf file");
				}
			}else{
				/* If is the first access to a stack page (hopefully write), we set the swap bit on the entry */
				as->control_bits[index] |= PT_SWAP_BIT;
			}
		}
	} 

	return paddr;
}

/* 	
	Executes the page-out of a page in the PT 

	-Invalid the entry 
	-Verify the swap bit and swap-out the page
	-Zero the pframe

*/

int
pt_pageout(int index){	
	struct addrspace *as;
	as = proc_getas();

	as->control_bits[index] &= ~(PT_VALID_BIT);
	
	if (as->control_bits[index] & PT_SWAP_BIT || as->control_bits[index] & PT_DIRTY_BIT){
		/* This page works on the swap file */
		sf_pageout(index*PAGE_SIZE, as->frames[index], -1);
		/* Set the swap bit if the swap-out is executed on dirty bit */
		if (!(as->control_bits[index] & PT_SWAP_BIT)){
			as->control_bits[index] |= PT_SWAP_BIT; 
		}
	}

	as_zero_region(as->frames[index], 1);
	return 0;
}

/*
	CREATE THE PAGE TABLE AND SEGMENTS STRUCT OF A PROCESS
*/

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}


	/* 	We initialize the page table only after defining regions
		(and knowing the dimension of the virtual memory)
	*/
	
	return as;
}


void
as_destroy(struct addrspace *as)
{
	/*
		Free all allocated frames 
	*/
	int i;
	for (i = 0; i < as->n_entry; i++){
		if ((as->control_bits[i] & PT_VALID_BIT) == PT_VALID_BIT){
			// Valid entry, frame has to be freed
			releaseframe(as->frames[i]);
		}
	}

	/*
		Destroy all addrspace structures (page table and segments)
	*/
	kfree(as->frames);
	kfree(as->control_bits);

	vfs_close(as->swapfile);
	vfs_close(as->segs.progelf);

	kfree(as);
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}
	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 Elf_Phdr ph, int readable, int writeable, int executable)
{

	size_t npages;

	/* Align the region. First, the base... */
	memsize += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	memsize = (memsize + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = memsize / PAGE_SIZE;
	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->segs.as_vbase1 == 0) {
		as->segs.as_vbase1 = vaddr;
		as->segs.as_npages1 = npages;
		as->segs.text_ph = ph;
		return 0;
	}

	if (as->segs.as_vbase2 == 0) {
		as->segs.as_vbase2 = vaddr;
		as->segs.as_npages2 = npages;
		as->segs.data_ph = ph;
		return 0;
	}

	
	/*
	 * Support for more than two regions is not available.
	 */
	kprintf("dumbvm: Warning: too many regions\n");
	return ENOSYS;
}

/* Saves the program's elf vnode into the addrspace */
void as_set_progelf(struct addrspace *as, struct vnode* elf) {
	/* Increase the refcount of elf's vnode, see runprogram for more details on this */
	VOP_INCREF(elf);

	as->segs.progelf = elf;
}

/* Saves the swapfile's vnode into the addrspace */
int as_set_swapfile(struct addrspace *as, char* path) {
	int result;
	struct vnode *sf;
	result = vfs_open(path, O_RDWR | O_TRUNC | O_CREAT, 0, &sf);
	if (result) {
		return result;
	}

	as->swapfile = sf;

	return 0;
}

/* Initialize the address space after the definition of all the segments */
void
as_initialize_pt(struct addrspace *as){
	int i;
	KASSERT(as->segs.as_vbase1 != 0);
	KASSERT(as->segs.as_vbase2 != 0);
	KASSERT(as->segs.as_stackptbase != 0);
	KASSERT(as->n_entry == 0);

	as->n_entry = as->segs.as_npages1 + as->segs.as_npages2 + PAGING_STACKPAGES;
	as->frames = kmalloc(as->n_entry*sizeof(paddr_t));
	if (as->frames == NULL){
		panic("Problem in creating page table");
	}
	as->control_bits = kmalloc(as->n_entry*(sizeof(unsigned char)));
	if (as->control_bits == NULL) {
		panic("Problem in creating page table");
	}
	for(i = 0; i < as->n_entry; i++){
		as->control_bits[i] = 0;
	}
	as->last_c_freed = 0;


}


int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * 	Copy the page table
	 */
	newas->frames = kmalloc(sizeof(paddr_t)*old->n_entry);
	newas->control_bits = kmalloc(sizeof(unsigned char)*old->n_entry);
	newas->n_entry = old->n_entry;
	newas->last_c_freed = 0;
	
	memmove((void *)newas->frames,
		(const void *)old->frames,
		sizeof(paddr_t)*old->n_entry);

	memmove((void *)newas->control_bits,
	(const void *)old->control_bits,
	sizeof(unsigned char)*old->n_entry);

	/*
		Copy the segments structure
	*/
	newas->segs = old->segs;

	/*
		Copy the elf file and assign new swapfile
	*/
	as_set_progelf(newas, old->segs.progelf);

	*ret = newas;
	return 0;
}



int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	as->segs.as_stackvbase = USERSTACK - PAGING_STACKPAGES * PAGE_SIZE;
	as->segs.as_stackvtop = USERSTACK;

	if (as->segs.as_vbase1 != 0 && as->segs.as_vbase2 != 0){
		/* Already defined the other regions, I can map the stack in the PT */
		as->segs.as_stackptbase = as->segs.as_npages1*PAGE_SIZE + as->segs.as_npages2*PAGE_SIZE;
	}

	/* Initial user-level stack pointer */
	*stackptr = as->segs.as_stackvtop;

	return 0;
}

/* SHOULD NOT BE NEEDED */

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */
	KASSERT(as->segs.as_vbase1 == 0);
	KASSERT(as->segs.as_vbase2 == 0);
	KASSERT(as->segs.as_stackptbase == 0);

	(void)as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

	(void)as;
	return 0;
}
