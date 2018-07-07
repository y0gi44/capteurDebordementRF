#include "Arduino.h"
/*Utilisation de la librairie LowPower https://github.com/rocketscream/Low-Power
 *
 *
 * Custom sensor (base code)
 *
 * Emission in RF 433MHz with the X10 protocol for Domoticz
 *
 * Hardware :
 *   Arduino Pro Mini 5V         (4€)        https://www.amazon.fr/gp/product/B00QPUEFNW/ref=oh_aui_search_detailpage?ie=UTF8&psc=1
 *   RF 433MHz emitter/receiver  (3€)        https://www.amazon.fr/HDigiWorld-metteur-cepteur-commande-Arduino/dp/B00JTQI39G/ref=sr_1_3?s=computers&ie=UTF8&qid=1470813610&sr=1-3&keywords=433
 *       Note: only the emitter is used
 *   3 wires : VCC, GND, Signal (on Arduino pin 2)
 *
 * Documentation found for RF X10: http://www.printcapture.com/files/X10_RF_Receiver.pdf
 *
 * Example of malformed data received in RfxMngr in mode "nodec" (before this code was correctly debugged) :
 *
 *       Packettype        = UNDECODED RF Message
 *       UNDECODED NEC:1C515C86F0
 *
 *
 * Correct data that is now receveived by RfxMngr with this program :
 *
 *------------------------------------------------
 *       Packettype    = Lighting1
 *       subtype       = X10
 *       Sequence nbr  = 13
 *       housecode     = D
 *       unitcode      = 1
 *       Command       = On
 *       Signal level  = 7  -64dBm
 *------------------------------------------------
 *       Packettype    = Lighting1
 *       subtype       = X10
 *       Sequence nbr  = 14
 *       housecode     = D
 *       unitcode      = 1
 *       Command       = Off
 *       Signal level  = 7  -64dBm
 *------------------------------------------------
 */
#include "LowPower.h"

// partie sensor
const int pin_Sense = 9;         // Signal pin for RF433 emitter
// partie emmeteru
const int pin_emit = 10;         // Signal pin for RF433 emitter
const int pin_pwr = 11;         // Signal pin for RF433 emitter

const unsigned int X10_A1_ON = 0x00;
const unsigned int X10_A1_OFF = 0x20;

const unsigned int X10_address = 5; // this is Housecode. In my case 5 results in "D" house code
unsigned int X10_data = X10_A1_ON; // command. I have implemented only On/Off for unit code 1

void setup() {
	pinMode(pin_emit, OUTPUT);
	pinMode(pin_pwr, OUTPUT);

	pinMode(pin_Sense, INPUT_PULLUP);
	digitalWrite(pin_emit, LOW);

	Serial.begin(9600);
	delay(3000);
	Serial.println("Init Complete");
}

/**
 * Bit to 0 : 1.125ms between rising edges, so before next bit
 */
void X10_sendBit_zero() {
	digitalWrite(pin_emit, HIGH);
	delayMicroseconds(555); // Arbitrary value: I took around 1.125ms divided by two for the "VCC" state.
	digitalWrite(pin_emit, LOW);
	delayMicroseconds(570);       // Total 555+570 : 1125µs = 1.125ms
}

/**
 * Bit à 1 : 2.25ms entre les "rising edges"
 */
void X10_sendBit_one() {
	digitalWrite(pin_emit, HIGH);
	delayMicroseconds(555); // Arbitrary value: I took around 1.125ms divided by two for the "VCC" state.
	digitalWrite(pin_emit, LOW);
	delayMicroseconds(1695);      // Total 555+1695 : 2250µs = 2.250ms
}

void X10_send8bits(unsigned int data) {
	// - Send the 8 bits, and then the complementary of the 8 bits.
	// from documentation: "Within each byte, bit7 is received first and bit0 last"
	int mask = 0x80;
	for (int i = 0; i < 8; i++) {
		int bit = mask & data;
		if (bit == 0)
			X10_sendBit_zero();
		else
			X10_sendBit_one();

		mask = mask >> 1;
	}
}

/**
 * Sending a byte consists of sending this byte, and then sending the opposite of the same byte.
 * As we send the opposite as well as the data, the total duration is always the same : 8 * (1.125ms+2.250ms) = 8 * 3.375 = total of 27ms
 */
void X10_sendByte(unsigned int data) {
	X10_send8bits(data);
	X10_send8bits(~data);
}

/**
 * Send a complete X10 packet
 *
 * Total duration :
 * (9ms + 4.5ms) + (27ms + 27ms) + 0.555ms + 40ms = 108.055 ms
 *  from doc: "It appears, from articles on the 'net, that other people have seen a range from 95 msec to 116 msec for various transmitters and that standard X-10 transceivers can tolerate timing variations of 30-35% from nominal."
 */
void X10_sendState_once(unsigned int data) {
	// - Header
	digitalWrite(pin_emit, HIGH); // "Burst" de 9ms
	delayMicroseconds(9000);
	digitalWrite(pin_emit, LOW); // Silence de 4.5ms
	delayMicroseconds(4500);

	// - Data
	X10_sendByte(0x50); // - Address
	X10_sendByte(data); // - Data

	// - Send a last rising edge to validate last bit
	digitalWrite(pin_emit, HIGH);
	delayMicroseconds(555); // Arbitraty value (but always the same), for "VCC" state.

	// - Footer
	digitalWrite(pin_emit, LOW); // Silence de 40ms pour signifier la fin de l'émission
	delayMicroseconds(40000);

}

/**
 * Send a complete packet multiple times (this X10 protocol has not ACK))
 * From doc: "Most X-10 RF transmitters send a minimum of five copies of the code separated by 40 msec silences although some can send single bursts."
 *
 *  5 times 108.055ms = 540.275 ms, half a second
 */
void X10_sendState(unsigned int data) {
	// - "clean" to be sure to have a zero for the first rising edge on init
	// allumage de l'emetteur RF

	digitalWrite(pin_pwr, HIGH);
	delay(2);

	digitalWrite(pin_emit, LOW);
	delay(1);

	// - Send 5 time complete packet
	for (int repeat = 0; repeat < 5; repeat++)
		X10_sendState_once(data);

	digitalWrite(pin_pwr, LOW);
}

void refreshSensor() {
	// - Récupération du statut du capteur
	int state = digitalRead(pin_Sense);
	if (state != 0) {
		X10_data = X10_A1_OFF;
	} else {
		X10_data = X10_A1_ON;
	}

	Serial.print("Send Statut : ");
	Serial.println(state);
	X10_sendState(X10_data);
}

void loop() {

	Serial.print("Enter Sleeping ");
	LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
	Serial.print("Wakeup");
	refreshSensor();

}

