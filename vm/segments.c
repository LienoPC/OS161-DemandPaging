#include <types.h>
#include <lib.h>
#include <uio.h>
#include <proc.h>
#include <current.h>
#include <vnode.h>
#include <elf.h>
#include <addrspace.h>
#include <segments.h>

/*
    Loads a page from the elf file, usign the elf header and the program header of the segment stored in the addrspace structure
    Given the offset of the segment, sums the offset of the virtual page we want to gather and reads PAGE_SIZE
*/


/* AGGIUNGERE IL VNODE */


/* 
     * Giulio: potrebbe essere necessario aggiungere eh.e_phentsize 1 volta se carico da text seg
     * o 2 volte se carico da data seg, come viene fatto nella load_elf nel for di set up dell'addrspace -> lo faccio nella funzione chiamante
     */

 /*
     * Giulio: importante considerare una &u e non una &ku (se non sbaglio) per il caricamento della pagina, dal
     * momento che viene caricata in un buffer user e non kernel (come la load_segment in loadelf.c). Questo distingue
     * il comportamento per considerare il campo uio_space dove dovremmo caricare l'addrspace user
     */


int
load_from_elf(paddr_t paddr, struct addrspace *as, off_t offset){
	
    struct iovec iov;
    struct uio frame_ku;
    
    uio_kinit(&iov, &frame_ku, (void *) paddr, PAGE_SIZE, offset, UIO_READ);
    if (VOP_READ(&as->elffile, &frame_ku)){
        panic("Error during the VOP_READ in load_from_elf");
    }
    return 0;
    
}



