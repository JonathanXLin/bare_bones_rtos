#ifndef BAREBONES_RTOS_EXAMPLE_
#define BAREBONES_RTOS_EXAMPLE_

#include "BareBones_RTOS.h"

extern mutex_t my_mutex;
extern sem_t my_semaphore;

void first_task(void *args);
void second_task(void *args);
void RTOS_setup(void);

#endif