#ifndef PT_FIFO_H
#define PT_FIFO_H

typedef struct node node_t;
typedef struct pt_fifo pt_fifo_t;

pt_fifo_t *fifo_init         (void);
void       fifo_push_back    (pt_fifo_t *fifo, int pt_index);
int        fifo_pop_front    (pt_fifo_t *fifo);
void       fifo_pop          (pt_fifo_t *fifo, int pt_index);
void       fifo_free         (pt_fifo_t *fifo);

#endif