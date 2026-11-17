#include <stdio.h>
#include <string.h>

#include <pico/stdlib.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "tkjhat/sdk.h"

#define MORSE_YKSIKKO_MS 240
#define MAX_VIESTI 256


//prototyypit
static void led_viesti(char viesti[]);
static void lcd_viesti(char viesti[]);
static void naytto_task(void *arg);
int viesti_eheys(char viesti[]);

//Kahva
static QueueHandle_t viestiJono;


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
static void led_viesti(char viesti[]) {
	
	//Lasketaan viestin pituus
	int pituus = strlen(viesti);
	
	//LED pois päältä
	set_led_status(false);
	
	//Kytketään LED päälle/pois merkin määräämäksi ajaksi
	for (int i = 0; i < pituus; i++) {
		
		char m = viesti[i];
		
		if (m == '-') {
			//LED päällä 720ms
			set_led_status(true);
			vTaskDelay(pdMS_TO_TICKS(3*MORSE_YKSIKKO_MS));
			//LED pois 240ms (merkkiväli)
			set_led_status(false);
			vTaskDelay(pdMS_TO_TICKS(MORSE_YKSIKKO_MS));

		}	
		else if (m == '.') {
			set_led_status(true);
			vTaskDelay(pdMS_TO_TICKS(MORSE_YKSIKKO_MS));
			set_led_status(false);
			vTaskDelay(pdMS_TO_TICKS(MORSE_YKSIKO_MS));

		}
		else if (m == ' ') {
			//Sanaväli, josta lyhennetty merkkiväli
			vTaskDelay(pdMS_TO_TICKS(MORSE_YKSIKKO_MS));
		}
	}
}
	

static void lcd_viesti(char viesti[]) {
		

	
	//Tyhjennetään LCD
	clear_display();
	
	//Viestin pituus
	int pituus = strlen(viesti);
	
	int koord_y = 0;
	
	for (int i = 0; i < pituus; i += 21) {
		//Tarkistetaan, kuinka monta merkkiä on näyttämättä
		int maara = pituus - i;
		if (maara > 21) {
			maara = 21;
		}
		//Tehdään tilaa 21:lee merkille ja \0:lle
		char rivi[22]; 
		//Kopioidaan rivin verran merkkejä tai loput merkeistä viestistä
		memcpy(rivi, viesti + i, maara);
		//Lisätään lopetusmerkki
		rivi[maara] = '\0';
		//Kirjoittaa viesti LCD:lle.
		//Merkkimäärä per rivi on 128/6 = 21.3 -> 21 merkkiä
		//Rivejä yhteensä: 64/8 = 8
		//Merkkejä yhteensä 8*21 168 per viesti
		write_text_xy(0, koord_y, rivi);
		koord_y += 8;
		//Lopetetaan loop jos viestin pituus on yli LCD:n merkkimäärän.
		if(koord_y >= 64) {
			break;
		}
	}
}

static void naytto_task(void *arg) {
	//Jätetään arg huomiotta
	(void)arg;
	
	//Kopioidaan viesti
	char viesti[MAX_VIESTI];
	
	//Alustetaan LED ja LCD
	init_display();
	init_led();

	while(1) {
		//Luetaan viesti ja ododtetaan 'ikuisesti'
		if(xQueueReceive(viestiJono, viesti, portMAX_DELAY) == pdTRUE) {
			//Näytetään viesti LED:llä ja LCD:llä
			lcd_viesti(viesti);
			led_viesti(viesti);
		}
	}
}



int main() {
	stdio_init_all(); //alustaa Picon I/O
	init_hat_sdk(); //Alustetaan TJKHAT

	sleep_ms(300);
	//Luodaan uusi viestijono
	rxQueue = xQueueCreate(3, MAX_VIESTI);
	if (rxQueue == NULL) {
		printf("Viestijonon luominen epäonnistui\n");
		return 0;
	}
	//Kahvat
	TaskHandle_t hNaytto = NULL;

	//Annetaan task FreeRTOSlle
	xTaskCreate(naytto_task, "naytto", 2048, NULL, 2, &hNaytto);

	//Aloitetaan Scheduler
	vTaskStartScheduler();
	return 0;
}
}
