#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

int empty(struct queue_t *q)
{
        if (q == NULL)
                return 1;
        return (q->size == 0);
}

void enqueue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: put a new process to queue [q] */
        if (q == NULL || proc == NULL)
                return;

        if (q->size >= MAX_QUEUE_SIZE)
                return; /* Queue is full, drop the request */

        q->proc[q->size] = proc;
        q->size++;
}

struct pcb_t *dequeue(struct queue_t *q)
{
        /* TODO: return a pcb whose prioprity is the highest
         * in the queue [q] and remember to remove it from q
         * */
        if (q == NULL || q->size == 0)
                return NULL;

        /* Find the item having the highest priority
         * (the smaller the value, the higher the priority).
         * Under MLQ_SCHED every PCB in one queue shares the same
         * priority, hence this degenerates into FIFO order which
         * realizes the round-robin behaviour inside one level.
         */
        int best = 0;
        int i;

        for (i = 1; i < q->size; i++) {
#ifdef MLQ_SCHED
                if (q->proc[i]->prio < q->proc[best]->prio)
                        best = i;
#else
                if (q->proc[i]->priority < q->proc[best]->priority)
                        best = i;
#endif
        }

        struct pcb_t *proc = q->proc[best];

        /* Remove the item and keep the queue order (shift left) */
        for (i = best; i < q->size - 1; i++)
                q->proc[i] = q->proc[i + 1];

        q->proc[q->size - 1] = NULL;
        q->size--;

        return proc;
}

struct pcb_t *purgequeue(struct queue_t *q, struct pcb_t *proc)
{
        /* TODO: remove a specific item from queue
         * */
        if (q == NULL || proc == NULL || q->size == 0)
                return NULL;

        int i, pos = -1;

        for (i = 0; i < q->size; i++) {
                if (q->proc[i] == proc) {
                        pos = i;
                        break;
                }
        }

        if (pos == -1)
                return NULL; /* Not found */

        for (i = pos; i < q->size - 1; i++)
                q->proc[i] = q->proc[i + 1];

        q->proc[q->size - 1] = NULL;
        q->size--;

        return proc;
}
