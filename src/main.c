
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
#define IMU_INTERVAL_MS 20           // IMU reading interval
#define MOTION_START_G 0.15f        // movement begins when the acceleration deviates from the resting value by this much
#define MOTION_AMOUNT_G_MS 25.0f    // movement amount (g*ms), which generates a signal
#define MOTION_WINDOW_MS 300       // time within which the movement amount must be met
#define STATIONARY_G 0.06f        // any change smaller than this is interpreted as being stationary
#define STATIONARY_MS 400         // for how long the device must remain stationary before the next signal
#define BUTTON_DELAY_MS 120        // button vibration filtering
// The direction of motion is measured along the x-axis: positive = '.', negative = '-'.
// If the directions are reversed, change the sign in the macro below (ax -> -ax).

#define MOTION_AXLE(ax, ay, az) (ax)

static void led_msg(char msg[]);
static void lcd_msg(char msg[]);
static void display_task(void *arg);
static void reception_task(void *arg);

// Queue used to pass the received message to display_task
static QueueHandle_t msgQueue = NULL;

// The I2C bus is shared between the display and the IMU, so access is protected by a mutex.
static SemaphoreHandle_t i2cMutex = NULL;
static void write_task(void *arg);

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
static void show_lines(const char *text, int y, int maxLINES) {
	int length = strlen(text);

	for (int i = 0, r = 0; i < length && r < maxLINES; i += 21, r++) {
		int maara = length - i;
		if (maara > 21) {
			maara = 21;
		}
		char rivi[22];
		memcpy(rivi, text + i, maara);
		rivi[maara] = '\0';
		write_text_xy(0, y, rivi);
		y += 8;
	}
}

// Displays the received message: letters at the top, morse code at the bottom.
// The caller must hold the i2cMutex.
static void show_received(const char *morse, const char *text) {
	clear_display();
	show_lines(text, 0, 3);     // lines 1–3: letters
	show_lines(morse, 32, 4);     // lines 5-8: morse symbols
}

static void display_task(void *arg) {
	//Ignore arg
	(void)arg;

	//Copying the message
	char message[MAX_MSG];
	//Message broken down into individual letters
	char text[MAX_MSG];

	//Initialize the LED and LCD (the display uses a shared I2C bus)
	xSemaphoreTake(i2cMutex, portMAX_DELAY);
	init_display();
	xSemaphoreGive(i2cMutex);
	init_led();

	while(1) {
		//Read the message and wait indefinitely
		if(xQueueReceive(msgQueue, message, portMAX_DELAY) == pdTRUE) {
			//Decode the message into individual letters and display it on the LCD and LED
			morse_to_string(message, text);
			if (xSemaphoreTake(i2cMutex, portMAX_DELAY) == pdTRUE) {
				show_received(message, text);
				xSemaphoreGive(i2cMutex);
			}
			led_msg(message);
		}
	}
}

// Collects symbols received from the workstation and forms a message to be displayed on the screen/LED.
// The message ends with a line break ('\n'), and any preceding spaces are removed.
static void reception_task(void *arg) {
	(void)arg;

	char message[MAX_MSG];
	int length = 0;
	char c;

	while (1) {
		if (serial_receive_symbol(&c, pdMS_TO_TICKS(100))) {
			if (c == '\n') {
				while (length > 0 && message[length - 1] == ' ') {
					length--;
				}
				message[length] = '\0';
				if (length > 0) {
					xQueueSend(msgQueue, message, 0);
				}
				length = 0;
			}
			else if (length < MAX_MSG - 1) {
				message[length] = c;
				length++;
			}
		}
	}
}

// Sends a morse message to the workstation (e.g., "... --- ...") and appends two spaces and a line break. Returns true if the transmission is successful.
// Called from a task (e.g., after a button press).
bool send_message(const char *morse) {
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

enum motion_state { STATIONARY_MOTION = 1, MOTION_READY, MOTION_IN_PROGRESS };

static enum motion_state motionState = STATIONARY_MOTION;
static bool referenceExists = false;       // is the rest value (reference position) set
static float refX, refY, refZ;         // resting value, i.e., the device's position while stationary
static int stationaryMs = 0;           // how long the device has been in place
static float motionSum = 0.0f;        // amount of motion (g*ms), sign indicates direction
static int motionMs = 0;                // time elapsed since the start of the movement

// Forces the state machine to wait for a stationary state (e.g., after a button press)
static void motion_reset(void) {
	motionState = STATIONARY_MOTION;
	referenceExists = false;
}

// Processes one IMU sample. Returns '.' or '-', when motion is detected,
// otherwise 0. dtMs is the time elapsed since the previous sample.
static char motion_step(float ax, float ay, float az, int dtMs) {
	char symbol = 0;
	float axle = MOTION_AXLE(ax, ay, az);
	float axleRef = MOTION_AXLE(refX, refY, refZ);

	switch (motionState) {

	case STATIONARY_MOTION:
		// If the orientation changes, the reference position and timing are reset
		if (!referenceExists
			|| fabsf(ax - refX) > STATIONARY_G
			|| fabsf(ay - refY) > STATIONARY_G
			|| fabsf(az - refZ) > STATIONARY_G) {
			refX = ax;
			refY = ay;
			refZ = az;
			referenceExists = true;
			stationaryMs = 0;
		}
		else {
			stationaryMs += dtMs;
			if (stationaryMs >= STATIONARY_MS) {
				motionState = MOTION_READY;
			}
		}
		break;

	case MOTION_READY:
		if (fabsf(axle - axleRef) > MOTION_START_G) {
			motionSum = (axle - axleRef) * dtMs;
			motionMs = dtMs;
			motionState = MOTION_IN_PROGRESS;
		}
		break;

	case MOTION_IN_PROGRESS:
		motionSum += (axle - axleRef) * dtMs;
		motionMs += dtMs;
		if (fabsf(motionSum) >= MOTION_AMOUNT_G_MS) {
			// Sufficient movement: the direction determines the sign
			symbol = (motionSum > 0) ? '.' : '-';
			motion_reset();
		}
		else if (motionMs >= MOTION_WINDOW_MS) {
			// Insufficient movement within the time window: rejected
			motion_reset();
		}
		break;

	default:
		motion_reset();
		break;
	}
	return symbol;
}

// ---------------------------------------------------------------------------
// Message assembly, buttons, and feedback
// ---------------------------------------------------------------------------

// Displays the message being assembled (morse code) on the display
static void show_text(char message[]) {
	if (xSemaphoreTake(i2cMutex, portMAX_DELAY) == pdTRUE) {
		lcd_msg(message);
		xSemaphoreGive(i2cMutex);
	}
}

// Feedback on transmission: text on the display and an LED.
// Success = three short flashes, error = one long light.
static void notify(const char *text, bool success) {
	if (xSemaphoreTake(i2cMutex, portMAX_DELAY) == pdTRUE) {
		clear_display();
		write_text_xy(0, 0, text);
		xSemaphoreGive(i2cMutex);
	}
	if (success) {
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
static void write_task(void *arg) {
	(void)arg;

	char message[MAX_MSG];       // message being assembled
	int length = 0;
	bool sw1Before = false;         // button's previous state (for detecting rising edges)
	bool sw2Before = false;
	TickType_t previousButtonPress = 0; // time of the previous button press
	TickType_t previous = xTaskGetTickCount();

	message[0] = '\0';

	while (1) {
		vTaskDelay(pdMS_TO_TICKS(IMU_INTERVAL_MS));

		// Time elapsed since the previous iteration
		TickType_t now = xTaskGetTickCount();
		int dtMs = (int)((now - previous) * portTICK_PERIOD_MS);
		previous = now;
		if (dtMs > 100) {
			dtMs = 100;
		}

		// --- IMU ---
		float ax = 0, ay = 0, az = 0, gx = 0, gy = 0, gz = 0, temp = 0;
		int rc = -1;
		if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
			rc = ICM42670_read_sensor_data(&ax, &ay, &az, &gx, &gy, &gz, &temp);
			xSemaphoreGive(i2cMutex);
		}
		if (rc == 0) {
			char symbol = motion_step(ax, ay, az, dtMs);
			if (symbol != 0 && length < MAX_MSG - 1) {
				message[length] = symbol;
				length++;
				message[length] = '\0';
				show_text(message);
			}
		}

		// --- Buttons (active level is high, so rising edge is detected) ---
		bool sw1 = gpio_get(SW1_PIN);
		bool sw2 = gpio_get(SW2_PIN);
		bool sw1Pressed = sw1 && !sw1Before;
		bool sw2Pressed = sw2 && !sw2Before;
		sw1Before = sw1;
		sw2Before = sw2;

		// Filter button noise: new press is not accepted immediately after the previous one
		if ((sw1Pressed || sw2Pressed)
			&& (now - previousButtonPress) < pdMS_TO_TICKS(BUTTON_DELAY_MS)) {
			sw1Pressed = false;
			sw2Pressed = false;
		}
		if (sw1Pressed || sw2Pressed) {
			previousButtonPress = now;
			// Pressing the button moves the device, so we wait for it to settle again
			motion_reset();
		}

		if (sw1Pressed) {
			// Add a space only if the message already contains characters and does not already end with two spaces
			bool twoSpace = (length >= 2 && message[length - 1] == ' ' && message[length - 2] == ' ');
			if (length > 0 && !twoSpace && length < MAX_MSG - 1) {
				message[length] = ' ';
				length++;
				message[length] = '\0';
				show_text(message);
			}
		}

		if (sw2Pressed) {
			// Remove trailing spaces from the message and add the protocol footer
			while (length > 0 && message[length - 1] == ' ') {
				length--;
			}
			message[length] = '\0';

			if (length == 0) {
				notify("Empty message", false);
			}
			else if (!serial_is_connected()) {
				notify("No USB link", false);
			}
			else if (send_message(message)) {
				notify("Sent OK!", true);
				length = 0;
				message[0] = '\0';
			}
			else {
				// The message is saved, so you can try sending it again
				notify("Send failed", false);
			}
			motion_reset();
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
	xTaskCreate(reception_task, "reception", DEFAULT_STACK_SIZE, NULL, 2, NULL);
	xTaskCreate(write_task, "write", DEFAULT_STACK_SIZE, NULL, 2, NULL);

	// Initializes USB serial communication. Followed immediately by vTaskStartScheduler.
	serial_comm_init();

    // Start the scheduler (never returns)
    vTaskStartScheduler();

    // Never reach this line.
    return 0;
}
