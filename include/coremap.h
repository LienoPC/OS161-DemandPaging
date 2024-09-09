#ifndef _COREMAP_H_
#define _COREMAP_H_



/*
    DATA STRUCTURE THAT KEEPS TRACK OF THE FREE PHYSICAL FRAMES
    -Simple bitmap that mantains two bits: 0 for occupied frames, 1 for free frames
*/

struct coremap_t {
    unsigned char *bitmap; // Bitmap structure
    int *allocSize; // Vector to mantain information about continuous alloc
    int nRamFrames;// Total number of ram frames
    struct spinlock coremap_lock;
    int last_frame; // Last allocated frame
};

int             isCoremapActive(void);

void            coremap_bootstrap(void);

paddr_t         getfreeframe(void);

paddr_t         getcontinuousalloc(int npages);

paddr_t         findfirsttosteal(int npages);

paddr_t         stealcontinuousalloc(int npages, paddr_t first);

void            releaseframe(paddr_t f_addr);

void            releasecontiguousalloc(paddr_t p_addr);

int             checkpercentageofuse(void);
#endif

