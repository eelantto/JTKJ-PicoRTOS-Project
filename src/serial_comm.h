
// serial_comm.h
 
/*
 USB serial communication with a workstation.

 Uses two CDC ports:
 -CDC0 = debug messages (usbSerialDebug/helper.h)
 -CDC1 = Morse symbols (this module)

 The module handles the USB stack and serial communication within its own FreeRTOS tasks. 
 The rest of the program interacts only via the functions listed below:
 
 Transmission: 
 -serial_send_symbol()
 -serial_send_string()
 
 Status checks: 
 -serial_is_connected() 
 -serial_tx_idle()
 -serial_tx_clear()
 
 Reception:   
 -serial_receive_symbol()
 
 Debug:        
 -serial_debug()
*/

#ifndef SERIAL_COMM_H
#define SERIAL_COMM_H

#include <stdbool.h>
#include <FreeRTOS.h>
#include <task.h>

// CDC interface
#define CDC_ITF_DEBUG 0
#define CDC_ITF_DATA  1

/*
 Initializes serial communication: 
 -creates queues 
 -creates tasks 
 -initializes TinyUSB
 -initializes the debug library. 
 
 Called from the main function just before the vTaskStartScheduler() call.
*/
void serial_comm_init(void);

/*
 Adds one allowed symbol to the queue.
 
 Returns false, if:
 -symbol is not allowed
 -queue is full 
 -data-port was not opened.
*/
bool serial_send_symbol(char symbol);

/*
 Add the symbols of the string to the queue one by one.
 Returns true if all symbols fit into the queue. Otherwise, returns false.
*/
bool serial_send_string(const char *s);

/*
 Check if the data port (CDC1) is open.
*/
bool serial_is_connected(void);

/*
 Returns true when the transmission is complete.
*/
bool serial_tx_idle(void);

/*
 Clears unsent symbols from the queue.
*/
void serial_tx_clear(void);

/*
 Retrieves a symbol received from the workstation.
 Returns true if the symbol was obtained.
*/
bool serial_receive_symbol(char *symbol, TickType_t timeout);

/*
 Sends a debug message via the CDC0 port.
 Don't call from an interrupt handler or from TinyUSB callbacks!
*/
void serial_debug(const char *text);

#endif /* SERIAL_COMM_H */
