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
#include <addrspace.h>
#include <vm.h>
#include <pt.h>
#include <coremap.h>



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
	paddr_t pa;
	vm_can_sleep();
	if (isCoremapActive()) {
		/* Use standard paging methods */
		pa = getcontinuousalloc((int)npages);
		if (pa == NULL){
			/* Could not find enough continuous pages, we should steal user pages  */
			/* TODO */
			
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
	(void) faulttype;
	(void) faultaddress;
	int i;
	uint32_t ehi, elo;
	struct addrspace *as;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "paging_vm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
			/* Tried to access a read-only page */
			break;
	    case VM_FAULT_READ:
			/* TLB miss on read an address */
	    case VM_FAULT_WRITE:
			/* TLB miss on write on an address */
			break;
	    default:
			return EINVAL;
	}

	/* It's the same code for read or write faults*/
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

	*ret = newas;
	return 0;
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
	/* TODO: aggiungere distruzione dello swapfile */


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
		 int readable, int writeable, int executable)
{

	/*
	 * Write this.
	 */

	(void)as;
	(void)vaddr;
	(void)memsize;
	(void)readable;
	(void)writeable;
	(void)executable;
	return ENOSYS;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */

	(void)as;

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

/* SHOULD NOT BE NEEDED */

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

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


