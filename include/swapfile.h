#ifndef _SWAPFILE_H_
#define _SWAPFILE_H_

#include <opt-paging.h>

#define MAX_SWAP_SPACE 9*1024*1024 /* 9MB of swap space*/

bool  is_sf_full      (void);
bool  sf_can_fit_page (void);
off_t sf_getsize      (void);
int   sf_pagein       (vaddr_t vaddr, paddr_t paddr);
void  sf_pageout      (vaddr_t vaddr, paddr_t paddr, off_t offset);
void  sf_replacepage  (vaddr_t vic_vaddr, vaddr_t dst_vaddr, paddr_t vic_paddr);

#endif