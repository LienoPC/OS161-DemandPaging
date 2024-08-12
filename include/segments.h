#ifndef _SEGMENTS_H_
#define _SEGMENTS_H_

#include <opt-paging.h>

struct segments {
    vaddr_t as_vbase1;
    size_t as_npages1;
    vaddr_t as_vbase2;
    size_t as_npages2;
    vaddr_t stackbase;
    vaddr_t stacktop;
};

#endif