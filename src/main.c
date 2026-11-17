
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

//Add here necessary states
enum state { IDLE, SEND_DATA, RECEIVE_DATA };
enum state programState = IDLE;

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

    // Start the scheduler (never returns)
    vTaskStartScheduler();

    // Never reach this line.
    return 0;
}

