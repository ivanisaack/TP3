/*
 * @brief FreeRTOS examples
 *
 * @note
 * Copyright(C) NXP Semiconductors, 2014
 * All rights reserved.
 *
 * @par
 * Software that is described herein is for illustrative purposes only
 * which provides customers with programming information regarding the
 * LPC products.  This software is supplied "AS IS" without any warranties of
 * any kind, and NXP Semiconductors and its licensor disclaim any and
 * all warranties, express or implied, including all implied warranties of
 * merchantability, fitness for a particular purpose and non-infringement of
 * intellectual property rights.  NXP Semiconductors assumes no responsibility
 * or liability for the use of the software, conveys no license or rights under any
 * patent, copyright, mask work right, or any other intellectual property rights in
 * or to any products. NXP Semiconductors reserves the right to make changes
 * in the software without notification. NXP Semiconductors also makes no
 * representation or warranty that such application will be suitable for the
 * specified use without further testing or modification.
 *
 * @par
 * Permission to use, copy, modify, and distribute this software and its
 * documentation is hereby granted, under NXP Semiconductors' and its
 * licensor's relevant copyrights in the software, without fee, provided that it
 * is used in conjunction with NXP Semiconductors microcontrollers.  This
 * copyright, permission, and disclaimer notice must appear in all copies of
 * this code.
 */

#include "board.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <stdlib.h>

#define xDelay500ms  ( (TickType_t) 500UL / portTICK_RATE_MS)
/*****************************************************************************
 * Private types/enumerations/variables
 ****************************************************************************/
#define EJ_4 (4)		/*  una Tarea 1 periódica que se ejecute cada 500mS, una interrupción (productor)
						    que se sincronice con la Tarea 2 (consumidor) mediante un semáforo y que la
						    Tarea 2 (productor) que se sincronice con la Tarea 3 (consumidor) mediante una cola*/
#define EJ_5 (5)		/* Idem 4 pero intercambiando las palabras semáforo por cola */
#define EJ_6 (6)		/* Idem 4 pero con 3 (tres) tareas que comparten el uso del LED, cada tarea enviará una secuencia fija de 1s y 0s (mayor que
                           time slice) y no deben mezclarse las secuencia */

#define TEST (EJ_5)

/*****************************************************************************
 * Public types/enumerations/variables
 ****************************************************************************/

/*****************************************************************************
 * Private functions
 ****************************************************************************/

/* Sets up system hardware */
static void prvSetupHardware(void) {
	SystemCoreClockUpdate();
	Board_Init();

	/* Initial LED state is off */
	Board_LED_Set(LED3, LED_OFF);
}

#if (TEST == EJ_4)

const char *pcTextForMain = "\r\n EJ 4\r\n";

/* The interrupt number to use for the software interrupt generation.  This
 * could be any unused number.  In this case the first chip level (non system)
 * interrupt is used, which happens to be the watchdog on the LPC1768.  WDT_IRQHandler */
/* interrupt is used, which happens to be the DAC on the LPC4337 M4.  DAC_IRQHandler */
#define mainSW_INTERRUPT_ID		(0)

/* Macro to force an interrupt. */
#define mainTRIGGER_INTERRUPT()	NVIC_SetPendingIRQ(mainSW_INTERRUPT_ID)

/* Macro to clear the same interrupt. */
#define mainCLEAR_INTERRUPT()	NVIC_ClearPendingIRQ(mainSW_INTERRUPT_ID)

#define mainSOFTWARE_INTERRUPT_PRIORITY	(5)

/* The tasks to be created. */
static void vPeriodicTask(void *pvParameters);
static void vHandlerTask(void *pvParameters);
static void vReceiverTask(void *pvParameters);

/* Enable the software interrupt and set its priority. */
static void prvSetupSoftwareInterrupt();

/* The service routine for the interrupt.  This is the interrupt that the
 * task will be synchronized with.  void vSoftwareInterruptHandler(void); */
/* the watchdog on the LPC1768 => WDT_IRQHandler *//* the DAC on the LPC4337 M4.  DAC_IRQHandler */
#define vSoftwareInterruptHandler (DAC_IRQHandler)
/* Declare a variable of type xSemaphoreHandle.  This is used to reference the
 * semaphore that is used to synchronize a task with an interrupt. */
xSemaphoreHandle xBinarySemaphore;
/* Declare a variable of type xQueueHandle.  This is used to store the queue
 * that is accessed by all three tasks. */
xQueueHandle xQueue;

static void vPeriodicTask(void *pvParameters) {
	/* As per most tasks, this task is implemented within an infinite loop. */
	while (1) {
		/* This task is just used to 'simulate' an interrupt.  This is done by
		 * periodically generating a software interrupt. */
		vTaskDelay(500 / portTICK_RATE_MS);

		/* Generate the interrupt, printing a message both before hand and
		 * afterwards so the sequence of execution is evident from the output. */
		DEBUGOUT("Periodic task - About to generate an interrupt.\r\n");
		mainTRIGGER_INTERRUPT();
		DEBUGOUT("Periodic task - Interrupt generated.\n\n");
	}
}

static void vHandlerTask(void *pvParameters) {

	xSemaphoreTake(xBinarySemaphore, (portTickType) 0);

	long lValueToSend;
	portBASE_TYPE xStatus;
	lValueToSend = (long) pvParameters;

	while (1) {
		Board_LED_Toggle(LED3);

		/* Use the semaphore to wait for the event.  The task blocks
		 * indefinitely meaning this function call will only return once the
		 * semaphore has been successfully obtained - so there is no need to check
		 * the returned value. */
		xSemaphoreTake(xBinarySemaphore, portMAX_DELAY);

		/* To get here the event must have occurred.  Process the event (in this
		 * case we just print out a message). */
		DEBUGOUT("Handler task - Processing event.\r\n");

		xStatus = xQueueSendToBack(xQueue, &lValueToSend, (portTickType)0);

		if (xStatus != pdPASS) {
			/* We could not write to the queue because it was full � this must
			 * be an error as the queue should never contain more than one item! */
			DEBUGOUT("Could not send to the queue.\r\n");
		}

		/* Allow the other sender task to execute. */
		taskYIELD();

	}
}

static void vReceiverTask(void *pvParameters) {
	/* Declare the variable that will hold the values received from the queue. */
	long lReceivedValue;
	portBASE_TYPE xStatus;
	const portTickType xTicksToWait = 100 / portTICK_RATE_MS;

	while (1) {
		Board_LED_Set(LED3, LED_OFF);

		/* As this task unblocks immediately that data is written to the queue this
		 * call should always find the queue empty. */
		if (uxQueueMessagesWaiting(xQueue) != 0) {
			DEBUGOUT("Queue should have been empty!\r\n");
		}

		xStatus = xQueueReceive(xQueue, &lReceivedValue, xTicksToWait);

		if (xStatus == pdPASS) {
			/* Data was successfully received from the queue, print out the received
			 * value. */
			DEBUGOUT("Received = %d\r\n", lReceivedValue);
		} else {
			/* We did not receive anything from the queue even after waiting for 100ms.
			 * This must be an error as the sending tasks are free running and will be
			 * continuously writing to the queue. */
			DEBUGOUT("Could not receive from the queue.\r\n");
		}
	}
}

static void prvSetupSoftwareInterrupt() {
	/* The interrupt service routine uses an (interrupt safe) FreeRTOS API
	 * function so the interrupt priority must be at or below the priority defined
	 * by configSYSCALL_INTERRUPT_PRIORITY. */
	NVIC_SetPriority(mainSW_INTERRUPT_ID, mainSOFTWARE_INTERRUPT_PRIORITY);

	/* Enable the interrupt. */
	NVIC_EnableIRQ(mainSW_INTERRUPT_ID);
}

void vSoftwareInterruptHandler(void)
{
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

	long lValueToSend;
	portBASE_TYPE xStatus;
	lValueToSend = 100;

	xQueueSendToBackFromISR(xQueue, &lValueToSend, &xHigherPriorityTaskWoken);

	/* Allow the other sender task to execute. */
	taskYIELD();

	/* Clear the software interrupt bit using the interrupt controllers
	 * Clear Pending register. */
	mainCLEAR_INTERRUPT();

	/* Giving the semaphore may have unblocked a task - if it did and the
	 * unblocked task has a priority equal to or above the currently executing
	 * task then xHigherPriorityTaskWoken will have been set to pdTRUE and
	 * portEND_SWITCHING_ISR() will force a context switch to the newly unblocked
	 * higher priority task.
	 *
	 * NOTE: The syntax for forcing a context switch within an ISR varies between
	 * FreeRTOS ports.  The portEND_SWITCHING_ISR() macro is provided as part of
	 * the Cortex-M3 port layer for this purpose.  taskYIELD() must never be called
	 * from an ISR! */
	portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}

/*****************************************************************************
 * Public functions
 ****************************************************************************/

int main(void) {
	/* Sets up system hardware */
	prvSetupHardware();

	/* Print out the name of this example. */
	DEBUGOUT(pcTextForMain);

	/* Before a semaphore is used it must be explicitly created.  In this example
	 * a binary semaphore is created. */
	vSemaphoreCreateBinary(xBinarySemaphore);

	/* The queue is created to hold a maximum of 5 long values. */
	xQueue = xQueueCreate(5, sizeof(long));

	/* Check the semaphore was created successfully. */
	if (xBinarySemaphore != (xSemaphoreHandle) NULL
			&& xQueue != (xQueueHandle) NULL) {
		/* Enable the software interrupt and set its priority. */
		prvSetupSoftwareInterrupt();

		xTaskCreate(vPeriodicTask, (char * ) "Task 1", configMINIMAL_STACK_SIZE,
						NULL, (tskIDLE_PRIORITY + 1UL), (xTaskHandle *) NULL);

		xTaskCreate(vHandlerTask, (char * ) "Task 2", configMINIMAL_STACK_SIZE,
				(void * ) 100, (tskIDLE_PRIORITY + 2UL), (xTaskHandle *) NULL);

		xTaskCreate(vReceiverTask, (char * ) "Task 3", configMINIMAL_STACK_SIZE,
				NULL, (tskIDLE_PRIORITY + 3UL), (xTaskHandle *) NULL);

		vTaskStartScheduler();
	}

	/* If all is well we will never reach here as the scheduler will now be
	 * running the tasks.  If we do reach here then it is likely that there was
	 * insufficient heap memory available for a resource to be created. */
	while (1)
	;

	/* Should never arrive here */
	return ((int) NULL);
}

#endif

#if (TEST == EJ_5)		/* Using a counting semaphore to synchronize a task with an interrupt */

const char *pcTextForMain = "\r\n EJ 5\r\n";

/* The interrupt number to use for the software interrupt generation.  This
 * could be any unused number.  In this case the first chip level (non system)
 * interrupt is used, which happens to be the watchdog on the LPC1768.  WDT_IRQHandler */
/* interrupt is used, which happens to be the DAC on the LPC4337 M4.  DAC_IRQHandler */
#define mainSW_INTERRUPT_ID		(0)

/* Macro to force an interrupt. */
#define mainTRIGGER_INTERRUPT()	NVIC_SetPendingIRQ(mainSW_INTERRUPT_ID)

/* Macro to clear the same interrupt. */
#define mainCLEAR_INTERRUPT()	NVIC_ClearPendingIRQ(mainSW_INTERRUPT_ID)

/* The priority of the software interrupt.  The interrupt service routine uses
 * an (interrupt safe) FreeRTOS API function, so the priority of the interrupt must
 * be equal to or lower than the priority set by
 * configMAX_SYSCALL_INTERRUPT_PRIORITY - remembering that on the Cortex-M3 high
 * numeric values represent low priority values, which can be confusing as it is
 * counter intuitive. */
#define mainSOFTWARE_INTERRUPT_PRIORITY	(5)

/* The tasks to be created. */
static void vPeriodicTask(void *pvParameters);
static void vHandlerTask(void *pvParameters);
static void vReceiverTask(void *pvParameters);

/* Enable the software interrupt and set its priority. */
static void prvSetupSoftwareInterrupt();

/* The service routine for the interrupt.  This is the interrupt that the
 * task will be synchronized with.  void vSoftwareInterruptHandler(void); */
/* the watchdog on the LPC1768 => WDT_IRQHandler *//* the DAC on the LPC4337 M4.  DAC_IRQHandler */
#define vSoftwareInterruptHandler (DAC_IRQHandler)
/* Declare a variable of type xSemaphoreHandle.  This is used to reference the
 * semaphore that is used to synchronize a task with an interrupt. */
xSemaphoreHandle xBinarySemaphore;
/* Declare a variable of type xQueueHandle.  This is used to store the queue
 * that is accessed by all three tasks. */
xQueueHandle xQueue;

static void vPeriodicTask(void *pvParameters)
{
	/* As per most tasks, this task is implemented within an infinite loop. */
	while (1) {
		/* This task is just used to 'simulate' an interrupt.  This is done by
		 * periodically generating a software interrupt. */

		vTaskDelay(500 / portTICK_RATE_MS);
		Board_LED_Toggle(LED1);

		/* Generate the interrupt, printing a message both before hand and
				 * afterwards so the sequence of execution is evident from the output. */
				DEBUGOUT("Periodic task - About to generate an interrupt.\r\n");
				mainTRIGGER_INTERRUPT();
				DEBUGOUT("Periodic task - Interrupt generated.\n\n");
	}
}
static void vHandlerTask(void *pvParameters)
{
	/* Declare the variable that will hold the values received from the queue. */
	long lReceivedValue;
	portBASE_TYPE xStatus;
	const portTickType xTicksToWait = 1000 / portTICK_RATE_MS;

	while (1) {

		/* As this task unblocks immediately that data is written to the queue this
		 * call should always find the queue empty. */
		if (uxQueueMessagesWaiting(xQueue) != 0) {
			DEBUGOUT("Queue should have been empty!\r\n");
		}

		xStatus = xQueueReceive(xQueue, &lReceivedValue, xTicksToWait);

		if (xStatus == pdPASS) {
					/* Data was successfully received from the queue, print out the received
					 * value. */
					DEBUGOUT("Received = %d\r\n", lReceivedValue);
				} else {
					/* We did not receive anything from the queue even after waiting for 100ms.
					 * This must be an error as the sending tasks are free running and will be
					 * continuously writing to the queue. */
					DEBUGOUT("Could not receive from the queue.\r\n");
				}

		/* 'Give' the semaphore to unblock the task. */
		xSemaphoreGive(xBinarySemaphore);
	}
}
static void vReceiverTask(void *pvParameters)
{

	xSemaphoreTake(xBinarySemaphore, (portTickType) 0);

	while (1) {
		Board_LED_Toggle(LED3);

		xSemaphoreTake(xBinarySemaphore, portMAX_DELAY);

		DEBUGOUT("Handler task - Processing event.\r\n");

	}
}


static void prvSetupSoftwareInterrupt()
{
	/* The interrupt service routine uses an (interrupt safe) FreeRTOS API
	 * function so the interrupt priority must be at or below the priority defined
	 * by configSYSCALL_INTERRUPT_PRIORITY. */
	NVIC_SetPriority(mainSW_INTERRUPT_ID, mainSOFTWARE_INTERRUPT_PRIORITY);

	/* Enable the interrupt. */
	NVIC_EnableIRQ(mainSW_INTERRUPT_ID);
}

//ISR
void vSoftwareInterruptHandler(void)
{
	portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

	long lValueToSend;
	portBASE_TYPE xStatus;
	lValueToSend = 100;

	/* The first parameter is the queue to which data is being sent.  The
	 * queue was created before the scheduler was started, so before this task
	 * started to execute.
	 *
	 * The second parameter is the address of the data to be sent.
	 *
	 * The third parameter is the Block time � the time the task should be kept
	 * in the Blocked state to wait for space to become available on the queue
	 * should the queue already be full.  In this case we don�t specify a block
	 * time because there should always be space in the queue. */

	//xStatus = xQueueSendToBack(xQueue, &lValueToSend, (portTickType)0);
	xQueueSendToBackFromISR(xQueue, &lValueToSend, &xHigherPriorityTaskWoken);

	/* Allow the other sender task to execute. */
	taskYIELD();

	/* Clear the software interrupt bit using the interrupt controllers
	 * Clear Pending register. */
	mainCLEAR_INTERRUPT();

	/* Giving the semaphore may have unblocked a task - if it did and the
	 * unblocked task has a priority equal to or above the currently executing
	 * task then xHigherPriorityTaskWoken will have been set to pdTRUE and
	 * portEND_SWITCHING_ISR() will force a context switch to the newly unblocked
	 * higher priority task.
	 *
	 * NOTE: The syntax for forcing a context switch within an ISR varies between
	 * FreeRTOS ports.  The portEND_SWITCHING_ISR() macro is provided as part of
	 * the Cortex-M3 port layer for this purpose.  taskYIELD() must never be called
	 * from an ISR! */
	portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}



/*****************************************************************************
 * Public functions
 ****************************************************************************/

int main(void)
{
	/* Sets up system hardware */
	prvSetupHardware();

	/* Print out the name of this example. */
	DEBUGOUT(pcTextForMain);

	/* Before a semaphore is used it must be explicitly created.  In this example
	 * a binary semaphore is created. */
	vSemaphoreCreateBinary(xBinarySemaphore);

	/* The queue is created to hold a maximum of 5 long values. */
	xQueue = xQueueCreate(5, sizeof(long));

	/* Check the semaphore was created successfully. */
	if (xBinarySemaphore != (xSemaphoreHandle) NULL && xQueue != (xQueueHandle)NULL) {
		/* Enable the software interrupt and set its priority. */
		prvSetupSoftwareInterrupt();




		xTaskCreate(vPeriodicTask, (char *) "Task 1", configMINIMAL_STACK_SIZE, NULL,
				(tskIDLE_PRIORITY + 1UL), (xTaskHandle *) NULL);

		xTaskCreate(vHandlerTask, (char *) "Task 2", configMINIMAL_STACK_SIZE, NULL,
						(tskIDLE_PRIORITY + 2UL), (xTaskHandle *) NULL);

		xTaskCreate(vReceiverTask, (char *) "Task 3", configMINIMAL_STACK_SIZE, NULL,
				(tskIDLE_PRIORITY + 3UL), (xTaskHandle *) NULL);

		vTaskStartScheduler();
	}

	/* If all is well we will never reach here as the scheduler will now be
	 * running the tasks.  If we do reach here then it is likely that there was
	 * insufficient heap memory available for a resource to be created. */
	while (1);

	/* Should never arrive here */
	return ((int) NULL);
}

#endif

#if (TEST == EJ_6)

const char *pcTextForMain = "\r\ EJ 6\r\n";

/* Dimensions the buffer into which messages destined for stdout are placed. */
#define mainMAX_MSG_LEN	( 80 )

/* The task to be created.  Two instances of this task are created. */
static void prvPrintTaskA(void *pvParameters);
static void prvPrintTaskB(void *pvParameters);
static void prvPrintTaskC(void *pvParameters);

/* The function that uses a mutex to control access to standard out. */
static void prvNewPrintString(const portCHAR *pcString, int color);

/* Declare a variable of type xSemaphoreHandle.  This is used to reference the
 * mutex type semaphore that is used to ensure mutual exclusive access to stdout. */
xSemaphoreHandle xMutex;

/* Take Mutex Semaphore, UART (or output) & LED toggle thread */
static void prvNewPrintString(const portCHAR *pcString) {
	static size_t i;
	static char cBuffer[mainMAX_MSG_LEN];
	volatile unsigned long ul;

	/* The semaphore is created before the scheduler is started so already
	 * exists by the time this task executes.
	 *
	 * Attempt to take the semaphore, blocking indefinitely if the mutex is not
	 * available immediately.  The call to xSemaphoreTake() will only return when
	 * the semaphore has been successfully obtained so there is no need to check the
	 * return value.  If any other delay period was used then the code must check
	 * that xSemaphoreTake() returns pdTRUE before accessing the resource (in this
	 * case standard out. */
	xSemaphoreTake(xMutex, portMAX_DELAY);
	{
		/* The following line will only execute once the semaphore has been
		 * successfully obtained - so standard out can be accessed freely. */
		sprintf(cBuffer, "%s", pcString);
		DEBUGOUT(cBuffer);

		Board_LED_Set(LED1, LED_ON);
		while (i < xDelay500ms) {
			i++;
		}
		i = 0;
		while (i < xDelay500ms) {
			i++;
		}
		i = 0;
		Board_LED_Set(LED1, LED_OFF);

	}
	xSemaphoreGive(xMutex);
}

static void prvPrintTaskA(void *pvParameters) {
	char *pcStringToPrint;

	/* Two instances of this task are created so the string the task will send
	 * to prvNewPrintString() is passed in the task parameter.  Cast this to the
	 * required type. */
	pcStringToPrint = (char *) pvParameters;

	while (1) {
		/* Print out the string using the newly defined function. */
		prvNewPrintString(pcStringToPrint);

		/* Wait a pseudo random time.  Note that rand() is not necessarily
		 * re-entrant, but in this case it does not really matter as the code does
		 * not care what value is returned.  In a more secure application a version
		 * of rand() that is known to be re-entrant should be used - or calls to
		 * rand() should be protected using a critical section. */
		vTaskDelay((rand() & 0x1FF));
	}
}

static void prvPrintTaskB(void *pvParameters) {
	char *pcStringToPrint;

	/* Two instances of this task are created so the string the task will send
	 * to prvNewPrintString() is passed in the task parameter.  Cast this to the
	 * required type. */
	pcStringToPrint = (char *) pvParameters;

	while (1) {
		/* Print out the string using the newly defined function. */
		prvNewPrintString(pcStringToPrint);

		/* Wait a pseudo random time.  Note that rand() is not necessarily
		 * re-entrant, but in this case it does not really matter as the code does
		 * not care what value is returned.  In a more secure application a version
		 * of rand() that is known to be re-entrant should be used - or calls to
		 * rand() should be protected using a critical section. */
		vTaskDelay((rand() & 0x1FF));
	}
}

static void prvPrintTaskC(void *pvParameters) {
	char *pcStringToPrint;

	/* Two instances of this task are created so the string the task will send
	 * to prvNewPrintString() is passed in the task parameter.  Cast this to the
	 * required type. */
	pcStringToPrint = (char *) pvParameters;

	while (1) {
		/* Print out the string using the newly defined function. */
		prvNewPrintString(pcStringToPrint);

		/* Wait a pseudo random time.  Note that rand() is not necessarily
		 * re-entrant, but in this case it does not really matter as the code does
		 * not care what value is returned.  In a more secure application a version
		 * of rand() that is known to be re-entrant should be used - or calls to
		 * rand() should be protected using a critical section. */
		vTaskDelay((rand() & 0x1FF));
	}
}

/*****************************************************************************
 * Public functions
 ****************************************************************************/
/**
 * @brief	main routine for FreeRTOS example 15 - Re-writing vPrintString() to use a semaphore
 * @return	Nothing, function should not exit
 */
int main(void) {
	/* Sets up system hardware */
	prvSetupHardware();

	/* Print out the name of this example. */
	DEBUGOUT(pcTextForMain);

	/* Before a semaphore is used it must be explicitly created.  In this example
	 * a mutex type semaphore is created. */
	xMutex = xSemaphoreCreateMutex();

	/* The tasks are going to use a pseudo random delay, seed the random number
	 * generator. */
	srand(567);

	/* Only create the tasks if the semaphore was created successfully. */
	if (xMutex != NULL) {
		/* Create two instances of the tasks that attempt to write stdout.  The
		 * string they attempt to write is passed in as the task parameter.  The tasks
		 * are created at different priorities so some pre-emption will occur. */
		xTaskCreate(prvPrintTaskA, (char * ) "Task A", configMINIMAL_STACK_SIZE,
				"Print Task A \r\n", (tskIDLE_PRIORITY + 1UL), (xTaskHandle *) NULL);
		xTaskCreate(prvPrintTaskB, (char * ) "Task B", configMINIMAL_STACK_SIZE,
				"Print Task B \r\n", (tskIDLE_PRIORITY + 1UL), (xTaskHandle *) NULL);
		xTaskCreate(prvPrintTaskC, (char * ) "Task C", configMINIMAL_STACK_SIZE,
				"Print Task C \r\n", (tskIDLE_PRIORITY + 1UL), (xTaskHandle *) NULL);

		/* Start the scheduler so the created tasks start executing. */
		vTaskStartScheduler();
	}
	/* If all is well we will never reach here as the scheduler will now be
	 * running the tasks.  If we do reach here then it is likely that there was
	 * insufficient heap memory available for a resource to be created. */
	while (1)
		;

	/* Should never arrive here */
	return ((int) NULL);
}
#endif

/**
 * @}
 */
