#ifndef PT_FIFO_H
#define PT_FIFO_H

typedef struct node node_t;
typedef struct pt_fifo pt_fifo_t;

pt_fifo_t *pt_fifo_init         (void);
void       pt_fifo_push_back    (pt_fifo_t *fifo, int pt_index);
int        pt_fifo_pop_front    (pt_fifo_t *fifo);
void       pt_fifo_pop          (pt_fifo_t *fifo, int pt_index);
void       pt_fifo_free         (pt_fifo_t *fifo);

#endif