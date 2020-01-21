#ifndef BAREBONES_RTOS_
#define BAREBONES_RTOS_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define task_blocked						0
#define task_ready							1
#define task_blocked_semaphore	            2

typedef uint8_t task_status;

// Node data structure
typedef struct Node_t{
	uint8_t task_num;
	struct Node_t *next;
}Node_t;

// Semaphore struct
typedef struct{
	uint32_t count;
	//Wait list
	Node_t *head;
	bool block_current_task_next_preempt;
}sem_t;

//Mutex struct
typedef struct{
	bool available;
	//Owner (acquirer) of mutex, is 99 if not acquired
	uint8_t task_owner;
}mutex_t;

// Declare TCB 
typedef struct{
	//Bottom of task stack (highest address)
  uint32_t *base;
	//Temp pointer
	uint32_t *current;
	//Top of stack, could also be on top of pushed registers (lowest address)
	uint32_t *stack_pointer;
	
	uint8_t priority;
	task_status status;
	
	//Total number of timeslices to be blocked, >1 if rtosDelay called, 1 if rtosYield or rtosDelay(0) is called
	uint32_t timeslices_to_be_blocked;
	//Timeslices that have been blocked so far. Incremented in PendSV_Handler, and if >timeslices_to_be_blocked, task is blocked->activated
	uint32_t timeslices_since_blocked;
	
	sem_t *when_unblocked_decrease_semaphore;
	
	//Temporary priority promotion flag, if task inherits priority to release mutex needed by higher priority task
	bool temporary_promotion;
	bool add_in_different_priority;
	uint8_t different_priority;
}tcb_t;

//Function pointer to create task function
typedef void(*rtosTaskFunc_t)(void *args);

__asm uint32_t storeContext(void);
__asm void restoreContext(uint32_t sp);
void SysTick_Handler(void);
void mutex_init(mutex_t *s);
void mutex_acquire(mutex_t *s);
void mutex_release(mutex_t *s);
void semaphore_init(sem_t *s, uint32_t count_);
void wait(sem_t *s);
void signal(sem_t *s);
void rtosDelay(int num_timeslices);
void PendSV_Handler(void);
void add_node(uint8_t priority_, uint8_t taskNum);
void task_create(rtosTaskFunc_t taskFunction, void *R0, uint8_t priority_);
uint8_t remove_front_node(uint8_t priority);
uint8_t find_next_task();
void initialization(void);
void idle(void);

#endif