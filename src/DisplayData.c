#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"

#define MORSE_UNIT_MS 240
#define MAX_MSG 128


//prototyypit
static void led_msg(char msg[]);
static void lcd_msg(char msg[]);
static void display_task(void *arg);


//Handle
static QueueHandle_t msgQueue;


//Varmistetaan viestin eheys
// int viesti_eheys(char viesti[]) {
	
	////Viestin pituus
	// int pituus = strlen(viesti);
	
	////Käydään läpi jokainen merkki
	// for (int i = 0; i < pituus; i++) {
		// char m = viesti[i];
		// if (!(m == '.' || m == '-' || m == ' ')) {
			//Palautetaan 0, jos merkki ei ole jokin sallituista
			// return 0;
		// }
	// }
	////Palautetaan 1, jos jokainen merkki OK
	// return 1;
// }

//LED 
static void lcd_msg(char msg[]) {
		

	
	//Clear LCD
	clear_display();
	
	//Message length
	int length = strlen(msg);
	
	int coord_y = 0;
	
	for (int i = 0; i < pituus; i += 21) {
		//Checking how many symbols are yet to be shown
		int amount = length - i;
		if (amount > 21) {
			amount = 21;
		}
		//Making space for 21 symbols +  '\0'
		char row[22]; 
		//Copying a row of symbols or the rest of the message
		memcpy(row, msg + i, amount);
		//Adding '\0'
		row[amount] = '\0';
		//Typing the msg on the LCD
		//Symbols per row is 128/6 = 21.3 -> 21
		//Rows in total: 64/8 = 8
		//Symbols in total 8*21 168 per message
		write_text_xy(0, coord_y, row);
		coord_y += 8;
		//ENding the loop if message length is more than the LCD can hold
		if(coord_y >= 64) {
			break;
		}
	}
}
	

static void led_msg(char msg[]) {
	
	//Message length
	int length = strlen(msg);
	
	//LED off
	set_led_status(false);
	
	//LED on/off depending for a time depending on symbol
	for (int i = 0; i < length; i++) {
		
		char m = msg[i];
		
		if (m == '-') {
			//LED on 720ms
			set_led_status(true);
			vTaskDelay(pdMS_TO_TICKS(3*MORSE_UNIT_MS));
			//LED off 240ms (space)
			set_led_status(false);
			vTaskDelay(pdMS_TO_TICKS(MORSE_UNIT_MS));

		}	
		else if (m == '.') {
			set_led_status(true);
			vTaskDelay(pdMS_TO_TICKS(MORSE_UNIT_MS));
			set_led_status(false);
			vTaskDelay(pdMS_TO_TICKS(MORSE_UNIT_MS));

		}
		else if (m == ' ') {
			//Word spacing, from which substracted a symbol space
			vTaskDelay(pdMS_TO_TICKS(MORSE_UNIT_MS));
		}
	}
}

static void display_task(void *arg) {
	//Ignoring arg
	(void)arg;
	
	//Copying message
	char msg[MAX_MSG];
	
	//Initializing LED and LCD
	init_display();
	init_led();

	while(1) {
		//Reading the message and waiting 'forever'
		if(xQueueReceive(msgQueue, msg, portMAX_DELAY) == pdTRUE) {
			//Displaying message with lcd and led
			lcd_viesti(msg);
			led_viesti(msg);
		}
	}
}



int main() {
	stdio_init_all(); //Init Pico I/O
	init_hat_sdk(); //Init TJKHAT

	sleep_ms(300);
	//Creating new msgQueue
	rxQueue = xQueueCreate(3, MAX_MSG);
	if (rxQueue == NULL) {
		printf("Viestijonon luominen epäonnistui\n");
		return 0;
	}
	//Handle
	TaskHandle_t hDisplay = NULL;

	//Giving the task to FreeRTOS
	xTaskCreate(display_task, "display", 2048, NULL, 2, &hDisplay);

	//Init Scheduler
	vTaskStartScheduler();
	return 0;
}

