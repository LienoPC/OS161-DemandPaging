- [Paging Project](#c1-paging-project)
	- [Introduction](#introduction)
	- [TLB](#tlb)
	- [Coremap](#coremap)
	- [Page Table](#page-table)
	- [On-Demand Paging](#on-demand-paging)
	- [Swap File Management](#swap-file-management)
	- [Page Replacement](#page-replacement)
	- [Statistics](#statistics)
	- [Conclusions](#conclusions)
 	- [Team](#team)
  	- [License](#license) 


# C1: Paging Project

## Introduction

The goal of this project is to implement an evolution of the virtual memory management (DUMBVM) used by default in the OS161 kernel. Specifically, the proposed solution is based on the use of a **Page Table** in its _Per-process_ version, implementing the ability to read pages from virtual memory following the _On-Demand_ model—loading a page into physical memory only when it is referenced for the first time.
The project also aims to handle cases where physical memory is full by using a page-replacement algorithm (in our case, FIFO), allowing user processes to run even when their virtual memory is larger than RAM.
Frame access and allocation are handled using a dedicated structure called a _coremap_, which is used by both the kernel memory manager (kmalloc) and the user frame allocator.
Finally, demand paging and page replacement are supported by managing swap space on disk, using a fixed-length file to store swapped-out frames and enabling swap-in when necessary.

## TLB

Each time a user process accesses a virtual address not mapped in the TLB (TLB miss), a trap is triggered (handled by the `mips_trap` function in trap.c), delegating the TLB miss management to the `vm_fault` function (in pt.c), which receives the fault code (`faulttype`) and the virtual address that caused the miss (`faultaddress`). The ultimate goal of `vm_fault` is to insert a new entry into the TLB mapping `faultaddress` to a physical memory address.
The steps involved are:

1. Check the fault type. If the user process tries to access a text segment address (read-only), it is terminated:

```c
switch (faulttype) {
    case VM_FAULT_READONLY:
        /* Text segment pages must be readonly, so this can happen */
        DEBUG(DB_VM, "VM_FAULT_READONLY\n");
        sys__exit(VM_FAULT_READONLY);
        panic("thread_exit returned (should not happen)\n");
        break;
...
```
2. Perform checks on the validity and consistency of the user process's address space (the kernel terminates on a failed `KASSERT`)
3. Check which ELF segment the `faultaddress` belongs to. If it's the text segment, the dirty bit in the TLB entry is set to 0; otherwise, it is set to 1.
4. Call the `pt_getframe` function with the `faultaddress`. This function, via a series of page table management calls (which activate demand paging and page replacement if needed), returns the physical frame address associated with the `faultaddress`.
5. Update the TLB using the `tlb_loadentry` function, which takes the `faultaddress`, the physical address (`paddr`), and a flag indicating whether `faultaddress` belongs to the text segment (`readonly`).

The `tlb_loadentry` function does the following:
1. Finds a free TLB entry using `tlb_findfree`, or selects a victim via `tlb_get_rr_victim` (a simple round-robin algorithm) if the TLB is full.
2. Prepares the new TLB entry, setting the valid and dirty bits using `TLB_VALID` and `TLB_DIRTY` masks:

```c
ehi = vaddr & TLBHI_VPAGE;
paddr = paddr & TLBLO_PPAGE;
/* Set dirty bit only if vpage does not belong to the elf's text segment */
if (readonly)
    elo = paddr | TLBLO_VALID;
else
    elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
```
3. Inserts the new entry into the TLB using the `tlb_write` function.

## Coremap

An important aspect of memory management concerns the structure used to keep track of free frames and their allocation. The need to allocate contiguous memory for the kernel led us to choose an implementation based on a simple bitmap in the `coremap_t` struct, accompanied by an `allocSize` vector of the same size that tracks contiguous frame intervals allocated (similarly to what dumbvm does). The bitmap, maintained as a character array, assigns 0 to an occupied frame and 1 to a free frame. The coremap structure also contains the total number of frames in memory (`nRamFrames`), a spinlock to regulate access, and `last_frame`, used by the algorithm to determine where to start the search for contiguous frame allocation.

The most important functions include:
1. `paddr_t getfreeframe(void)`, which returns the physical address of a single allocated frame, called by the page table when a page needs to be loaded into memory.
2. `paddr_t getcontinuousalloc(int npages)`, which returns the physical address of the first of n allocated frames for the kernel (-1 if there are not enough free frames).
3. The related deallocation functions `releaseframe` and `releasecontiguousalloc`.

The function `alloc_kpages` has also been modified to use the coremap if already initialized; otherwise, it uses `ram_stealmem` (only during system startup).

## Page Table

As mentioned earlier, the page table follows a per-process structure, where each entry corresponds to a logical page of the process’s virtual address space. The proposed virtual memory modification also required changes to parts of the address space functions (originally in `dumbvm.c`, but moved to `addrspace.c` for this project), as the `struct addrspace` was completely revised. The page table is managed in `pt.c` using the struct defined in `addrspace.h`, consisting of the following fields:
1. `paddr_t *frames`: an array associating each page table entry with a physical address, corresponding to the frame if present.
2. `unsigned char *control_bits`: an array associating each entry with control bits:
   - S, swap bit, indicates if the entry should be handled using the swap space.
   - V, valid bit, indicates if the entry is valid, i.e., if the frame is present in memory.
   - Possibility for dirty and reference bits is included, but not used due to lack of hardware support.
3. `int n_entry`: initialized during PT creation, indicates the number of entries in the page table, corresponding to the number of logical pages of the process.
4. `vnode *swapfile`: keeps a reference to the vnode of the swap file, which is kept open during the process execution.
5. `segments segs`: a variable of type `struct segments` defined in `segments.h`, maintaining all information about the process’s virtual memory space and its ELF file; includes:
   - `vnode *progelf`: reference to the vnode of the process executable.
   - `text_ph` and `data_ph`: program headers of sections used for reading pages in demand paging (only two sections plus stack are supported in this version).
   - `as_vbase1` and `as_vbase2`: base virtual addresses of the two user memory sections, used for mapping to page table indices.
   - `as_npages1` and `as_npages2`: number of pages in the two sections.
   - `as_stackvbase`, `as_stackvtop`: respectively the last valid virtual address and first valid address of the stack (stack grows top-down). We use a fixed-length stack.
   - `as_stackptbase`: first virtual address of the stack mapped “from below” in the page table.
6. `pt_fifo_t *page_queue`: contains the FIFO queue used for page replacement.

The translation from a process’s virtual address to a page table index (and vice versa) is managed by the static functions `static int get_pt_index(struct addrspace *as, vaddr_t vaddr)` and `static vaddr_t get_vaddr_from_index(struct addrspace *as, int index)`.

## On-Demand Paging

The page table implementation is coupled with an On-Demand memory management, meaning that pages from a process’s logical address space are only loaded into memory when referenced. Initially, the page table has no references to frames, and the first instruction causes a TLB miss (handled by `vm_fault`), which follows this process:
1. Identify the fault type, terminating the process for write access to read-only pages.
2. Determine the process segment the fault address belongs to, to set the TLB entry’s read-only bit.
3. Call `paddr_t pt_getframe(vaddr_t addr)`, which returns the corresponding physical address.
4. The function checks if the entry is valid. If so, it returns the frame’s address; otherwise, it calls `paddr_t pt_pagefault(int index)`.
5. On a page fault:
   - If memory use exceeds a defined RAM threshold, the page replacement starts via `paddr_t pt_page_replacement(int dst_index)`.
   - Otherwise, a new frame is allocated using `getframe` from the coremap, and the page is loaded either:
     1. From the SWAPFILE (if the SWAP BIT is set),
     2. From the ELF file (if the SWAP BIT is not set),
     3. Or initialized as a blank page (e.g., stack access), in which case the SWAP BIT is immediately set.

Since we lack hardware support for the modify bit, swap-out is always performed on a page replacement, even if the page wasn’t modified.

## ELF File Management

A crucial component of demand paging is not loading the entire address space at process startup. Instead, pages are loaded on-demand. This required small changes in the function sequence during process startup, especially in `load_elf`. Instead of loading all segments with `load_segment`, we now initialize the `struct segments` inside the process’s addrspace and read individual pages later.

Page loading involves:
1. `get_elf_offset`: calculates the physical offset in the ELF file to read from, based on the segment the virtual address belongs to.
2. `load_from_elf`: using the offset, sets up a kernel uio and calls `VOP_READ` to load the page from the ELF file into the allocated frame.

This allows dynamic loading of logical pages from the ELF file into RAM.

# Swap File Management

The SWAPFILE in the root directory constitutes the system's swap space.
In the `swapfile.c` file (and header `swapfile.h`), functions are defined to interface with the swap file, supporting page out, page in, and therefore page replacement operations.
We describe here how the swap file is managed because it is useful for understanding the implementation of page replacement.

The maximum size of the swap space can be easily set from `swapfile.h` via the constant `MAX_SWAP_SPACE`.

All read and write operations to the swap file are handled via `struct iovec` and `struct uio`, initialized to handle kernel space I/O (via the `uio_kinit` function).
Reading and writing are performed using the macros `VOP_READ` and `VOP_WRITE` respectively.

Before proceeding with the function descriptions provided by `swapfile.c`, it is important to note that a page written to the swap file during a page out operation will be 4100 bytes in size instead of 4096 (`PAGE_SIZE`), as the page content is preceded by its virtual address (type `vaddr_t`, 4 bytes), in order to identify the page during a page in.

Defined functions for swap file operations are:
1. `off_t sf_getsize(void)`: gets the current size of the swap file using `VOP_STAT` to retrieve file stats, and returns the size (`st_size` field of `struct stat`).
2. `bool is_sf_full(void)`: returns `true` if the swap space is full, `false` otherwise.
3. `bool sf_can_fit_page(void)`: returns `true` if the swap file can accommodate at least one more page (4096 + 4 bytes), otherwise `false`.
4. `int sf_pagein(vaddr_t vaddr, paddr_t paddr)`: reads the page identified by virtual address `vaddr` and writes it to physical address `paddr`. Returns specific error codes if the swap file is empty or the page is not found.
5. `void sf_pageout(vaddr_t vaddr, paddr_t paddr, off_t offset)`: writes the contents of the frame at physical address `paddr` (corresponding virtual address is `vaddr`, written before the page content) to the swap file. `offset` distinguishes between append and overwrite, and must be aligned to 4100 bytes. In case of append, the kernel panics if there's insufficient swap space.
6. `void sf_replacepage(vaddr_t vic_vaddr, vaddr_t dst_vaddr, paddr_t vic_paddr)`: supports swap file-based page replacement. It first reads from swap file the page identified by `dst_vaddr`, asserting failure if not found, and stores it in a buffer. Then calls `sf_pageout` to page out the victim page (identified by `vic_vaddr` and `vic_paddr`). Finally, it copies the buffer's content to the destination frame after zeroing it. This avoids file append, preserving the file size.

# Page Replacement

Page replacement is managed via a FIFO algorithm. The queue used by the algorithm is `pt_fifo_t`, defined as an ADT in `pt_fifo.h` and implemented in `pt_fifo.c`.
The `struct addrspace` contains a `pt_fifo_t *page_queue` field used to track mapped pages in the page table.
Interface functions of the data structure include:
- `pt_fifo_t *pt_fifo_init(void)`: initializes the queue.
- `void pt_fifo_push_back(pt_fifo_t *fifo, int pt_index)`: adds an element to the end of the queue.
- `int pt_fifo_pop_front(pt_fifo_t *fifo)`: removes the front element of the queue.
- `void pt_fifo_pop(pt_fifo_t *fifo, int pt_index)`: removes a specific element by its `pt_index` (not its position in queue).
- `void pt_fifo_free(pt_fifo_t *fifo)`: frees allocated memory for the queue.

When page table entries are added or removed, the corresponding index is respectively inserted or removed from the queue, so `page_queue` tracks only the pages currently present in the page table.

The function `static paddr_t pt_page_replacement(int dst_index)` defined in `pt.c` manages the page replacement process:
1. If `as->page_queue` is not empty, removes the head (`pt_fifo_pop_front`) and retrieves the page table index, identifying the victim page. If empty, it panics with "Out of memory".
2. Calls `pt_invalid_entry` with the victim's page table index to invalidate it and set the swap bit:
```c
static void
pt_invalid_entry(struct addrspace *as, int index){
	as->control_bits[index] &= ~PT_VALID_BIT;
	if(!(as->control_bits[index] & PT_SWAP_BIT)) {
		as->control_bits[index] |= PT_SWAP_BIT;
	}
	tlb_invalid_entry(get_vaddr_from_index(as, index));
}
```
3. If the page to be loaded has its swap bit set (i.e., it's on the swap file), it calls `sf_replacepage`. Otherwise, the page is read and loaded from the ELF file: it pages out the victim with `sf_pageout` and loads the new page from ELF.
4. Returns the physical address of the frame where the page was loaded.

The page table is updated after replacement in `pt_getframe` at the return from `pt_pagefault`:
```c
paddr = pt_pagefault(index);
as->frames[index] = paddr;
as->control_bits[index] |= PT_VALID_BIT;
pt_fifo_push_back(as->page_queue, index);
```

Note the index insertion into `as->page_queue` for the new page table entry.

# Statistics

Statistics collection is the final element, simply based on defining a static struct in `vmstats.h` containing all fields (counters) to track specific kernel events, as required by the project. External access is managed via individual functions that increment the structure's counters:
```c
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
```
The structure is initialized during the coremap bootstrap, and statistics printing and validation are added to the `vm_shutdown` function, called on OS161 shutdown.

# Conclusions

To conclude, the introduced structures lack certain improvements, like synchronization primitives for the page table, which isn't an issue here due to the single-threaded user process assumption. Although this new virtual memory implementation can degrade performance for simple user programs, it allows user processes with virtual memory larger than RAM to run and ensures, in theory, no out-of-memory conditions.

## Team
[Alberto Cagnazzo](https://github.com/LienoPC)
[Giulio Arecco](https://github.com/giulio-arecco)

## License
The original code in this repository is licensed under the MIT License - see the [LICENSE](LICENSE) file for details. 

**Third-Party Code:** 
This repository includes third-party code (the OS161 kernel code). These files remain licensed under their respective original terms and retain their original copyright notices.
