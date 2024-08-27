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
    uio_kinit(&header_iov, &header_ku, &header, sizeof(header), (off_t) 0, UIO_READ);

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

    uio_kinit(&data_iov, &data_ku, (void *) paddr, PAGE_SIZE, (off_t) header_ku.uio_offset + sizeof(header), UIO_READ);
    if (VOP_READ(as->swapfile, &data_ku)) {
        panic("Error during the second VOP_READ in sf_pagein");
    }

    return 0;
}

/* Writes a single page on the swap file.
 * Panics if the page can't fit in the available swap space. 
 */
void sf_pageout(vaddr_t vaddr, paddr_t paddr, off_t offset) {
    struct iovec header_iov, data_iov;
    struct uio header_ku, data_ku;
    off_t filesz;
    struct addrspace *as;

    as = proc_getas();
    filesz = sf_getsize();

    /* 
     * The offset should only be used if the intention is to overwrite a page
     * (normally during page replacement). To append a new page to the swapfile,
     * a negative integer should be passed instead.
     */
    KASSERT(offset < filesz);

    /* Currently doing I/O using kernel space */

    /* If the offset is < 0, append to the end of the file */
    if(offset < (off_t) 0) {
        if(!sf_can_fit_page()) {
            panic("Out of swap space");
        }
        uio_kinit(&header_iov, &header_ku, &vaddr, sizeof(vaddr), (off_t) filesz, UIO_WRITE);
        uio_kinit(&data_iov, &data_ku, (void *) paddr, PAGE_SIZE, (off_t) (filesz + sizeof(vaddr)), UIO_WRITE);
        
    }
    else {
        /* Make sure the offset is aligned to the page size on the swapfile (considering the "header") */
        KASSERT(offset % ((off_t) (PAGE_SIZE + sizeof(vaddr))) == 0);
        uio_kinit(&header_iov, &header_ku, &vaddr, sizeof(vaddr), offset, UIO_WRITE);
        uio_kinit(&data_iov, &data_ku, (void *) paddr, PAGE_SIZE, (off_t) (offset + sizeof(vaddr)), UIO_WRITE);
    }

    if(VOP_WRITE(as->swapfile, &header_ku)) {
        panic("Error during the first VOP_WRITE in sf_pageout");
    }
        
    if(VOP_WRITE(as->swapfile, &data_ku)) {
        panic("Error during the second VOP_WRITE in sf_pageout");
    }
}

void sf_replacepage(vaddr_t vic_vaddr, vaddr_t dst_vaddr, paddr_t vic_paddr, paddr_t dst_paddr) {
    /* 
     * We don't panic here because free swap space availability should be
     * verified before writes that make the swap file grow in size (basically appending).
     * Here, we overwrite a page, so in the end the file size should be unaltered.
     */
    KASSERT(!is_sf_full());

    struct iovec header_iov, data_iov;
    struct uio header_ku, data_ku;
    vaddr_t header = -1;
    off_t filesz, page_offset;
    struct addrspace *as;
    char buf[PAGE_SIZE];

    as = proc_getas();
    filesz = sf_getsize();

    /* There must be at least a page on the swap file to start
     * replacing pages with it.
     */
    KASSERT(filesz > 0);

    /* Currently doing I/O using kernel space */
    uio_kinit(&header_iov, &header_ku, &header, sizeof(header), 0, UIO_READ);

    while (header_ku.uio_offset < filesz) {
        if (VOP_READ(as->swapfile, &header_ku)) {
            panic("Error during the first VOP_READ in sf_replacepage");
        }

        KASSERT((header & PAGE_FRAME) == header);
        if (header == dst_vaddr) {
            break;
        }

        header_ku.uio_offset += (PAGE_SIZE + sizeof(header));
    }

    /* A match must be found, otherwise the replacement shouldn't have started */
    KASSERT(header == dst_vaddr);
    page_offset = (off_t) header_ku.uio_offset;

    /* Read the page from the swap file and store it in a buffer */
    uio_kinit(&data_iov, &data_ku, &buf, PAGE_SIZE, page_offset + sizeof(header), UIO_READ);
    if (VOP_READ(as->swapfile, &data_ku)) {
        panic("Error during the second VOP_READ in sf_replacepage");
    }

    /* Page out of the victim */
    sf_pageout(vic_vaddr, vic_paddr, page_offset);

    /* Copy the content of the buffer in the destination frame */
    uint8_t *ptr = (uint8_t *) dst_paddr;
    for (size_t i = 0; i < PAGE_SIZE; i++) {
        ptr[i] = buf[i];
    }
}