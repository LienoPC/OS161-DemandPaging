#include <types.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <vnode.h>
#include <elf.h>
#include <segments.h>
#include <addrspace.h>

/*
    Loads a page from the elf file, usign the elf header and the program header of the segment stored in the addrspace structure
    Given the offset of the segment, sums the offset of the virtual page we want to gather and reads PAGE_SIZE
*/

/* AGGIUNGERE IL VNODE */
int
load_from_elf(paddr_t *paddr, struct addrspace *as, off_t offset){
	
    struct iovec iov;
    struct uio ku;
    /* 
     * Giulio: potrebbe essere necessario aggiungere eh.e_phentsize 1 volta se carico da text seg
     * o 2 volte se carico da data seg, come viene fatto nella load_elf nel for di set up dell'addrspace 
     */
    off_t offset = as->segs.eh.e_phoff + offset; // Offset in the ELF file of the page we want to read
    uio_kinit(&iov, &ku, &as->segs.ph, sizeof(as->segs.ph), offset, UIO_READ);

    result = VOP_READ(v, &ku);
    if (result) {
        return result;
    }

    if (ku.uio_resid != 0) {
        /* short read; problem with executable? */
        kprintf("ELF: short read on phdr - file truncated?\n");
        return ENOEXEC;
    }

    switch (ph.p_type) {
        case PT_NULL: /* skip */ continue;
        case PT_PHDR: /* skip */ continue;
        case PT_MIPS_REGINFO: /* skip */ continue;
        case PT_LOAD: break;
        default:
        kprintf("loadelf: unknown segment type %d\n",
            ph.p_type);
        return ENOEXEC;
    }

    result = load_segment(as, v, ph.p_offset, ph.p_vaddr,
                    ph.p_memsz, ph.p_filesz,
                    ph.p_flags & PF_X);
    if (result) {
        return result;
    }
}
