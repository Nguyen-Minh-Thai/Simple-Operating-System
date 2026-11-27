#ifndef SCHED_H
#define SCHED_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MLQ_SCHED
#define MLQ_SCHED
#endif

#define MAX_PRIO 140

int queue_empty(void);

void init_scheduler(void);
void finish_scheduler(void);

/* Get the next process from ready queue */
struct pcb_t * get_proc(void);

/* Put a process back to run queue */
void put_proc(struct pcb_t * proc);

/* Add a new process to ready queue */
void add_proc(struct pcb_t * proc);

/* Remove a finished process from the running list */
void remove_proc(struct pcb_t * proc);

/* Find a running process by its PID (dual-space syscall support:
 * kernel code looks up the PCB from the PID instead of receiving
 * the user PCB pointer directly) */
struct pcb_t * find_running_proc(uint32_t pid);

#ifdef __cplusplus
}
#endif

#endif
