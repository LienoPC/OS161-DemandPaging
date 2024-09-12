#ifndef _SEGMENTS_H_
#define _SEGMENTS_H_

#include <opt-paging.h>

struct segments {
    struct vnode *progelf;
    Elf_Phdr text_ph;
    Elf_Phdr data_ph;
    vaddr_t as_vbase1;
    size_t as_npages1;
    vaddr_t as_vbase2;
    size_t as_npages2;
    vaddr_t as_stackvbase;
    vaddr_t as_stackvtop;
    vaddr_t as_stackptbase;
};



#endif