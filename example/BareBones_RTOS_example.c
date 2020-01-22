#include "BareBones_RTOS_Example.h"

/* Declare mutexes and semaphores */
mutex_t my_mutex;
sem_t my_semaphore;

/* Task function 1 */
void first_task(void *args) {
	while (1)
	{
		/* User code */

		/* Non-blocking delay in user code, time units of ms */
		rtosDelay(3);

		/* Acquire priority-inheritance mutex */
		mutex_acquire(&my_mutex);

		/* Release priority-inheritance mutex */
		mutex_release(&my_mutex);
	}
}

/* Task function 2 */
void second_task(void *args) {
	while (1)
	{
		/* User code */

		/* Non-blocking delay in user code, time units of ms */
		rtosDelay(3);

		/* Wait for semaphore */
		wait(&my_semaphore);

		/* Signal semaphore */
		signal(&my_semaphore);
	}
}

/* Required RTOS setup, must run initially */
void RTOS_setup(void) {
	/* Initialize mutexes and semaphores and set initial count */
	mutex_init(&my_mutex);
	semaphore_init(&my_semaphore, 1);

	/* Initialize RTOS */
	initialization();
	
	/* Declare task */
	rtosTaskFunc_t my_first_task = &first_task;
	/* Create task and initialize with task function and priority*/
	task_create(my_first_task, NULL, 3);
	
	/* Declare task */
	rtosTaskFunc_t my_second_task = &second_task;
	/* Create task and initialize with task function and priority*/
	task_create(my_second_task, NULL, 2);
	
	/* Idle busy loop and Systick_Handler configuration */
	idle();
}