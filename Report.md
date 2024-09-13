- [C1: Paging Project](#c1-paging-project)
	- [Introduzione](#introduzione)
	- [TLB](#tlb)
	- [Coremap](#coremap)
	- [Page Table](#page-table)
	- [On-Demand Paging](#on-demand-paging)
	- [Gestione dello Swap File](#gestione-dello-swap-file)
	- [Page Replacement](#page-replacement)
	- [Statistiche](#statistiche)
	- [Conclusioni](#conclusioni)


# C1: Paging Project

## Introduzione

Lo scopo di questo progetto è quello di implementare un'evoluzione della gestione di memoria virtuale (DUMBVM) che di base è utilizzata nel kernel di OS161. Nello specifico, la soluzione proposta, si basa sull'uso di una **Page Table** nella sua versione _Per-process_, implementando la possibilità di leggere le pagine dalla memoria virtuale seguendo il modello _On-Demand_, cioè quello di caricare una pagina in memoria fisica solo quando viene referenziata per la prima volta.
Si vuole gestire poi anche il caso di riempimento della memoria fisica e intervento di un algoritmo di page-replacement (nel nostro caso, FIFO), che permetta l'esecuzione di uno userprocess anche nel caso in cui la sua memoria virtuale sia più grande della memoria RAM.
L'accesso ai frame della memoria e l'allocazione di questi è gestita tramite una struttura apposita, una _coremap_, richiamata sia dal gestore della memoria kernel (kmalloc), sia dall'allocatore dei frame user.
Infine, il funzionamento del demand paging e del page replacement sarà supportato dalla gestione di uno spazio di swap sul disco, tramite la definizione di un file apposito di lunghezza fissa che accoglierà i frame sottoposti a swap-out e permetterà eventualmente lo swap-in laddove necessario.

## TLB

Ogni volta che un processo user fa accesso ad un indirizzo virtuale non contenuto in una pagina mappata in TLB (TLB miss), viene scatenata una trap (il cui punto di aggancio è la funzione `mips_trap`, definita in trap.c) che delega il compito di gestire il TLB miss alla funzione `vm_fault` (pt.c) a cui viene passato il codice del fault (`faulttype`) e l'indirizzo virtuale che ha causato il miss (`faultaddress`). L'obbiettivo ultimo di `vm_fault` è quello di inserire nella TLB una nuova entry che mappi il `faultaddress` ad un indirizzo in memoria fisica.
Le operazioni svolte a questo proposito sono:

1. Controllare il tipo di fault e, nel caso in cui il processo user abbia tentato di accedere ad un indirizzo del text segment (accessibile in sola lettura), terminarlo:

```c
switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* Text segment pages must be readonly, so this can happen */
		DEBUG(DB_VM, "VM_FAULT_READONLY\n");
		sys__exit(VM_FAULT_READONLY);
		panic("thread_exit returned (should not happen)\n");
		break;
...
```
2. Eseguire controlli sulla correttezza e la coerenza dello spazio di indirizzamento del processo user (in caso di fallimento di un `KASSERT` il kernel viene terminato)
3. Controllare il segmento (program header dell'elf file) a cui appartiene il `faultaddress`. Se l'indirizzo appartiene al text segment, il dirty bit della nuova entry della TLB sarà settato a 0, altrimenti a 1. 
4. Chiamare la funzione `pt_getframe`, a cui viene passato il `faultaddress` e che, in seguito ad una serie di chiamate a funzioni di gestione della page table (che attivano i meccanismi di demand paging e page replacement in caso di necessità), ritorna l'indirizzo del nuovo frame associato al `faultaddress`. 
5. Aggiornare la TLB tramite la funzione `tlb_loadentry`, a cui vengono passati il `faultaddress`, l'indirizzo fisico a cui associarlo (parametro `paddr`) e un flag che indica l'appartenenza di `faultaddress` al text segment del processo user (`readonly`). 

La funzione `tlb_loadentry` nel dettaglio si occupa di:
<list>
1. Trovare una entry libera nella TLB tramite la funzione `tlb_findfree` o, in caso la TLB sia piena, selezionare una vittima tramite la funzione `tlb_get_rr_victim`, che implementa un semplice algoritmo round-robin. 
2. Preparare la nuova entry da inserire nella TLB, settando i bit valid e dirty tramite le maschere `TLB_VALID` e `TLB_DIRTY`:

```c
ehi = vaddr & TLBHI_VPAGE;
	paddr = paddr & TLBLO_PPAGE;
	/* Set dirty bit only if vpage does not belong to the elf's text segment */
	if (readonly)
		elo = paddr | TLBLO_VALID;
	else
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
```
3. Inserire la nuova entry nella TLB tramite la funzione `tlb_write`.

## Coremap

Un'aspetto importante della gestione della memoria riguarda la struttura per tenere traccia dei frame liberi e, di conseguenza, la loro allocazione. La necessità di allocare memoria contigua per il kernel ci ha portato a scegliere un'implementazione basata su una semplice bitmap nella struct `coremap_t`, accompagnata da un vettore `allocSize` della stessa dimensione che tiene traccia degli intervalli di frame contigui allocati (similmente a quanto fatto da dumbvm). La bitmap, mantenuta come array di caratteri, assegna 0 ad un frame occupato e 1 ad un frame libero. La struttura di coremap contiene poi il numero totale di frame in memoria (`nRamFrames`), uno spinlock per regolarne l'accesso e `last_frame`, usato nell'algoritmo che sceglie da dove partire per la ricerca di frame da usare per allocazione contigua.
Tra le funzioni più importanti identifichiamo:
<list>
1. `paddr_t getfreeframe(void)`, che ritorna l'indirizzo fisico di un singolo frame allocato, richiamato dalla page table quando si vuole caricare una pagina in memoria
2. `paddr_t getcontinuousalloc(int npages)`, che ritorna l'indirizzo fisico del primo di n-frames allocati per il kernel (-1 se non ci sono abbastanza frame liberi)
3. Le relative funzioni di de-allocazione `releaseframe` e `releasecontiguousalloc`
E' stata modificata anche la funzione `alloc_kpages` in modo da utilizzare la coremap se già inizializzata, altrimenti utilizzando la `ram_stealmem` (solo durante la fase di startup del sistema).


## Page Table

Come già anticipato, la tabella delle pagine segue una struttura per-process, in cui ogni entry corrisponde ad una pagina logica dello spazio di indirizzamento virtuale del processo. La modifica alla memoria virtuale proposta necessita di cambiare anche parti delle funzioni di addresspace (contenute in `dumbvm.c` nell'implementazione originale, ma che per questo progetto sono state spostate in `addrspace.c`), dato che la struttura dello spazio di indirizzamento `struct addrspace` è stata completamente rivista. La page table è gestita quindi all'interno del file `pt.c` usando la struct definita in `addrspace.h` composta dai seguenti campi:
1. `paddr_t *frames`: vettore che associa ad ogni entry della tabella un indirizzo fisico, corrispondente al, se presente, frame associato alla pagina logica indicizzata
2. `unsigned char *control_bits`: vettore che associa ad ogni entry i bit di controllo, quali:
    - S, swap bit, indica se l'entry deve essere gestita usando lo spazio di swap 
    - V, valid bit, indica se l'entry è valida, cioè se il frame è presente in memoria. 
    - E' stata predisposta anche la possibilità di usare il dirty bit e il reference bit, ma a causa della mancanza di supporto hardware associato in questa versione della memoria virtuale non sono utilizzati.
3. `int n_entry`: inizializzata alla creazione della PT, indica il numero di entry nella page table e corrisponde al numero di pagine logiche del processo in esecuzione
4. `vnode *swapfile`: mantiene un riferimento al vnode dello swapfile, che viene tenuto sempre aperto durante l'esecuzione del processo
5. `segments segs`: variabile del tipo `struct segments` definita in `segments.h`, che mantiene tutte le informazioni riguardo lo spazio di memoria virtuale del processo in esecuzione e il suo file elf; è composta dai seguenti campi:
   - `vnode *progelf`: riferimento al vnode dell'eseguibile del processo in esecuzione
   - `text_ph e data_ph`: program headers delle sezioni usati per la lettura delle pagine nel demand paging (in questa versione si supporta unicamente la presenza di due sezioni più lo stack)
   - `as_vbase1 e as_vbase2`: primi indirizzi virtuali delle due sezioni della memoria user, usati per la mappatura con gli indici della page table
   - `as_npages1 e as_npages2`: numero di pagine delle due sezioni
   - `as_stackvbase, as_stackvtop`: rispettivamente l'ultimo indirizzo virtuale valido dello stack e il primo indirizzo valido (lo stack cresce dall'alto verso il basso). In questa versione (così come in dumbvm) usiamo uno stack a lunghezza fissa
   - `as_stackptbase`: primo indirizzo virtuale dello stack mappato 'in basso' nella page table
6. `pt_fifo_t *page_queue`: campo contenente la coda FIFO usata nel page replacement

A parte i principali metodi di accesso e utilizzo della page table, è interessante dare un'occhiata al processo di traduzione da indirizzo virtuale del processo a indice della page table, gestito dai metodi statici `static int get_pt_index(struct addrspace *as, vaddr_t vaddr)` e `static vaddr_t get_vaddr_from_index(struct addrspace *as, int index)`:

```c
static int
get_pt_index(struct addrspace *as, vaddr_t vaddr){
	int index = -1;
	if(vaddr >= as->segs.as_vbase1 && vaddr < (as->segs.as_vbase1 + PAGE_SIZE*as->segs.as_npages1)) {
		index = (vaddr - as->segs.as_vbase1)/PAGE_SIZE;
	}
	else if (vaddr >= as->segs.as_vbase2 && vaddr < (as->segs.as_vbase2 + PAGE_SIZE*as->segs.as_npages2)) {
		index = (vaddr - as->segs.as_vbase2)/PAGE_SIZE + as->segs.as_npages1;
	}else if (vaddr >= as->segs.as_stackvbase && vaddr < as->segs.as_stackvtop) {
		index = (as->segs.as_stackptbase + (as->segs.as_stackvtop - vaddr))/PAGE_SIZE;
	}else{
		// Invalid vaddr
		index = -1;
	}
	return index;
}

```
Si assegnano cioè progressivamente le prime `as_npages1` entry della pt al primo segmento, a partire poi dall'indice `as_npages1` vengono mappate le pagine del secondo segmento e infine quelle dello stack, dove `as_stackptbase` viene definito nella funzione `as_define_stack` come indirizzo virtuale subito successivo alla somma di as_npages1 e 2.

```c
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{	
	#if OPT_PAGING
		as->segs.as_stackvbase = USERSTACK - PAGING_STACKPAGES * PAGE_SIZE;
		as->segs.as_stackvtop = USERSTACK;

		if (as->segs.as_vbase1 != 0 && as->segs.as_vbase2 != 0){
			/* Already defined the other regions, I can map the stack in the PT */
			as->segs.as_stackptbase = as->segs.as_npages1*PAGE_SIZE + as->segs.as_npages2*PAGE_SIZE;
		}

		/* Initial user-level stack pointer */
		*stackptr = as->segs.as_stackvtop;
	#else
		KASSERT(as->as_stackpbase != 0);

		*stackptr = USERSTACK;
	#endif

	return 0;
}

```

## On-Demand Paging

L'implementazione della page table è stata associata ad una gestione della memoria del tipo "On-Demand", per cui le pagine dello spazio di indirizzamento logico di un processo sono caricate in memoria solo dopo essere state referenziate dal programma. Questo vuol dire che all'inizio la page table non ha frame fisici in ram, per cui la prima istruzione parte con un tlb miss (ricevuto dalla funzione `vm_fault`) che segue, quindi, il seguente iter:
1. Identifica il tipo di fault, interrompendo il processo nel caso di accesso in scrittura a pagine _read-only_
2. Verifica a quale segmento del processo il faultaddr appartiene, per settare correttamente il read-only bit nell'inserimento della nuova entry nella tlb
3. Viene chiamata la funzione `paddr_t pt_getframe(vaddr_t addr)` che, dato l'indirizzo virtuale di una pagina, ritorna il suo corrispettivo indirizzo fisico in memoria
4. La funzione verifica quindi se l'entry associata al faultaddress sia valida, ritornando direttamente l'indirizzo al frame associato, altrimenti chiama la funzione di page fault `paddr_t pt_pagefault(int index)`, dato che il frame deve essere caricato in memoria
5. Nel page fault si verifica prima di tutto se sia necessario far partire il page replacement: abbiamo deciso di settare una percentuale massima di utilizzo della memoria ram da parte dei frame del processo user, in modo da lasciare sempre disponibile una parte di memoria per il kernel. Se la ram occupata supera il threshold specificato, inizia il page replacement, richiamando la funzione `paddr_t pt_page_replacement(int dst_index)`, che sarà descritta nei paragrafi successivi. Nel caso invece in cui la memoria non sia ancora saturata, si richiama la funzione `getframe` della coremap, che ritorna l'indirizzo fisico del frame su cui sarà caricata la pagina a cui si sta cercando di accedere, che può essere caricata
	1. Dallo SWAPFILE, nel caso in cui sia settato lo SWAP BIT, tramite l'uso delle funzioni di gestione dello spazio di swap
 	2. Dal file ELF, nel caso in cui lo SWAP BIT non sia settato.
 	3. Infine vi è il caso di allocazione di una pagina vuota su cui il processo vuole scrivere, che si verifica quando il programma cerca di accedere per la prima volta ad una pagina dello stack. In questo caso settiamo subito lo SWAP BIT

Considerata la mancanza di supporto hardware nella TLB per l'uso del modify bit nella page table, non possiamo gestire dinamicamente la scrittura su SWAPFILE unicamente delle pagine che sono state modificate, per questo motivo ogni volta che si verifica un page-out (che in questo caso può verificarsi unicamente per page replacement) viene settato lo swap bit ed eseguito, nel pratico, lo swap-out della vittima (anche se appunto, questa non è stata mai modificata e potrebbe essere, teoricamente, ri-caricata direttamente dal file elf)

## Gestione del File ELF

Componente fondamentale del demand paging è il fatto di non caricare l'intero spazio di indirizzamento del processo quando questo viene avviato, ma solo, appunto, on-demand. Questo ha richiesto fare dei piccoli cambiamenti nella sequenza di funzioni chiamate per lo startup di un processo user, soprattutto nella `load_elf`, che nella versione usata da dumbvm carica tutti i segment in memoria chiamando la funzione load_segment: nella nostra versione, invece, setuppiamo il contenuto della `struct segments` contenuta all'interno dell'addrspace del processo e accediamo successivamente al file elf per leggere le singole pagine. La lettura di una pagina si basa sulla seguente sequenza di chiamate:
1.`get_elf_offset`: funzione che calcola l'offset fisico all'interno del file elf per la lettura. In base al segmento al quale appartiene l'indirizzo virtuale della pagina a cui vogliamo accedere, sommiamo l'offset del segmento con la posizione relativa alla vbase del segmento stesso.
2.`load_from_elf`: a partire dall'offset ricevuto, inizializza un uio di kernel, finalizzato tramite una `VOP_READ`, per appunto caricare la pagina dal file elf all'indirizzo fisico del frame allocato
In questo modo gestiamo il caricamento dinamico delle pagine logiche dal file elf alla memoria ram.
## Gestione dello Swap File

Il file SWAPFILE nella root directory costituisce lo spazio di swap del sistema.
Nel file swapfile.c (e header swapfile.h) sono definite le funzioni che permettono di interfacciarsi con lo swap file, offrendo il supporto per le operazioni di page out, page in e, di conseguenza, page replacement. 
Illustriamo qui come viene gestito lo swap file perché sarà utile per comprendere l'implementazione del page replacement. 

La dimensione massima dello spazio di swap è facilmente impostabile da swapfile.h tramite la costante `MAX_SWAP_SPACE`.

Tutte le operazioni di lettura e scrittura su swap file vengono gestite tramite `struct iovec` e `struct uio`, inizializzate per gestire l'I/O in spazio di kernel (tramite la funzione `uio_kinit`).
Lettura e scrittura vengono rispettivamente eseguite mediante le macro `VOP_READ` e `VOP_WRITE`.

Prima di procedere alla descrizione delle funzioni offerte da swapfile.c è opportuno notare che la dimensione di una pagina scritta sullo swap file durante un page out sarà di 4100 bytes invece di 4096 (`PAGE_SIZE`), in quanto il contenuto della pagina viene preceduto dal suo indirizzo virtuale (di tipo `vaddr_t`, 4 bytes), al fine di identificare la pagina durante un page in.

Le funzioni definite per il supporto alle operazioni su swap file sono:
1. `off_t sf_getsize(void)`: permette di ottenere la dimensione attuale dello swap file. Utilizza la macro `VOP_STAT` per ottenere statistiche relative al file e da esse estrae e ritorna la dimensione (campo `st_size` della `struct stat`).
2. `bool is_sf_full(void)`: ritorna `true` se lo spazio di swap è stato saturato, `false` altrimenti. 
3. `bool sf_can_fit_page(void)`: ritorna `true` se lo swap file può accogliere almeno un'altra pagina (4096 + 4 bytes), altrimenti ritorna `false`.
4. `int sf_pagin(vaddr_t vaddr, paddr_t paddr)`: legge la pagina identificata dall'indirizzo virtuale `vaddr` e la scrive all'indirizzo fisico `paddr`. Ritorna codici di errore specifici se lo swapfile è vuoto o se la pagina non viene trovata.
5. `void sf_pageout(vaddr_t vaddr, paddr_t paddr, off_t offset)`: scrive il contenuto del frame all'indirizzo fisico `paddr` (il cui corrispettivo indirizzo virtuale è `vaddr`, che costituisce i 4 bytes scritti prima del contenuto della pagina) su swap file. `offset` serve per distinguere tra un'operazione di append e una di sovrascrittura, motivo per cui deve essere allineato a 4100 bytes. In caso di append, il kernel va in panic se non c'è sufficiente spazio di swap libero per eseguire l'operazione.
6. `void sf_replacepage(vaddr_t vic_vaddr, vaddr_t dst_vaddr, paddr_t vic_paddr)`: offre il supporto per il page replacement su swap file. Come prima cosa, legge da swap file la pagina identificata dall'indirizzo virtuale `dst_vaddr` (fallendo un `KASSERT` nel caso in cui non venga trovata) e la salva in un buffer temporaneo. Dopo di che, chiama `sf_pageout` per fare page out della pagina selezionata come vittima dal page replacement, con indirizzo virtuale `vic_vaddr` e fisico `vic_paddr`. Infine, copia il contenuto del buffer temporaneo nel frame destinazione all'indirizzo `dst_frame`, dopo averlo azzerato. È importante notare che questa funzione non esegue un append su swap file, ma scrive la vittima allo stesso offset della destinazione (vedi il punto 5 per maggiori dettagli). Per questo motivo, dopo la sua esecuzione la dimensione del file sarà inalterata.

## Page Replacement
Il page replacement viene gestito mediante un algoritmo FIFO. La coda usata dall'algoritmo è `pt_fifo_t`, definita come ADT insieme alla sua interfaccia nel file pt_fifo.h e la cui implementazione si trova nel file pt_fifo.c.
La `struct addrspace` contiene il campo `pt_fifo_t *page_queue`, utilizzato per tenere traccia delle pagine mappate nella page table.
Ci limiteremo ad elencare le funzioni di interfaccia della struttura dati senza soffermarci sui particolari della sua implementazione, non presentando particolari variazioni rispetto alle classiche implementazioni che si trovano in letteratura:
- `pt_fifo_t *pt_fifo_init(void)`: inizializza la coda.
- `void pt_fifo_push_back(pt_fifo_t *fifo, int pt_index)`: aggiunge un elemento al fondo della coda. 
- `int pt_fifo_pop_front(pt_fifo_t *fifo)`: rimuove l'elemento in testa alla coda.
- `void pt_fifo_pop(pt_fifo_t *fifo, int pt_index)`: rimuove l'elemento specificato dalla coda. `pt_index` non è la posizione dell'elemento in coda, ma il valore del campo `pt_index` di `node_t` (elemento della coda) da ricercare e rimuovere.  
- `void pt_fifo_free(pt_fifo_t *fifo)`: libera la memoria allocata per la coda.

Contestualmente all'aggiunta e alla rimozione di entry nella page table, viene rispettivamente inserito o rimosso dalla coda l'indice di tale entry, in modo tale che la `page_queue` tenga traccia delle sole pagine attualmente presenti nella page table, mediante l'indice a cui si trovano.

La funzione `static paddr_t pt_page_replacement(int dst_index)`, definita in pt.c e a cui viene passato l'indice associato all'indirizzo virtuale della pagina da caricare in memoria (vedi [Page Table](#page-table) per i dettagli sulla traduzione indirizzo virtuale - indice in PT) si occupa della gestione del page replacement:
1. Se `as->page_queue` non è vuota, rimuove l'elemento in testa (`pt_fifo_pop_front`), ottenendone l'indice nella page table: la pagina corrispondente sarà la vittima da rimpiazzare. Se la coda è vuota, va in `panic("Out of memory")`.   
2. Chiama la funzione `pt_invalid_entry` passando l'indice della vittima nella page table. Tale funzione si occupa di invalidare l'entry sia nella page table che nella TLB e di settarne lo swap bit:
```c
static void
pt_invalid_entry(struct addrspace *as, int index){
	/* Perform the page-out operations */
	as->control_bits[index] &= ~PT_VALID_BIT;
	if(!(as->control_bits[index] & PT_SWAP_BIT)) {
		as->control_bits[index] |= PT_SWAP_BIT;
	}
	tlb_invalid_entry(get_vaddr_from_index(as, index));
}
```
3. Se lo swap bit della pagina da caricare in memoria è settato (ovvero la pagina da caricare è stata scritta sullo swap file), chiama la funzione `sf_replacepage` (vedi [Gestione dello Swap File](#gestione-dello-swap-file) per maggiori dettagli), terminando così la procedura di page replacement. Altrimenti, la pagina va letta e caricata dal file elf: esegue prima page out della vittima tramite `sf_pageout` e poi carica la pagina dall'elf.
4. Ritorna l'indirizzo fisico del frame in cui è stata caricata la pagina.

L'aggiornamento della page table in seguito al page replacement viene finalizzato al ritorno di `pt_pagefault` in `pt_getframe`:
```c
/* PAGE FAULT: frame not in memory */
		paddr = pt_pagefault(index);
		/* update the PT */
		as->frames[index] = paddr;
		/* now the pt entry is valid */ 
		as->control_bits[index] |= PT_VALID_BIT;
		pt_fifo_push_back(as->page_queue, index);
```
Notare l'inserimento in coda dell'indice della nuova entry nella page table in `as->page_queue`.

## Statistiche

La raccolta delle statistiche è l'ultimo elemento del nostro lavoro, basata semplicemente sulla definizione di una struct statica nel file `vmstats.h` contenente tutti i campi (in questo caso, contatori) usati per tenere traccia di determinati eventi del kernel, così come indicato sui requisiti del progetto. L'accesso dall'esterno è gestito tramite le singole funzioni usate per incrementare i contatori della struttura:
```c
struct vmstats{

    unsigned int tlb_faults;
    unsigned int tlb_faults_with_free;
    unsigned int tlb_faults_with_replace;
    unsigned int tlb_invalidations;
    unsigned int tlb_reloads;
    unsigned int pf_zeroed;
    unsigned int pf_disk;
    unsigned int pf_from_elf;
    unsigned int pf_from_swap;
    unsigned int swapfile_writes;
};
```
La struttura è inizializzata insieme al coremap bootstrap, mentre la stampa delle statistiche e la verifica della loro validità è stata inserita nella funzione `vm_shutdown`, richiamata allo spegnimento di OS161.
## Conclusioni

Per concludere, le strutture introdotte mancano di alcuni accorgimenti, come ad esempio l'utilizzo di primitive di sincronizzazione per la page table, dato che si eseguono sempre, in questa versione di OS161, processi utente single-threaded. Nonostante l'introduzione della nuova memoria virtuale vada a peggiorare le prestazioni per i programmi utente più semplici, essa permette di gestire il caso di processo utente con memoria virtuale più grande della memoria ram e di garantire, in teoria, la mancanza di out of memory.
