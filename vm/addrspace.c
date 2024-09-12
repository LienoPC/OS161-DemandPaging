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



/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <proc.h>
#include <vnode.h>
#include <elf.h>
#include <segments.h>
#include <cpu.h>
#include <vm_tlb.h>
#include <vm.h>
#include <vfs.h>
#include <addrspace.h>
#include <coremap.h>
#include <kern/fcntl.h>
#include <vmstats.h>


#if OPT_DUMBVM

#elif OPT_PAGING


/*
	Creates the address space structure for a process
*/
struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}
	bzero(as, sizeof(struct addrspace));
	/* 	We initialize the page table only after defining regions
		(and knowing the dimension of the virtual memory)
	*/
	return as;
}

/*
	Destroy the address space of a process, releasing the occupied frames
	and deallocating the as structure
*/
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
	as->segs.as_npages1 = 0;
	as->segs.as_npages2 = 0;
	as->segs.as_vbase1 = 0;
	as->segs.as_vbase2 = 0;
	as->segs.as_stackptbase = 0;
	as->segs.as_stackvbase = 0;
	as->segs.as_stackvtop = 0;
	/*
		Destroy all addrspace structures (page table and segments)
	*/
	kfree(as->frames);
	kfree(as->control_bits);
	pt_fifo_free(as->page_queue);

	vfs_close(as->swapfile);
	vfs_close(as->segs.progelf);

	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}
	/* Invalid the TLB on context switch */
	tlb_invalid();
    // Count the tlb invalidation
	increase_tlb_invalidations();
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
	for(i = 0; i < as->n_entry; i++){
		as->frames[i] = (paddr_t)0;
	}
	if (as->frames == NULL){
		panic("Problem in creating page table");
	}
	as->control_bits = kmalloc(as->n_entry*(sizeof(unsigned char)));
	if (as->control_bits == NULL) {
		panic("Problem in creating page table");
	}
	as->page_queue = pt_fifo_init();
	for(i = 0; i < as->n_entry; i++){
		as->control_bits[i] = 0;
	}


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
	#if OPT_PAGING
		as->segs.as_stackvbase = USERSTACK - PAGING_STACKPAGES * PAGE_SIZE;
		as->segs.as_stackvtop = USERSTACK;

		if (as->segs.as_vbase1 != 0 && as->segs.as_vbase2 != 0){
			/* Already defined the other regions, I can map the stack in the PT */
			as->segs.as_stackptbase = as->segs.as_npages1*PAGE_SIZE + as->segs.as_npages2*PAGE_SIZE;
		}

		/* Initial user-level stack pointer */
		*stackptr = as->segs.as_stackvtop;
	#else
		KASSERT(as->as_stackpbase != 0);

		*stackptr = USERSTACK;
	#endif

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

#endif
