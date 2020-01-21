#include <LPC17xx.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

//Timeslice frequency Hz, CURRENTLY UNUSED
const int timeslice_frequency = 1;

// Define task status macros
typedef uint8_t task_status;
#define task_ready							1
#define task_blocked						0
#define task_blocked_semaphore	2//Need this to tell scheduler to disregard variables which keep track of how long delay is

//Function declarations
uint32_t storeContext(void);
void restoreContext(uint32_t sp);
uint8_t find_next_task();
uint8_t remove_front_node(uint8_t priority);
void add_node(uint8_t priority_, uint8_t taskNum);

// Node data structure
typedef struct Node_t{
	uint8_t task_num;
	struct Node_t *next;
}Node_t;

// System clock and pre-empt
uint32_t msTicks = 0;
void SysTick_Handler(void) {
	// When context switch required
	if (!(msTicks % 500)) {
		// Write 1 to PENDSVSET bit of ICSR
		SCB->ICSR |= (1 << 28);
	}
	msTicks++;
}

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

mutex_t mutex_lock;

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

tcb_t TCBS[6];
//Created tasks is number of total tasks
uint8_t createdTasks;
//Numtasks is number of active tasks
uint8_t numTasks;
uint8_t currTask;
uint8_t next_task;

Node_t *schedule_array[6];

void mutex_init(mutex_t *s, uint32_t count_) {
	(*s).available = true;
	(*s).task_owner = 99;
}
void mutex_acquire(mutex_t *s) {
	__disable_irq();
	
	while(!((*s).available)) {
		//Check if mutex is owned (acquired) by owner of lower priority
		if (TCBS[(*s).task_owner].priority < TCBS[currTask].priority)
		{
			printf("I AM EXPLICITY INVOKING PENDSV HANDLER BECAUSE I HAVE TEMPORARILY PROMOTED LOWER PRIORITY TASK <%d> TO HIGHER PRIORITY OF CURRENT TASK <%d>====================================", (*s).task_owner, currTask);
			TCBS[(*s).task_owner].different_priority = TCBS[(*s).task_owner].priority;
			
			//Find lower priority owner
			Node_t *target = NULL;
			//Case 1, only 1 item at beginning
			if (((*schedule_array[TCBS[(*s).task_owner].priority]).task_num == (*s).task_owner) && ((*schedule_array[TCBS[(*s).task_owner].priority]).next == NULL))
			{
				printf("case 1\n");
				target = schedule_array[TCBS[(*s).task_owner].priority];
				schedule_array[TCBS[(*s).task_owner].priority] = NULL;
			}
			else if (((*schedule_array[TCBS[(*s).task_owner].priority]).task_num == (*s).task_owner) && ((*schedule_array[TCBS[(*s).task_owner].priority]).next != NULL))//Case 2, target at beginning but not alone
			{
				printf("case 2\n");
				target = schedule_array[TCBS[(*s).task_owner].priority];
				Node_t *afterTarget = (*schedule_array[TCBS[(*s).task_owner].priority]).next;
				schedule_array[TCBS[(*s).task_owner].priority] = afterTarget;
				
			}
			else//if not only one item
			{
				printf("case 3.");
				target = (*schedule_array[TCBS[(*s).task_owner].priority]).next;
				Node_t *beforeTarget = schedule_array[TCBS[(*s).task_owner].priority];

				while ((*target).task_num != (*s).task_owner)
				{
					target = (*target).next;
					beforeTarget = (*beforeTarget).next;
				}
				if ((*target).next == NULL)//If target is at end of linked list
				{
					printf("1\n");
					(*beforeTarget).next = NULL;
				}
				else//If target not at end of linked list
				{
					printf("2\n");
					Node_t *afterTarget = (*target).next;
					(*beforeTarget).next = afterTarget;
				}
			}
			if (schedule_array[TCBS[currTask].priority] == NULL)//Case 1, empty list, won't happen
			{
				schedule_array[TCBS[currTask].priority] = target;
				(*target).next = NULL;
			}
			else
			{
				Node_t *afterTarget = schedule_array[TCBS[currTask].priority];
				schedule_array[TCBS[currTask].priority] = target;
				(*target).next = afterTarget;
			}
				
			//Set new priority and promotion flag
			TCBS[(*s).task_owner].temporary_promotion = true;
			TCBS[(*s).task_owner].priority = TCBS[currTask].priority;
		}
		
		__enable_irq();
		printf("I am waiting for the mutex to be released by the owner. Thus I am enabling and disabling IRQs\n");
		printf("Current value of mutex count is <%d>\n", (*s).available);
		__disable_irq();
	}
	
	(*s).task_owner = currTask;
	(*s).available = false;
	printf("=================================THE MUTEX IS NOW <UNAVAILABLE> WITH OWNER TASK <%d>=======================================\n", currTask);
	__enable_irq();
}
	
void mutex_release(mutex_t *s) {
	__disable_irq();
	(*s).task_owner = 99;
	(*s).available = true;
	printf("=================================THE MUTEX IS NOW <AVAILABLE>=======================================\n");
	if (TCBS[currTask].temporary_promotion)
	{
		TCBS[currTask].temporary_promotion = false;
		TCBS[currTask].add_in_different_priority = true;
		__enable_irq();
		printf("I AM EXPLICITY INVOKING PENDSV HANDLER BECAUSE I AM DONE BEING TEMPORARILY PROMOTED\n");
		SCB->ICSR |= (1 << 28);
	}
	else
		__enable_irq();
}

void semaphore_init(sem_t *s, uint32_t count_) {
	(*s).count = count_;
	(*s).head = NULL;
	(*s).block_current_task_next_preempt = false;
}
void wait(sem_t *s) {
	__disable_irq();
	//Why in his notes does he do s<-s-1 in page 8 week 8
	
	//If semaphore is available
	if ((*s).count > 0)
	{
		(*s).count--;
		printf("=====================================SEMAPHORE DECREMENTED --====================================\n");
		__enable_irq();
		return;
	}
	else//If semaphore is not available
	{		
		//Iterates down wait list and adds new node
		Node_t *currNode;
		if ((*s).head == NULL)
		{
			Node_t* newNode = (Node_t*)malloc(sizeof(Node_t));
			(*s).head = newNode;
			(*((*s).head)).task_num = currTask;
			(*((*s).head)).next = NULL;
		}
		else
		{
			currNode = (*s).head;

			while ((*currNode).next != NULL)
			{
				currNode = (*currNode).next;
			}
		
			Node_t* newNode = (Node_t*)malloc(sizeof(Node_t));
			(*newNode).task_num = currTask;
			(*newNode).next = NULL;
			(*currNode).next = newNode;
		}
		
		//Blocks that task because it is trying to access an unavailable semaphore
		TCBS[currTask].status = task_blocked_semaphore;
		TCBS[currTask].when_unblocked_decrease_semaphore = s;
		
		
		//Invokes PendSV_Handler
		//printf("I AM EXPLICITY INVOKING PENDSV HANDLER====================================");
		__enable_irq();
		SCB->ICSR |= (1 << 28);
	}
}

void signal(sem_t *s) {
	__disable_irq();
	
	if ((*s).head == NULL)//No other threads waiting, does nothing
	{}
	else if ((*((*s).head)).next == NULL)//Deletes first task in wait list
	{
		//Unblock first task in wait list
		TCBS[(*((*s).head)).task_num].status = task_ready;
		add_node(TCBS[(*((*s).head)).task_num].priority, (*((*s).head)).task_num);
		
		//Deletes first task in wait list
		free((*s).head);
		(*s).head = NULL;
	}
	else//Deletes first task in wait list and rewires it
	{
		//Unblock first task in wait list
		TCBS[(*((*s).head)).task_num].status = task_ready;
		add_node(TCBS[(*((*s).head)).task_num].priority, (*((*s).head)).task_num);
		
		//Deletes first task in wait list and rewires it
		Node_t *secondInWaitList = (*((*s).head)).next;
		free((*s).head);
		(*s).head = secondInWaitList;
	}
	
	(*s).count++;
	printf("=====================================SEMAPHORE INCREMENTED ++====================================\n");
	
	__enable_irq();
	
	printf("I AM EXPLICITY INVOKING PENDSV HANDLER====================================");
		SCB->ICSR |= (1 << 28);
}

sem_t lock1;
sem_t lock2;

void rtosDelay(int num_timeslices)
{
	//Blocks current task, current task node is already removed from linked list array so just need to update its
	//status, and the next PendSV_Handler will handle everything
	TCBS[currTask].status = task_blocked;
	TCBS[currTask].timeslices_to_be_blocked = num_timeslices;
	TCBS[currTask].timeslices_since_blocked = 0;
	
	SCB->ICSR |= (1 << 28);
}

void PendSV_Handler(void) {
	printf("\n\n=============PENDSV BEGIN===============\n\n");			
	printf("numTasks: %d\n", numTasks);
	printf("createdTasks: %d\n", createdTasks);
	printf("currTask: %d\n", currTask);
	
	//Increments each blocked task's timeslices_since_blocked, and checks if blocked tasks are to be made active
	for (int i=0; i<createdTasks; i++)
	{
		if (TCBS[i].status == task_blocked)
		{
			TCBS[i].timeslices_since_blocked++;
			if (TCBS[i].timeslices_since_blocked > TCBS[i].timeslices_to_be_blocked)
			{
				add_node(TCBS[i].priority, i);
				TCBS[i].status = task_ready;
				printf("==============================================Task: %d no longer blocked\n", i);
			}
		}
	}
	
	for (int i=0; i<createdTasks; i++)
		printf("TASK %d STATUS: %d\n", i, TCBS[i].status);
	
	//TWO THINGS CHECKED HERE: 1. If it hasnt been blocked in last timeslice, put back. 2. If block flag set, DO NOT put back.
	if (TCBS[currTask].status == task_ready)
	{
		//If needs to be added in different priority because done priority inheritance
		if (TCBS[currTask].add_in_different_priority)
		{
			add_node(TCBS[currTask].different_priority, currTask);
			TCBS[currTask].add_in_different_priority = false;
			TCBS[currTask].different_priority = 99;
		}
		else//Normal add back
			add_node(TCBS[currTask].priority, currTask);
	}
	else
		printf("I have blocked task <%d>\n", currTask);
	
	//==================Print out bit vector lists
	for (int priority = 0; priority<6; priority++)
	{
		Node_t *currNode = schedule_array[priority];

		printf("\t\t\t\tPriority list %d:", priority);
		while (currNode != NULL)
		{
			printf("%d ", (*currNode).task_num);
			currNode = (*currNode).next;
		}
		printf("\n");
	}
	printf("\n");
	//=================================================

	//Finds next task
	next_task = find_next_task();
	
	//Pushes register contents onto current task's stack and updates its stack pointer
	TCBS[currTask].stack_pointer = (uint32_t *)storeContext();

	//Pops new task's registers content (stored on its stack) into registers
	restoreContext((uint32_t)TCBS[next_task].stack_pointer);
	
	printf("prev task: %d\n", currTask);
	//Updates current task
	currTask = next_task;
	printf("next task: %d\n", currTask);
	
	//Decreases semaphore if unblocked after waiting for semaphore to be available
	if (TCBS[currTask].when_unblocked_decrease_semaphore != NULL)
	{
		(*TCBS[currTask].when_unblocked_decrease_semaphore).count--;
		TCBS[currTask].when_unblocked_decrease_semaphore = NULL;
	}
	
	//Removes next task's node
	remove_front_node(TCBS[next_task].priority);
	
	//Reset PENDSVSET bit of ICSR to 0
	SCB->ICSR &= !(1 << 28);
	
	printf("\n\n=============PENDSV END===============\n\n");		
}

//Function pointer to create task function
typedef void(*rtosTaskFunc_t)(void *args);
//Gets called in taks initialization and pre-emting
void add_node(uint8_t priority_, uint8_t taskNum)
{
	//Iterate down linked list
	Node_t* currNode = schedule_array[priority_];

	//Case 1: if this priority's linked list is empty, just insert)
	if (schedule_array[priority_] == NULL)
	{
    Node_t* newNode = (Node_t*)malloc(sizeof(Node_t));
    (*newNode).task_num = taskNum;
    (*newNode).next = NULL;
		schedule_array[priority_] = newNode;
	}
	//Case 2: if not empty
  else
  {
		//Get to last node
    while ((*currNode).next != NULL)
      currNode = (*currNode).next;
    
    //Set last node pointer to pointer of new node, and set its members
    Node_t* newNode = (Node_t*)malloc(sizeof(Node_t));
    (*newNode).task_num = taskNum;
    (*newNode).next = NULL;
    (*currNode).next = newNode;
  }
}

void task_create(rtosTaskFunc_t taskFunction, void *R0, uint8_t priority_)
{
	//Protects against more than 6 tasks being created
	if (numTasks > 5)
		return;
	
	add_node(priority_, numTasks);
		
	//Initialize TCB members
	TCBS[numTasks].stack_pointer = TCBS[numTasks].base - 15;
	TCBS[numTasks].priority = priority_;
	TCBS[numTasks].status = task_ready;
	TCBS[numTasks].timeslices_since_blocked = 0;
	TCBS[numTasks].timeslices_to_be_blocked = 0;
	
	//Setting R0
	*(TCBS[numTasks].base - 7) = (uint32_t)R0;
	//Setting task function address
	*(TCBS[numTasks].base - 1) = (uint32_t)(*taskFunction);
	//Setting P0 to default value of 0x01000000 as specified in manual
	*(TCBS[numTasks].base) = (uint32_t)(0x01000000);

  numTasks++;
	createdTasks++;
}

//Returns task number of task removed, 0 if no ready tasks at priority, -1 if invalid priority
uint8_t remove_front_node(uint8_t priority)
{
  uint8_t taskNumOfRemoved = 0;

  if (priority > 5)
    return 99;

  if (schedule_array[priority] == NULL)
    return 0;
    
  taskNumOfRemoved = (*schedule_array[priority]).task_num;

  Node_t* secondNode = (*schedule_array[priority]).next;
  free(schedule_array[priority]);
  schedule_array[priority] = secondNode;

  return (uint8_t)taskNumOfRemoved;
}

//Return -1 if no available next task
uint8_t find_next_task()
{
  //Iterate down bit vector until find next available priority
  int next_priority = 5;
  while (schedule_array[next_priority] == NULL && next_priority >= 0) {
    next_priority--;
  }
  
  //If no available next task
  if (next_priority == -1)
	{
		printf("No available next task, ERROR");
    return 99;
	}

  //If available next task, return first task num of linked list.
  //Because available tasks are added at back of linked list
  //and ready tasks are removed at front of linked list,
  //tasks of equal priority that are ready are round robined
  return (uint8_t)(*schedule_array[next_priority]).task_num;
}

void initialization(void) {

	// Find address of main stack (first 32 bit value at 0x0 is base address)
	uint32_t **mainstack = 0x0;
	//This used to copy over main stack to task 1 stack
	uint32_t *mainstack_address = *mainstack;
	//This used to remember where main stack base is
	uint32_t *mainstack_base = *mainstack;
	
	TCBS[5].base = (uint32_t *)(mainstack_address - 0x0800/4);
	TCBS[4].base = (uint32_t *)(mainstack_address - 0x1200/4);
	TCBS[3].base = (uint32_t *)(mainstack_address - 0x1600/4);
	TCBS[2].base = (uint32_t *)(mainstack_address - 0x2000/4);
	TCBS[1].base = (uint32_t *)(mainstack_address - 0x2400/4);
	TCBS[0].base = (uint32_t *)(mainstack_address - 0x2800/4);
	
	numTasks = 0;
	
	//Initialize schedule array to all point to NULL. Will be populated by task create function.
	for (int i=0; i<6; i++)
		schedule_array[i] = NULL;
		
	for (int i=0; i<6; i++)
	{
		TCBS[i].when_unblocked_decrease_semaphore = NULL;
		TCBS[i].temporary_promotion = false;
		TCBS[i].add_in_different_priority = false;
		TCBS[i].different_priority = 99;
	}
	
	// Copy the main stack contents to process stack of new main() task and set the MSP to the main stack base address
	// Loop through each item and then save to next stack from mainstack_address - 0x8000
	uint32_t *MSP = (uint32_t *)__get_MSP();
	TCBS[0].current = TCBS[0].base;
	
	//Copy everything in main stack to task 1 stack
	while (mainstack_address >= MSP) {
		*TCBS[0].current = *mainstack_address;
		TCBS[0].current--;
		mainstack_address--;
	}
	
	TCBS[0].stack_pointer = TCBS[0].current + 1;
	
	//Set MSP to mainstack base address
	__set_MSP((uint32_t)mainstack_base);
	
	//Switches control from MSP to PSP
	__set_CONTROL(__get_CONTROL() | (1 << 1));
	
	//Set PSP to top of task 1 stack
	__set_PSP((uint32_t)TCBS[0].stack_pointer);
	
	//Begin multithread by running task 0. Correct next task will be determined at next pre-empt
	currTask = 0;
	
	//Set task 1 priority to 0, acts as idle task
	TCBS[0].priority = 0;
	
	//Set task 1 status to ready
	TCBS[0].status = task_ready;
	TCBS[0].timeslices_since_blocked = 0;
	TCBS[0].timeslices_to_be_blocked = 0;
	
	//Increment numtasks now that there is a task
	numTasks++;
	createdTasks++;
}


void first_task(void *args) {
	while (1)
	{
		printf("=====================================Task 1 attempts to decrement-- semaphore====================================\n");
		wait(&lock1);
		for (int i=0; i<30; i++)
		{
			printf("TASK 1: PROTECTED BY SEMAPHORE %d\n", lock1.count);
		}
		printf("=====================================Task 1 increments++ semaphore====================================\n");
		signal(&lock1);
		for (int i=0; i<30; i++)
		{
			printf("TASK 1: UNPROTECTED %d\n", lock1.count);
		}
	}
}

void second_task(void *args) {
	while (1)
	{
		printf("=====================================Task 2 attempts to decrement-- semaphore====================================\n");
		wait(&lock1);
		for (int i=0; i<30; i++)
		{
			printf("TASK 2: PROTECTED BY SEMAPHORE %d\n", lock1.count);
		}
		printf("=====================================Task 2 increments++ semaphore====================================\n");
		signal(&lock1);
		for (int i=0; i<30; i++)
		{
			printf("TASK 2: UNPROTECTED %d\n", lock1.count);
		}
	}
}

int main(void) {
	printf("\n   _____  ____  _               _____  _____  _____   _____ _______ ____   _____ \n");
	printf("  / ____|/ __ \| |        /\   |  __ \|_   _|/ ____| |  __ \__   __/ __ \ / ____|\n");
	printf(" | (___ | |  | | |       /  \  | |__) | | | | (___   | |__) | | | | |  | | (___  \n");
	printf("  \___ \| |  | | |      / /\ \ |  _  /  | |  \___ \  |  _  /  | | | |  | |\___ \ \n");
	printf("  ____) | |__| | |____ / ____ \| | \ \ _| |_ ____) | | | \ \  | | | |__| |____) |\n");
	printf(" |_____/ \____/|______/_/    \_\_|  \_\_____|_____/  |_|  \_\ |_|  \____/|_____/ \n");
	
	semaphore_init(&lock1, 1);

	//Initialization creates task 0
	initialization();
	
	rtosTaskFunc_t task1 = &first_task;
	task_create(task1, NULL, 3);
	rtosTaskFunc_t task2 = &second_task;
	task_create(task2, NULL, 3);
	
	SysTick_Config(SystemCoreClock/(1000));
	
	while(1) 
	{
		printf("TASK 0 (IDLE)\n");
	}
}