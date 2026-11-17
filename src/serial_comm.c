
//serial_comm.c
 
/*
USB serial communication with a workstation. 

 Structure:
 -usbTask runs the TinyUSB stack (tud_task)
 -serialTxTask sends symbols queued for transmission to the workstation (state machine)
 -tud_cdc_rx_cb called by TinyUSB when data is received from the workstation

 Data exchange between tasks is handled via FreeRTOS queues:
 -txQueue:  other parts of the program -> serialTxTask -> USB -> Workstation
 -rxQueue:  Workstation -> USB -> tud_cdc_rx_cb -> other parts of the program
*/

#include <stdio.h>
#include <string.h>

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <tusb.h>

#include "usbSerialDebug/helper.h"
#include "serial_comm.h"

#define TX_QUEUE_LEN 256   
#define RX_QUEUE_LEN 128   
#define DEBUG_BUF_SIZE 96 

// ------------------------------------------------------------------ 
// Transmit task state machine                                             
// ------------------------------------------------------------------ 

/*
 States:
 -SERIAL_WAIT_USB  waiting for USB connection and an open terminal
 -SERIAL_READY     connection ready, waiting for a symbol to send
 -SERIAL_SENDING   writing a symbol to the USB transmit buffer

 Allowed transitions:
 SERIAL_WAIT_USB -> SERIAL_READY      connection established
 SERIAL_READY    -> SERIAL_SENDING    symbol in queue
 SERIAL_SENDING  -> SERIAL_READY      symbol written, removed from queue
 SERIAL_READY    -> SERIAL_WAIT_USB   connection lost
 SERIAL_SENDING  -> SERIAL_WAIT_USB   connection lost during transmission

 The symbol is removed from the queue only after it has been written. 
*/

enum serial_state { SERIAL_WAIT_USB = 1, SERIAL_READY, SERIAL_SENDING };

//State variable. Only serialTxTask modifies it; other tasks only read it.
static volatile enum serial_state serialState = SERIAL_WAIT_USB;

// Queues for symbol transmission
static QueueHandle_t txQueue = NULL;
static QueueHandle_t rxQueue = NULL;

// Check if the debug library (CDC0) been successfully initialized
static bool debugReady = false;

// ------------------------------------------------------------------ 
// Helper functions                                                       
// ------------------------------------------------------------------ 

// Check if the character is a permitted Morse symbol
static bool is_valid_symbol(char c) {
    return (c == '.' || c == '-' || c == ' ' || c == '\n');
}

// Check if the workstation data port is open 
static bool data_port_ready(void) {
    return tud_mounted() && tud_cdc_n_connected(CDC_ITF_DATA);
}

/*
 Writes one character to the CDC1 transmit buffer. 
 If the buffer is full, it is cleared and the operation is retried. 
 Returns false if the connection is lost during writing.
*/
static bool write_symbol(char symbol) {
    while (tud_cdc_n_write_char(CDC_ITF_DATA, symbol) == 0) {
        /* 
        Buffer is full --> push data forward and wait for 
        the USB task to empty the buffer
        */
        tud_cdc_n_write_flush(CDC_ITF_DATA);
        vTaskDelay(pdMS_TO_TICKS(1));
        if (!data_port_ready()) {
            return false;
        }
    }
    return true;
}

// ------------------------------------------------------------------
// Tasks                                                    
// ------------------------------------------------------------------ 

/*
 USB stack task. In FreeRTOS 'tud_task()' handles event waiting internally,
 so don't add a 'vTaskDelay' call to this loop!
*/
static void usbTask(void *arg) {
    (void)arg;
    while (1) {
        tud_task();
    }
}

/*
 Transmission task. Transfers symbols from the queue to the workstation. 
 The operation is implemented as a state machine.
*/
static void serialTxTask(void *arg) {
    (void)arg;
    char symbol = 0;    // symbol currently being transmitted
    char removed = 0;   // symbol removed from queue

    while (1) {
        switch (serialState) {

        case SERIAL_WAIT_USB:
            // Wait for the data port to open 
            if (data_port_ready()) {
                serialState = SERIAL_READY;       
            } else {
                // Old symbols are not preserved without a connection
                xQueueReset(txQueue);
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            break;

        case SERIAL_READY:
            if (!data_port_ready()) {
                xQueueReset(txQueue);
                serialState = SERIAL_WAIT_USB;    
                break;
            }
            /*
            Examine the first symbol in the queue without removing it
            (at most 100 ms, so that a connection loss is detected) 
            */
            if (xQueuePeek(txQueue, &symbol, pdMS_TO_TICKS(100)) == pdTRUE) {
                serialState = SERIAL_SENDING;     
            }
            break;

        case SERIAL_SENDING:
            if (write_symbol(symbol)) {
                // The symbol is in the USB buffer --> remove it from the queue
                xQueueReceive(txQueue, &removed, 0);
                //Flush the buffer when the queue is empty.
                if (uxQueueMessagesWaiting(txQueue) == 0) {
                    tud_cdc_n_write_flush(CDC_ITF_DATA);
                }
                serialState = SERIAL_READY;        
            } else {
                // Connection lost. Incomplete message will not be continued.
                xQueueReset(txQueue);
                serialState = SERIAL_WAIT_USB;     
            }
            break;

        default:
            // Unknown state --> return to initial state
            serialState = SERIAL_WAIT_USB;
            break;
        }
    }
}

// ------------------------------------------------------------------ 
// Reception                                                       
// ------------------------------------------------------------------ 

/*
 TinyUSB calls this function when data has arrived on a CDC interface.

 Data is read and moved to 'rxQueue', from where other tasks process it. 
 Data is read from both ports to prevent the RX buffer from remaining full.
 Data from CDC0 is discarded.
*/
void tud_cdc_rx_cb(uint8_t itf) {
    uint8_t buf[32];

    while (tud_cdc_n_available(itf)) {
        uint32_t count = tud_cdc_n_read(itf, buf, sizeof(buf));

        if (itf != CDC_ITF_DATA) {
            continue;   // debug port data is passed
        }

        for (uint32_t i = 0; i < count; i++) {
            char c = (char)buf[i];

            // Filter out anything other than Morse symbols
            if (!is_valid_symbol(c)) {
                continue;
            }
            // If the queue is full, the symbol is lost.
            xQueueSend(rxQueue, &c, 0);
        }
    }
}

// ------------------------------------------------------------------ 
// Public interface                                                 
// ------------------------------------------------------------------ 

void serial_comm_init(void) {
    
    txQueue = xQueueCreate(TX_QUEUE_LEN, sizeof(char));
    rxQueue = xQueueCreate(RX_QUEUE_LEN, sizeof(char));

    // USB tasks have a higher priority than application tasks.
    TaskHandle_t hUsb = NULL;
    xTaskCreate(usbTask, "usb", 1024, NULL, 3, &hUsb);
    xTaskCreate(serialTxTask, "serialTx", 1024, NULL, 2, NULL);

#if (configNUMBER_OF_CORES > 1)
    // Attach the USB task to the core 0
    vTaskCoreAffinitySet(hUsb, 1u << 0);
#endif

    /* 
    Initialize TinyUSB and the debug library just before starting the
    scheduler (the caller starts the scheduler). usb_serial_init()
    must be called after tusb_init(). If it fails, debug messages
    will not be sent, but serial communication on CDC1 works.
    */
    tusb_init();
    debugReady = usb_serial_init();
}

bool serial_send_symbol(char symbol) {
    if (txQueue == NULL || !is_valid_symbol(symbol)) {
        return false;
    }
    
    if (!data_port_ready()) {
        return false;
    }
    // if the queue is full, return false
    return xQueueSend(txQueue, &symbol, 0) == pdTRUE;
}

bool serial_send_string(const char *s) {
    if (s == NULL) {
        return false;
    }
    for (size_t i = 0; s[i] != '\0'; i++) {
        if (!serial_send_symbol(s[i])) {
            return false;
        }
    }
    return true;
}

bool serial_is_connected(void) {
    return data_port_ready();
}

bool serial_tx_idle(void) {
    // Complete when the queue is empty and the last symbol has been written and flushed.
    return txQueue != NULL
        && uxQueueMessagesWaiting(txQueue) == 0
        && serialState == SERIAL_READY;
}

void serial_tx_clear(void) {
    if (txQueue != NULL) {
        xQueueReset(txQueue);
    }
}

bool serial_receive_symbol(char *symbol, TickType_t timeout) {
    if (rxQueue == NULL || symbol == NULL) {
        return false;
    }
    return xQueueReceive(rxQueue, symbol, timeout) == pdTRUE;
}

void serial_debug(const char *text) {
    char buf[DEBUG_BUF_SIZE];

    if (text == NULL || !debugReady || !usb_serial_connected()) {
        return;
    }
    // The __ markers prevent serial_client from interpreting the text as Morse code
    snprintf(buf, sizeof(buf), "__%s__\n", text);
    usb_serial_print(buf);
    usb_serial_flush();
}
