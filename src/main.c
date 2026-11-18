
#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"
#include "serial_comm.h"

// Default stack size for the tasks. It can be reduced to 1024 if task is not using lot of memory.
#define DEFAULT_STACK_SIZE 2048

// Note frequencies for success sound
#define NOTE_E5 659
#define NOTE_G5 784
#define NOTE_E6 1319
#define NOTE_C6 1047
#define NOTE_D6 1175
#define NOTE_G6 1568

#define MORSE_UNIT_MS 240
#define MAX_MSG 128

//Add here necessary states
enum state { IDLE, SEND_DATA, RECEIVE_DATA };
enum state programState = IDLE;

static void led_msg(char msg[]);
static void lcd_msg(char msg[]);
static void display_task(void *arg);
static void vastaanotto_task(void *arg);

// Jono, jolla vastaanotettu viesti siirretään naytto_task:lle
static QueueHandle_t msgQueue = NULL;

// Note sequence and durations for the success melody
int melody[] = {
NOTE_E5, NOTE_G5, NOTE_E6,
NOTE_C6, NOTE_D6, NOTE_G6
};

int noteLength[] = {
130, 130, 130,
130, 130, 260
};

// Struct for the morse translator
struct MorseCharacter {
    char character;
    char morse[6];
};

struct MorseCharacter morseAlphabet[] = {
    {'A', ".-"},
    {'B', "-..."},
    {'C', "-.-."},
    {'D', "-.."},
    {'E', "."},
    {'F', "..-."},
    {'G', "--."},
    {'H', "...."},
    {'I', ".."},
    {'J', ".---"},
    {'K', "-.-"},
    {'L', ".-.."},
    {'M', "--"},
    {'N', "-."},
    {'O', "---"},
    {'P', ".--."},
    {'Q', "--.-"},
    {'R', ".-."},
    {'S', "..."},
    {'T', "-"},
    {'U', "..-"},
    {'V', "...-"},
    {'W', ".--"},
    {'X', "-..-"},
    {'Y', "-.--"},
    {'Z', "--.."},
    {'0', "-----"},
    {'1', ".----"},
    {'2', "..---"},
    {'3', "...--"},
    {'4', "....-"},
    {'5', "....."},
    {'6', "-...."},
    {'7', "--..."},
    {'8', "---.."},
    {'9', "----."}
};

void play_success_melody(void) {
    int noteCount = sizeof(melody) / sizeof(melody[0]);
    
    for (int i = 0; i < noteCount; i++) {
        buzzer_play_tone (melody[i], noteLength[i]);
    }
    buzzer_turn_off();
}

char morse_to_char(char *morseCharacter) {
    int alphabetLength = sizeof(morseAlphabet) / sizeof(morseAlphabet[0]);
    
    for (int i = 0; i < alphabetLength; i++) {
        if (strcmp(morseAlphabet[i].morse, morseCharacter) == 0) {
            return morseAlphabet[i].character;
        }
    }
    return '!';
}

void morse_to_string(const char *morseText, char *decodedText) {
    char temporary[6];
    int j = 0; //index for temporary
    int k = 0; //index for decodedText
    
    for (int i = 0; ; i++) {
        if (morseText[i] != ' ' && morseText[i] != '\0') {
            temporary[j] = morseText[i];
            j++;
        }
        
        else {
            if (j > 0) {
                temporary[j] = '\0';
                decodedText[k] = morse_to_char(temporary);
                k++;
                j = 0;
            }
            
            else if (morseText[i] == ' ') {
                decodedText[k] = ' ';
                k++;
            }
            
            if (morseText[i] == '\0') {
                decodedText[k] = '\0';
                break;
            }
        }
    }
}




static void example_task(void *arg){
    (void)arg;

    for(;;){
        tight_loop_contents(); // Modify with application code here.
        vTaskDelay(pdMS_TO_TICKS(2000));
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

// Kokoaa työasemalta tulevat symbolit viestiksi ja antaa sen näytölle/LEDille.
// Viesti päättyy rivinvaihtoon ('\n'), jonka edellä olevat välilyönnit poistetaan.
static void vastaanotto_task(void *arg) {
	(void)arg;

	char viesti[MAX_VIESTI];
	int pituus = 0;
	char c;

	while (1) {
		if (serial_receive_symbol(&c, pdMS_TO_TICKS(100))) {
			if (c == '\n') {
				while (pituus > 0 && viesti[pituus - 1] == ' ') {
					pituus--;
				}
				viesti[pituus] = '\0';
				if (pituus > 0) {
					xQueueSend(viestiJono, viesti, 0);
				}
				pituus = 0;
			}
			else if (pituus < MAX_VIESTI - 1) {
				viesti[pituus] = c;
				pituus++;
			}
		}
	}
}

// Lähettää morse-viestin työasemalle (esim. "... --- ...") ja lisää loppuun
// kaksi välilyöntiä ja rivinvaihdon. Palauttaa true, jos lähetys onnistui.
// Kutsutaan taskista (esim. napin painalluksen jälkeen).
bool laheta_viesti(const char *morse) {
	if (!serial_is_connected()) {
		return false;
	}
	if (!serial_send_string(morse) || !serial_send_string("  \n")) {
		serial_tx_clear();
		return false;
	}
	// Odotetaan enintään 1 s, että kaikki on kirjoitettu USB:lle
	for (int i = 0; i < 100 && !serial_tx_idle(); i++) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	if (!serial_tx_idle()) {
		serial_tx_clear();
		return false;
	}
	return true;
}

int main() {
    stdio_init_all();
    // Uncomment this lines if you want to wait till the serial monitor is connected
    /*while (!stdio_usb_connected()){
        sleep_ms(10);
    }*/ 
    init_hat_sdk();
    sleep_ms(300); //Wait some time so initialization of USB and hat is done.

    TaskHandle_t myExampleTask = NULL;
    // Create the tasks with xTaskCreate
    BaseType_t result = xTaskCreate(example_task,       // (en) Task function
                "example",              // (en) Name of the task 
                DEFAULT_STACK_SIZE, // (en) Size of the stack for this task (in words). Generally 1024 or 2048
                NULL,               // (en) Arguments of the task 
                2,                  // (en) Priority of this task
                &myExampleTask);    // (en) A handle to control the execution of this task

    if(result != pdPASS) {
        printf("Example Task creation failed\n");
        return 0;
    }

    TaskHandle_t hDisplay = NULL;

	//Giving the task to FreeRTOS
	xTaskCreate(display_task, "display", 2048, NULL, 2, &hDisplay);

	// Jono luodaan ennen taskeja, jotta ne voivat käyttää sitä heti
	viestiJono = xQueueCreate(4, MAX_VIESTI);
	xTaskCreate(vastaanotto_task, "vastaanotto", DEFAULT_STACK_SIZE, NULL, 2, NULL);

	// Käynnistää USB-sarjaliikenteen. Heti perään vTaskStartScheduler.
	serial_comm_init();

    // Start the scheduler (never returns)
    vTaskStartScheduler();

    // Never reach this line.
    return 0;
}

