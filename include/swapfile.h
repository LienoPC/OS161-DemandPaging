#ifndef _SWAPFILE_H_
#define _SWAPFILE_H_

#include <opt-paging.h>

#define MAX_SWAP_SPACE 9*1024*1024 /* 9MB of swap space*/

bool  is_sf_full      (void);
bool  sf_can_fit_page (void);
off_t sf_getsize      (void);
int   sf_pagein      (vaddr_t vaddr, paddr_t paddr);
void  sf_pageout     (vaddr_t vaddr, paddr_t paddr);
// TODO: Funzione per l'effettivo replacement, che fa swap out di una pagina nella posizione di quella di cui fa swap in 

#endif