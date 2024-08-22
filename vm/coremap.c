#include <types.h>
#include <mips/vm.h>
#include <vm.h>
#include <spinlock.h>
#include <lib.h>
#include <coremap.h>


//#include <current.h>





static int bootstrap_completed = 0; // Check if the coremap is active and choose how to allocate kernel memory
static struct spinlock bootstrap_lock = SPINLOCK_INITIALIZER;
static struct coremap_t *coremap;


int
isCoremapActive(){
    int active;
    spinlock_acquire(&bootstrap_lock);
    active = bootstrap_completed;
    spinlock_release(&bootstrap_lock);
    return active;
}


             



void            
coremap_bootstrap(void){
    int bootstrap_pages = 0;
    int i;
    /* Alloc coremap struct */
    kprintf("Allocazione bootstrap");

    coremap = kmalloc(sizeof(*coremap));
    if (coremap == NULL){
        /*problemi nell'allocazione del bootstrap*/
        kprintf("Problemi allocazione bootstrap");
    }
    coremap->nRamFrames = ((int)ram_getsize())/PAGE_SIZE;
    coremap->bitmap = kmalloc(sizeof(unsigned char) * coremap->nRamFrames);
    if (coremap->bitmap == NULL){
         /*problemi nell'allocazione del bootstrap*/
        kprintf("Problemi allocazione bitmap");

    }
    coremap->allocSize = kmalloc(sizeof(int) *  coremap->nRamFrames);
    kprintf("AllocSize: %p", coremap->allocSize);
    if (coremap->allocSize == NULL){
         /*problemi nell'allocazione del bootstrap*/
        kprintf("Problemi allocazione allocSize");

    }
    for(i = 0; i < coremap->nRamFrames; i++){
        coremap->bitmap[i] = 1;
        coremap->allocSize[i] = 0;
    }
    /* Calculate the number of pages used by the coremap and occupied using ram_stealmem*/
    bootstrap_pages = ram_getfirstfreeaddr()/PAGE_SIZE;
    /* AGGIUNGERE RIFERIMENTO AD ALLOCSIZE COME BLOCCO INTERO */
    for(i = 0; i < bootstrap_pages; i++){
        coremap->bitmap[i] = 0;
    }
    spinlock_init(&coremap->coremap_lock);

    spinlock_acquire(&bootstrap_lock);
    bootstrap_completed = 1;
    spinlock_release(&bootstrap_lock);

}


/* Returns the physical address of a single frame and allocates it*/

paddr_t getfreeframe() {
    /* Linear search on the bitmap to find a free frame */
    paddr_t addr;
    int found = -1;
    int i;
    spinlock_acquire(&coremap->coremap_lock);
    for(i = coremap->last_frame; i < coremap->nRamFrames; i++){
        if (coremap->bitmap[i] == 1){
            found = i;
            coremap->bitmap[found] = (unsigned char) 0;
            break;
        }
    }
    if (found == -1 && coremap->last_frame > 0){
        for(i = 0; i < coremap->last_frame; i++){
            if (coremap->bitmap[i] == 1){
                found = i;
                coremap->bitmap[found] = (unsigned char)0;
                break;
            }
        }
    }
    spinlock_release(&coremap->coremap_lock);
    addr = (paddr_t) found*PAGE_SIZE;
    return addr;
}


/* Tries to get npages continuous pages using the bitmap */

paddr_t getcontinuousalloc(int npages){

    paddr_t addr;
    int i,first,found;
    spinlock_acquire(&coremap->coremap_lock);
    for (i=0,first=found=-1; i<coremap->nRamFrames; i++) {
        if (coremap->bitmap[i] == 1) {
            if (i==0 || !coremap->bitmap[i-1]) 
                first = i; /* set first free in an interval */   
            if (i-first+1 >= npages) {
                found = first;
                break;
            }
        }
    }
	
  if (found>=0) {
    for (i=found; i<found+npages; i++) {
      coremap->bitmap[i] = (unsigned char)0;
    }
    coremap->allocSize[found] = npages;
    addr = (paddr_t) found*PAGE_SIZE;
  }
  else {
    addr = (paddr_t)NULL;
  }

  spinlock_release(&coremap->coremap_lock);

  return addr;
}

/* If the kernel needs npages for continuos allocation (and they aren't available through getcontinuousalloc)
    npages are freed from the user space and the corresponding page table entries are swapped-out
*/

paddr_t stealcontinuousalloc(int npages, paddr_t first){

    /* Serve aggiungere swap out su SWAPFILE? */
    
    int i;
    int f_first;
    f_first = first/PAGE_SIZE;
    KASSERT(f_first > 0 && f_first < coremap->nRamFrames);
    spinlock_acquire(&coremap->coremap_lock);
    for(i = f_first; i < f_first + npages; i++){
        coremap->bitmap[i] = (unsigned char) 0;
    }
    coremap->allocSize[f_first] = npages;
    spinlock_release(&coremap->coremap_lock);
    return first;

}

void           
releaseframe(paddr_t p_addr){
    int f_number;
    f_number = p_addr/PAGE_SIZE;
    KASSERT(f_number > 0 && f_number < coremap->nRamFrames);
    spinlock_acquire(&coremap->coremap_lock);
    coremap->bitmap[f_number] = (unsigned char) 1;
    spinlock_release(&coremap->coremap_lock);
}


void
releasecontiguousalloc(paddr_t p_addr, int npages){
    int f_number, i;
    f_number = p_addr/PAGE_SIZE;
    KASSERT(f_number > 0 && f_number + npages < coremap->nRamFrames);
    spinlock_acquire(&coremap->coremap_lock);
    for(i = f_number; i < f_number + npages; i++){
        coremap->bitmap[f_number] = (unsigned char) 1;
    }
    coremap->allocSize[f_number] = 0;
    spinlock_release(&coremap->coremap_lock);

}