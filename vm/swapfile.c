#include <types.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <vnode.h>
#include <uio.h>
#include <addrspace.h>
#include <proc.h>
#include <stat.h>
#include <swapfile.h>

/* Checks if the swap space is full */
bool is_sf_full(void) {
    off_t filesz;

    filesz = sf_getsize();
    if (filesz > MAX_SWAP_SPACE) {
        return true;
    }
    return false;
}

/* Checks whether a page (of size PAGE_SIZE) can fit in swap space */
bool sf_can_fit_page(void) {
    off_t filesz;

    filesz = sf_getsize();
    if(filesz + PAGE_SIZE + sizeof(vaddr_t) <= MAX_SWAP_SPACE) {
        return true;
    }
    return false;
}

/* Returns the current size of the swap file */
off_t sf_getsize(void) {
    struct addrspace *as;
    struct stat stats;

    as = proc_getas();
    if (VOP_STAT(as->swapfile, &stats)) {
        panic("Error during VOP_STAT in sf_getsize");
    }
    return stats.st_size;
}

/* 
 * Reads a single page from the swap file. 
 * Returns the following error codes:
 * -1: empty swapfile
 * -2: page not found
*/
int sf_pagein(vaddr_t vaddr, paddr_t paddr) {
    struct iovec header_iov, data_iov;
    struct uio header_ku, data_ku;
    vaddr_t header = -1;
    off_t filesz;
    struct addrspace *as;

    /* 
     * We don't panic here because free swap space availability should be
     * verified before writes. This function just reads.
     */
    KASSERT(!is_sf_full());
    KASSERT(sizeof(header) == sizeof(vaddr_t));

    as = proc_getas();
    filesz = sf_getsize();

    if(filesz == 0) {
        return -1;
    }

    /* Currently doing I/O using kernel space */
    uio_kinit(&header_iov, &header_ku, &header, sizeof(header), 0, UIO_READ);

    while (header_ku.uio_offset < filesz) {
        if (VOP_READ(as->swapfile, &header_ku)) {
            panic("Error during the first VOP_READ in sf_pagein");
        }

        KASSERT((header & PAGE_FRAME) == header);
        if (header == vaddr) {
            break;
        }

        header_ku.uio_offset += (PAGE_SIZE + sizeof(header));
    }

    if (header != vaddr) {
        return -2;
    }

    uio_kinit(&data_iov, &data_ku, (void *) paddr, PAGE_SIZE, header_ku.uio_offset + sizeof(header), UIO_READ);
    if (VOP_READ(as->swapfile, &data_ku)) {
        panic("Error during the second VOP_READ in sf_pagein");
    }

    return 0;
}

/* Writes a single page on the swap file.
 * Panics if the page can't fit in the available swap space. 
 */
void sf_pageout(vaddr_t vaddr, paddr_t paddr) {
    struct iovec header_iov, data_iov;
    struct uio header_ku, data_ku;
    off_t filesz;
    struct addrspace *as;

    if(!sf_can_fit_page()) {
        panic("Out of swap space");
    }

    as = proc_getas();
    filesz = sf_getsize();

    /* Currently doing I/O using kernel space */
    uio_kinit(&header_iov, &header_ku, &vaddr, sizeof(vaddr), filesz, UIO_WRITE);
    if(VOP_WRITE(as->swapfile, &header_ku)) {
        panic("Error during the first VOP_WRITE in sf_pageout");
    }
    uio_kinit(&data_iov, &data_ku, (void *) paddr, PAGE_SIZE, (off_t) (filesz + sizeof(vaddr)), UIO_WRITE);
    if(VOP_WRITE(as->swapfile, &data_ku)) {
        panic("Error during the first VOP_WRITE in sf_pageout");
    }
}