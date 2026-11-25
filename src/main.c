
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
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

// IMU motion detection initial settings (acceleration in g-units).
#define IMU_JAKSO_MS 20           // IMU reading interval
#define LIIKE_ALKU_G 0.15f        // movement begins when the acceleration deviates from the resting value by this much
#define LIIKE_SUMMA_G_MS 25.0f    // movement amount (g*ms), which generates a signal
#define LIIKE_IKKUNA_MS 300       // time within which the movement amount must be met
#define PAIKALLAAN_G 0.06f        // any change smaller than this is interpreted as being stationary
#define PAIKALLAAN_MS 600         // for how long the device must remain stationary before the next signal
#define NAPPI_VIIVE_MS 120        // button vibration filtering
// The direction of motion is measured along the x-axis: positive = '.', negative = '-'.
// If the directions are reversed, change the sign in the macro below (ax -> -ax).

#define LIIKE_AKSELI(ax, ay, az) (ax)

//Add here necessary states
enum state { IDLE, SEND_DATA, RECEIVE_DATA };
enum state programState = IDLE;

static void led_msg(char msg[]);
static void lcd_msg(char msg[]);
static void display_task(void *arg);
static void vastaanotto_task(void *arg);

// Queue used to pass the received message to display_task
static QueueHandle_t msgQueue = NULL;

// The I2C bus is shared between the display and the IMU, so access is protected by a mutex.
static SemaphoreHandle_t i2cMutex = NULL;
static void kirjoitus_task(void *arg);

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
		buzzer_turn_off();
		vTaskDelay(pdMS_TO_TICKS(40));
    }
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
            // Store a maximum of 5 characters to avoid exceeding the array
            if (j < 5) {
                temporary[j] = morseText[i];
            }
            j++;
        }
        
        else {
            if (j > 0) {
                if (j <= 5) {
                    temporary[j] = '\0';
                    decodedText[k] = morse_to_char(temporary);
                }
                else {
                    decodedText[k] = '!';   // segment too long, invalid character
                }
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
	
	for (int i = 0; i < length; i += 21) {
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

// Writes the text on the screen line by line (21 characters/line) starting from y.
// Uses at most maxRows rows. The caller must hold the i2cMutex.
static void nayta_rivit(const char *teksti, int y, int maxRivit) {
	int pituus = strlen(teksti);

	for (int i = 0, r = 0; i < pituus && r < maxRivit; i += 21, r++) {
		int maara = pituus - i;
		if (maara > 21) {
			maara = 21;
		}
		char rivi[22];
		memcpy(rivi, teksti + i, maara);
		rivi[maara] = '\0';
		write_text_xy(0, y, rivi);
		y += 8;
	}
}

// Displays the received message: letters at the top, morse code at the bottom.
// The caller must hold the i2cMutex.
static void nayta_vastaanotettu(const char *morse, const char *teksti) {
	clear_display();
	nayta_rivit(teksti, 0, 3);     // lines 1–3: letters
	nayta_rivit(morse, 32, 4);     // lines 5-8: morse symbols
}

static void display_task(void *arg) {
	//Ignore arg
	(void)arg;

	//Copying the message
	char viesti[MAX_MSG];
	//Message broken down into individual letters
	char teksti[MAX_MSG];

	//Initialize the LED and LCD (the display uses a shared I2C bus)
	xSemaphoreTake(i2cMutex, portMAX_DELAY);
	init_display();
	xSemaphoreGive(i2cMutex);
	init_led();

	while(1) {
		//Read the message and wait indefinitely
		if(xQueueReceive(msgQueue, viesti, portMAX_DELAY) == pdTRUE) {
			//Decode the message into individual letters and display it on the LCD and LED
			morse_to_string(viesti, teksti);
			if (xSemaphoreTake(i2cMutex, portMAX_DELAY) == pdTRUE) {
				nayta_vastaanotettu(viesti, teksti);
				xSemaphoreGive(i2cMutex);
			}
			led_msg(viesti);
		}
	}
}

// Collects symbols received from the workstation and forms a message to be displayed on the screen/LED.
// The message ends with a line break ('\n'), and any preceding spaces are removed.
static void vastaanotto_task(void *arg) {
	(void)arg;

	char viesti[MAX_MSG];
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
					xQueueSend(msgQueue, viesti, 0);
				}
				pituus = 0;
			}
			else if (pituus < MAX_MSG - 1) {
				viesti[pituus] = c;
				pituus++;
			}
		}
	}
}

// Sends a morse message to the workstation (e.g., "... --- ...") and appends two spaces and a line break. Returns true if the transmission is successful.
// Called from a task (e.g., after a button press).
bool laheta_viesti(const char *morse) {
	if (!serial_is_connected()) {
		return false;
	}
	if (!serial_send_string(morse) || !serial_send_string("  \n")) {
		serial_tx_clear();
		return false;
	}
	// Wait for at most 1 s for all data to be written to USB
	for (int i = 0; i < 100 && !serial_tx_idle(); i++) {
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	if (!serial_tx_idle()) {
		serial_tx_clear();
		return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
// IMU
// ---------------------------------------------------------------------------

/*
State machine:
    STATIONARY_MOTION  Waiting for the device to remain stationary for STATIONARY_MS.
                      This is the initial state upon startup; therefore,
                      the orientation at the moment of startup does not trigger a symbol.
    MOTION_READY       The device is stationary; waiting for motion.
    MOTION_IN_PROGRESS Motion has begun; the magnitude of motion is accumulated over a duration of MOTION_WINDOW_MS.
    Transitions:
        STATIONARY -> READY       The device has remained stationary for a sufficient duration.
        READY      -> IN_PROGRESS  Acceleration deviates from the resting value by more than MOTION_START_G.
        IN_PROGRESS -> STATIONARY  The accumulated motion is sufficient (a symbol is recorded) or the time window expires.
*/

enum liike_tila { LIIKE_PAIKALLAAN = 1, LIIKE_VALMIS, LIIKE_KESKEN };

static enum liike_tila liikeTila = LIIKE_PAIKALLAAN;
static bool refOlemassa = false;       // is the rest value (reference position) set
static float refX, refY, refZ;         // resting value, i.e., the device's position while stationary
static int paikallaanMs = 0;           // how long the device has been in place
static float liikeSumma = 0.0f;        // amount of motion (g*ms), sign indicates direction
static int liikeMs = 0;                // time elapsed since the start of the movement

// Forces the state machine to wait for a stationary state (e.g., after a button press)
static void liike_nollaa(void) {
	liikeTila = LIIKE_PAIKALLAAN;
	refOlemassa = false;
}

// Processes one IMU sample. Returns '.' or '-', when motion is detected,
// otherwise 0. dtMs is the time elapsed since the previous sample.
static char liike_askel(float ax, float ay, float az, int dtMs) {
	char symboli = 0;
	float akseli = LIIKE_AKSELI(ax, ay, az);
	float akseliRef = LIIKE_AKSELI(refX, refY, refZ);

	switch (liikeTila) {

	case LIIKE_PAIKALLAAN:
		// If the orientation changes, the reference position and timing are reset
		if (!refOlemassa
			|| fabsf(ax - refX) > PAIKALLAAN_G
			|| fabsf(ay - refY) > PAIKALLAAN_G
			|| fabsf(az - refZ) > PAIKALLAAN_G) {
			refX = ax;
			refY = ay;
			refZ = az;
			refOlemassa = true;
			paikallaanMs = 0;
		}
		else {
			paikallaanMs += dtMs;
			if (paikallaanMs >= PAIKALLAAN_MS) {
				liikeTila = LIIKE_VALMIS;
			}
		}
		break;

	case LIIKE_VALMIS:
		if (fabsf(akseli - akseliRef) > LIIKE_ALKU_G) {
			liikeSumma = (akseli - akseliRef) * dtMs;
			liikeMs = dtMs;
			liikeTila = LIIKE_KESKEN;
		}
		break;

	case LIIKE_KESKEN:
		liikeSumma += (akseli - akseliRef) * dtMs;
		liikeMs += dtMs;
		if (fabsf(liikeSumma) >= LIIKE_SUMMA_G_MS) {
			// Sufficient movement: the direction determines the sign
			symboli = (liikeSumma > 0) ? '.' : '-';
			liike_nollaa();
		}
		else if (liikeMs >= LIIKE_IKKUNA_MS) {
			// Insufficient movement within the time window: rejected
			liike_nollaa();
		}
		break;

	default:
		liike_nollaa();
		break;
	}
	return symboli;
}

// ---------------------------------------------------------------------------
// Message assembly, buttons, and feedback
// ---------------------------------------------------------------------------

// Displays the message being assembled (morse code) on the display
static void nayta_teksti(char viesti[]) {
	if (xSemaphoreTake(i2cMutex, portMAX_DELAY) == pdTRUE) {
		lcd_msg(viesti);
		xSemaphoreGive(i2cMutex);
	}
}

// Feedback on transmission: text on the display and an LED.
// Success = three short flashes, error = one long light.
static void ilmoita(const char *teksti, bool onnistui) {
	if (xSemaphoreTake(i2cMutex, portMAX_DELAY) == pdTRUE) {
		clear_display();
		write_text_xy(0, 0, teksti);
		xSemaphoreGive(i2cMutex);
	}
	if (onnistui) {
		play_success_melody();
		
		for (int i = 0; i < 3; i++) {
			set_led_status(true);
			vTaskDelay(pdMS_TO_TICKS(150));
			set_led_status(false);
			vTaskDelay(pdMS_TO_TICKS(150));
		}
	}
	else {
		set_led_status(true);
		vTaskDelay(pdMS_TO_TICKS(800));
		set_led_status(false);
	}
}

// Reads the IMU, saves the detected characters to the message, and processes the buttons:
//   SW1 = space (1 = character space, 2 = word space)
//   SW2 = send message to workstation
static void kirjoitus_task(void *arg) {
	(void)arg;

	char viesti[MAX_MSG];       // message being assembled
	int pituus = 0;
	bool sw1Ennen = false;         // button's previous state (for detecting rising edges)
	bool sw2Ennen = false;
	TickType_t edellinenNappi = 0; // time of the previous button press
	TickType_t edellinen = xTaskGetTickCount();

	viesti[0] = '\0';

	while (1) {
		vTaskDelay(pdMS_TO_TICKS(IMU_JAKSO_MS));

		// Time elapsed since the previous iteration
		TickType_t nyt = xTaskGetTickCount();
		int dtMs = (int)((nyt - edellinen) * portTICK_PERIOD_MS);
		edellinen = nyt;
		if (dtMs > 100) {
			dtMs = 100;
		}

		// --- IMU ---
		float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0, lampo = 0;
		int rc = -1;
		if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
			rc = ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &lampo);
			xSemaphoreGive(i2cMutex);
		}
		if (rc == 0) {
			char symboli = liike_askel(ax, ay, az, dtMs);
			if (symboli != 0 && pituus < MAX_MSG - 1) {
				viesti[pituus] = symboli;
				pituus++;
				viesti[pituus] = '\0';
				nayta_teksti(viesti);
			}
		}

		// --- Buttons (active level is high, so rising edge is detected) ---
		bool sw1 = gpio_get(SW1_PIN);
		bool sw2 = gpio_get(SW2_PIN);
		bool sw1Painettu = sw1 && !sw1Ennen;
		bool sw2Painettu = sw2 && !sw2Ennen;
		sw1Ennen = sw1;
		sw2Ennen = sw2;

		// Filter button noise: new press is not accepted immediately after the previous one
		if ((sw1Painettu || sw2Painettu)
			&& (nyt - edellinenNappi) < pdMS_TO_TICKS(NAPPI_VIIVE_MS)) {
			sw1Painettu = false;
			sw2Painettu = false;
		}
		if (sw1Painettu || sw2Painettu) {
			edellinenNappi = nyt;
			// Pressing the button moves the device, so we wait for it to settle again
			liike_nollaa();
		}

		if (sw1Painettu) {
			// Add a space only if the message already contains characters and does not already end with two spaces
			bool kaksiValia = (pituus >= 2 && viesti[pituus - 1] == ' ' && viesti[pituus - 2] == ' ');
			if (pituus > 0 && !kaksiValia && pituus < MAX_MSG - 1) {
				viesti[pituus] = ' ';
				pituus++;
				viesti[pituus] = '\0';
				nayta_teksti(viesti);
			}
		}

		if (sw2Painettu) {
			// Remove trailing spaces from the message and add the protocol footer
			while (pituus > 0 && viesti[pituus - 1] == ' ') {
				pituus--;
			}
			viesti[pituus] = '\0';

			if (pituus == 0) {
				ilmoita("Empty message", false);
			}
			else if (!serial_is_connected()) {
				ilmoita("No USB link", false);
			}
			else if (laheta_viesti(viesti)) {
				ilmoita("Sent OK!", true);
				pituus = 0;
				viesti[0] = '\0';
			}
			else {
				// The message is saved, so you can try sending it again
				ilmoita("Send failed", false);
			}
			liike_nollaa();
		}
	}
}

int main() {
    stdio_init_all();
    // Uncomment this lines if you want to wait till the serial monitor is connected
    /*while (!stdio_usb_connected()){
        sleep_ms(10);
    }*/ 
    init_hat_sdk();
    sleep_ms(300); //Wait some time so initialization of USB and hat is done.

	init_buzzer();

    // Buttons and IMU. The mutex is created before the tasks.
    init_sw1();
    init_sw2();
    if (init_ICM42670() == 0) {
        ICM42670_start_with_default_values();
    }
    i2cMutex = xSemaphoreCreateMutex();

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

	// The queue is created before the tasks so that they can use it immediately
	msgQueue = xQueueCreate(4, MAX_MSG);
	xTaskCreate(vastaanotto_task, "vastaanotto", DEFAULT_STACK_SIZE, NULL, 2, NULL);
	xTaskCreate(kirjoitus_task, "kirjoitus", DEFAULT_STACK_SIZE, NULL, 2, NULL);

	// Initializes USB serial communication. Followed immediately by vTaskStartScheduler.
	serial_comm_init();

    // Start the scheduler (never returns)
    vTaskStartScheduler();

    // Never reach this line.
    return 0;
}
