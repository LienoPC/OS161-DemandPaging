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
#include <opt-paging.h>

#if OPT_PAGING

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
	vm_can_sleep();
	
	return PADDR_TO_KVADDR((paddr_t) 0x00000);
	
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
	panic("dumbvm tried to do tlb shootdown?!\n");
}



/* HERE SHOULD GO ALL METHODS TO MANAGE THE ADDRESSPACE OF A PROCESS
	AND ALSO ALL THE METHODS TO MANAGE THE ACTUAL PAGE TABLE 
*/


int
vm_fault(int faulttype, vaddr_t faultaddress)
{ 
	// uint32_t ehi, elo;
	struct addrspace *as;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "dumbvm: fault: 0x%x\n", faultaddress);

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* Text segment pages must be readonly, so this can happen */
		DEBUG(DB_VM, "VM_FAULT_READONLY\n");
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
	// TODO!

	/* 
		If faultaddress is already in memory, load the appropriate paddr in the TLB,
	   	setting the dirty bit according to faultaddress' segment.
	   	Else: the page must be loaded from the elf to ram, the PT must be updated
	   	and then the newly mapped paddr must be loaded in the TLB
	*/

	/*
		TLB replacement algorithm
	*/

	return 0;
}

struct addrspace *
as_create(void)
{
	return NULL;
}

void as_destroy(struct addrspace *as){
	(void) as;
  vm_can_sleep();
}

void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
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
	/* nothing */
}

int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	(void) as;
	(void) vaddr;
	(void) sz;
	(void) readable;
	(void) writeable;
	(void) executable;
	return 0;
}

/*
static
void
as_zero_region(paddr_t paddr, unsigned npages)
{
	bzero((void *)PADDR_TO_KVADDR(paddr), npages * PAGE_SIZE);
}
*/



int
as_prepare_load(struct addrspace *as)
{
	(void) as;
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	vm_can_sleep();
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	(void) as;
	(void) stackptr;
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{	
	(void) old;
	(void) ret;
	return 0;
}

#endif