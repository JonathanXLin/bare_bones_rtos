
void first_task(void *args) {
	while (1)
	{
		/* User code */

		/* Non-blocking delay in user code, time units of ms */
		rtosDelay(3);

		/* Acquire priority-inheritance mutex */
		mutex_acquire(&mutex_lock);

		/* Release priority-inheritance mutex */
		mutex_release(&mutex_lock);
	}
}

void second_task(void *args) {
	while (1)
	{
		printf("TASK 2 (priority 3)\n");
		rtosDelay(3);
	}
}

/* Declare mutexes and semaphores */
mutex_t my_mutex;
sem_t my_semaphore;

int main(void) {

	/* Initialize mutexes and semaphores */
	mutex_init(&my_mutex);
	semaphore_init(&lock1, 1);

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