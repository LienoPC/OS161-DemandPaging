#include <types.h>
#include <lib.h>  
#include <pt_fifo.h>

struct node {
    int pt_index;
    struct node *next;
    struct node *prev;
};

struct pt_fifo {
    node_t *head;
    node_t *tail;
};

pt_fifo_t *pt_fifo_init(void) {
    pt_fifo_t *fifo = (pt_fifo_t *) kmalloc(sizeof(pt_fifo_t));
    if(fifo == NULL) {
        panic("Error during FIFO init");
    }
    fifo->head = NULL;
    fifo->tail = NULL;
    return fifo;
}

void pt_fifo_push_back(pt_fifo_t *fifo, int pt_index) {
    if(fifo == NULL) {
        panic("FIFO is NULL in pt_fifo_push_back");
    }
    if((fifo->tail == NULL && fifo->head != NULL) || (fifo->tail != NULL && fifo->head == NULL)) {
        panic("Either the FIFO's head or tail is NULL, but not both.");
    }

    node_t *node = (node_t *) kmalloc(sizeof(node_t));
    if(node == NULL) {
        panic("Unable to allocate node_t in pt_fifo_push_back");
    }
    node->pt_index = pt_index;
    node->next = NULL;

    if(fifo->tail == NULL) {
        node->prev = NULL;
        fifo->tail = node;
        fifo->head = node;
    }
    else {
        node->prev = fifo->tail;
        fifo->tail->next = node;
        fifo->tail = node;
    }
}

int pt_fifo_pop_front(pt_fifo_t *fifo) {
    int retval;
    node_t *temp;

    if(fifo == NULL) {
        panic("FIFO is NULL in pt_fifo_pop_front");
    }

    if(fifo->head == NULL) {
        /* There's nothing to pop */
        return -1;
    }
    else {
        retval = fifo->head->pt_index;
        temp = fifo->head;

        /* Only one node in queue */
        if(fifo->head->next == NULL) {
            if(fifo->tail != fifo->head) {
                panic("pt_fifo's head has no successor, but tail != head (pt_fifo_pop_front)");
            }
            fifo->head = NULL;
            fifo->tail = NULL;
        }
        /* Multiple nodes in queue */
        else {
            fifo->head->next->prev = NULL;
            fifo->head = fifo->head->next;
        }
        kfree(temp);
    }

    return retval;
}

void pt_fifo_pop(pt_fifo_t *fifo, int pt_index) {
    node_t *temp = NULL;

    if(fifo == NULL) {
        panic("FIFO is NULL in pt_fifo_pop");
    }
    if((fifo->tail == NULL && fifo->head != NULL) || (fifo->tail != NULL && fifo->head == NULL)) {
        panic("Either the FIFO's head or tail is NULL, but not both.");
    }

    if(fifo->head == fifo->tail) {
        /* If head and tail point to the same node, just pop it */
        if(fifo->head != NULL && fifo->head->pt_index == pt_index) {
            temp = fifo->head;
            fifo->head = NULL;
            fifo->tail = NULL;
        }
    }
    else {
        for (node_t *curr = fifo->head; curr != NULL; curr = curr->next) {
            if (curr->pt_index == pt_index) {
                temp = curr;
                /* pop front */
                if(curr == fifo->head) {
                    curr->next->prev = NULL;
                    fifo->head = curr->next;
                }
                /* pop back */
                else if(curr == fifo->tail) {
                    curr->prev->next = NULL;
                    fifo->tail = curr->prev;
                }
                /* pop body */
                else {
                    curr->prev->next = curr->next;
                    curr->next->prev = curr->prev;
                }
            }
        }
    }

    if(temp == NULL) {
        panic("Searched pt_index not found in fifo queue (pt_fifo_pop)");
    }
    kfree(temp);
}

void pt_fifo_free(pt_fifo_t *fifo) {
    node_t *curr, *temp;

    if(fifo == NULL) {
        panic("FIFO is NULL in pt_fifo_pop");
    }
    if((fifo->tail == NULL && fifo->head != NULL) || (fifo->tail != NULL && fifo->head == NULL)) {
        panic("Either the FIFO's head or tail is NULL, but not both.");
    }

    if (fifo->head == fifo->tail) {
        if(fifo->head != NULL) {
            kfree(fifo->head);
        }
    }
    else {
        curr = fifo->tail;
        /* Tail inaccessible from now on */
        while (curr != NULL) {
            temp = curr->prev;
            kfree(curr);
            curr = temp;
        }
    }
    kfree(fifo);
}