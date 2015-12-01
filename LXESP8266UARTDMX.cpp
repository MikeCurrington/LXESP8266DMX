/**************************************************************************/
/*!
    @file     LXESP8266UARTDMX.cpp
    @author   Claude Heintz
    @license  MIT (see LXESP8266UARTDMX.h)

    DMX Driver for ESP8266 using UART1.

    @section  HISTORY

    v1.0 - First release
*/
/**************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "Arduino.h"
#include "cbuf.h"

extern "C" {
#include "osapi.h"
#include "ets_sys.h"
#include "mem.h"
#include "user_interface.h"
}

#include "LXESP8266UARTDMX.h"

/* ***************** Utility functions derived from ESP HardwareSerial.cpp  ****************
   HardwareSerial.cpp - esp8266 UART support - Copyright (c) 2014 Ivan Grokhotkov. All rights reserved.
   HardwareSerial is licensed under GNU Lesser General Public License
   HardwareSerial is included in the esp8266 by ESP8266 Community board package for Arduino IDE
*/

static const int UART0 = 0;
static const int UART1 = 1;
static const int UART_NO = -1;


/**
 *  UART GPIOs
 *
 * UART0 TX: 1 or 2
 * UART0 RX: 3
 *
 * UART0 SWAP TX: 15
 * UART0 SWAP RX: 13
 *
 *
 * UART1 TX: 7 (NC) or 2
 * UART1 RX: 8 (NC)
 *
 * UART1 SWAP TX: 11 (NC)
 * UART1 SWAP RX: 6 (NC)
 *
 * NC = Not Connected to Module Pads --> No Access
 *
 */
 
void uart_tx_interrupt_handler(LX8266DMXOutput* dmxo);
void uart_rx_interrupt_handler(LX8266DMXInput* dmxi);
void uart__tx_flush(void);
void uart__rx_flush(void);
void uart_enable_rx_interrupt(LX8266DMXInput* dmxi);
void uart_disable_rx_interrupt(void);
void uart_enable_tx_interrupt(LX8266DMXOutput* dmxo);
void uart_disable_tx_interrupt(void);
void uart_set_baudrate(int uart_nr, int baud_rate);

void uart_init_tx(int baudrate, byte config, LX8266DMXOutput* dmxo);
void uart_init_rx(int baudrate, byte config, LX8266DMXInput* dmxi);
void uart_uninit(uart_t* uart);

// ####################################################################################################

// UART register definitions see esp8266_peri.h

void ICACHE_RAM_ATTR uart_tx_interrupt_handler(LX8266DMXOutput* dmxo) {

    // -------------- UART 1 --------------
    // check uart status register 
    // if fifo is empty clear interrupt
    // then call _tx_empty_irq
	  if(U1IS & (1 << UIFE)) {
			U1IC = (1 << UIFE);
			dmxo->_tx_empty_irq();
	  }
	 
}

void ICACHE_RAM_ATTR uart_rx_interrupt_handler(LX8266DMXInput* dmxi) {

    // -------------- UART 1 --------------
    // check uart status register 
    // if read buffer is full call _rx_complete_irq and then clear interrupt

	  while(U1IS & (1 << UIFF)) {
			dmxi->_rx_complete_irq((char) (U1F & 0xff));
			U1IC = (1 << UIFF);
	  }
   
}


// ####################################################################################################

//LX uses uart1 for tx
void uart_tx_flush(void) {
    uint32_t tmp = 0x00000000;

    tmp |= (1 << UCTXRST);
    
    USC0(UART1) |= (tmp);
    USC0(UART1) &= ~(tmp);
}

//LX uses uart0 for rx
void uart_rx_flush(void) {
    uint32_t tmp = 0x00000000;

    tmp |= (1 << UCRXRST);

    USC0(UART0) |= (tmp);
    USC0(UART0) &= ~(tmp);
}


//LX uses uart0 for rx
void uart_enable_rx_interrupt(LX8266DMXInput* dmxi) {
	USIC(UART0) = 0x1ff;
	uint8_t* uart;
	ETS_UART_INTR_ATTACH(&uart_rx_interrupt_handler, dmxi);
    USIE(UART0) |= (1 << UIFF);
}

//LX uses uart0 for rx
void uart_disable_rx_interrupt(void) {
   USIE(UART0) &= ~(1 << UIFF);
   ETS_UART_INTR_DISABLE();
}

//LX uses uart1 for tx
void uart_enable_tx_interrupt(LX8266DMXOutput* dmxo) {
	USIC(UART1) = 0x1ff;
	uint8_t* uart;
	ETS_UART_INTR_ATTACH(&uart_tx_interrupt_handler, dmxo);
   USIE(UART1) |= (1 << UIFE);
   ETS_UART_INTR_ENABLE();
}

//LX uses uart1 for tx
void uart_disable_tx_interrupt(void) {
   USIE(UART1) &= ~(1 << UIFE);
   ETS_UART_INTR_DISABLE();
}

//LX uses uart1 for tx, uart0 for rx
void uart_set_baudrate(int uart_nr, int baud_rate) {
    USD(uart_nr) = (ESP8266_CLOCK / baud_rate);
}

//LX uses uart1 for tx, uart0 for rx
void uart_set_config(int uart_nr, byte config) {
    USC0(uart_nr) = config;
}


void uart_init_tx(int baudrate, byte config, LX8266DMXOutput* dmxo) {
	pinMode(2, SPECIAL);
	uint32_t conf1 = 0x00000000;
	
   uart_set_baudrate(UART1, baudrate);
    USC0(UART1) = config;
    uart_tx_flush();
    uart_enable_tx_interrupt(dmxo);

    conf1 |= (0x01 << UCFFT);//empty threshold 0x20
    USC1(UART1) = conf1;
}


void uart_init_rx(int baudrate, byte config, LX8266DMXInput* dmxi) {

    uint32_t conf1 = 0x00000000;
    pinMode(3, SPECIAL);
    uart_set_baudrate(UART0, baudrate);
    USC0(UART0) = config;

    uart_rx_flush();
    uart_enable_rx_interrupt(dmxi);

    conf1 |= (0x01 << UCFFT);
    USC1(UART0) = conf1;
}

void uart_uninit_tx(void) {
    uart_disable_tx_interrupt();
	 pinMode(2, INPUT);
}

void uart_uninit_rx(void) {
    uart_disable_tx_interrupt();
	 pinMode(3, INPUT);
}

// **************************** global data (can be accessed in ISR)  ***************

// UART register definitions see esp8266_peri.h

#define DMX_DATA_BAUD		250000
#define DMX_BREAK_BAUD 	 	90000
/*
#define UART_STOP_BIT_NUM_SHIFT  4
TWO_STOP_BIT             = 0x3
ONE_STOP_BIT             = 0x1,

#define UART_BIT_NUM_SHIFT       2
EIGHT_BITS = 0x3

parity
#define UCPAE   1  //Parity Enable			(possibly set for none??)
#define UCPA    0  //Parity 0:even, 1:odd

111100 = 8n2  = 60 = 0x3C  (or 0x3E if bit1 is set for no parity)
011100 = 8n1  = 28 = 0x1C

*/

#define FORMAT_8N2			0x3C
#define FORMAT_8E1			0x1C


 //***** states indicate current position in DMX stream
    #define DMX_STATE_BREAK 0
    #define DMX_STATE_START 1
    #define DMX_STATE_DATA 2
    #define DMX_STATE_IDLE 3
	#define DMX_STATE_BREAK_SENT 4
	
	//***** interrupts to wait before changing Baud
    #define DATA_END_WAIT 25
    #define BREAK_SENT_WAIT 70

	//***** status is if interrupts are enabled and IO is active
    #define ISR_DISABLED 0
    #define ISR_ENABLED 1


/*******************************************************************************
 ***********************  LX8266DMXOutput member functions  ********************/

LX8266DMXOutput::LX8266DMXOutput ( void )
{
	//zero buffer including _dmxData[0] which is start code
	_slots = DMX_MAX_SLOTS;
	
    for (int n=0; n<DMX_MAX_SLOTS+1; n++) {
    	_dmxData[n] = 0;
    }
    _interrupt_status = ISR_DISABLED;
}

LX8266DMXOutput::LX8266DMXOutput ( uint8_t pin, uint16_t slots  )
{
	pinMode(pin, OUTPUT);
	digitalWrite(pin, HIGH);
	_slots = slots;
	
	//zero buffer including _dmxData[0] which is start code
    for (int n=0; n<DMX_MAX_SLOTS+1; n++) {
    	_dmxData[n] = 0;
    }
    _interrupt_status = ISR_DISABLED;
}

LX8266DMXOutput::~LX8266DMXOutput ( void )
{
    stop();
}

void LX8266DMXOutput::start ( void ) {
	if ( _interrupt_status != ISR_ENABLED ) {	//prevent messing up sequence if already started...
		_interrupt_status = ISR_ENABLED;
		_dmx_state = DMX_STATE_IDLE;
		_idle_count = 0;
		uart_init_tx(DMX_DATA_BAUD, FORMAT_8N2, this);//starts interrupt because fifo is empty
		//USF(1) = 0x0;									
	}
}

void LX8266DMXOutput::stop ( void ) { 
	uart_uninit_tx();
	_interrupt_status = ISR_DISABLED;
}

void LX8266DMXOutput::setMaxSlots (int slots) {
	_slots = max(slots, DMX_MIN_SLOTS);
}

void LX8266DMXOutput::setSlot (int slot, uint8_t value) {
	_dmxData[slot] = value;
}

uint8_t* LX8266DMXOutput::dmxData(void) {
	return &_dmxData[0];
}

void LX8266DMXOutput::_tx_empty_irq1(void) {
USF(1) = _interrupt_status;
_interrupt_status++;
}
/*!
 * @discussion TX FIFO EMPTY INTERRUPT
 *
 * this routine is called when UART fifo is empty
 *
 * what this does is to push the next byte into the fifo register
 *
 * when that byte is shifted out and the fifo is empty , the ISR is called again
 *
 * and the cycle repeats...
 *
 * until _slots worth of bytes have been sent on succesive triggers of the ISR
 *
 * and then the fifo empty interrupt is allowed to trigger 25 times to insure the last byte is fully sent
 *
 * then the break/mark after break is sent at a different speed
 *
 * and then the fifo empty interrupt is allowed to trigger 60 times to insure the MAB is fully sent
 *
 * then the baud is restored and the start code is sent
 *
 * and then on the next fifo empty interrupt
 *
 * the next data byte is sent
 *
 * and the cycle repeats...
*/

void ICACHE_RAM_ATTR LX8266DMXOutput::_tx_empty_irq(void) {
	
	switch ( _dmx_state ) {
		
		case DMX_STATE_BREAK:
			// set the slower baud rate and send the break
			uart_set_baudrate(UART1, DMX_BREAK_BAUD);
			uart_set_config(UART1, FORMAT_8E1);			
			_dmx_state = DMX_STATE_BREAK_SENT;
			_idle_count = 0;
			USF(1) = 0x0;
			break;		// <- DMX_STATE_BREAK
			
		case DMX_STATE_START:
			// set the baud to full speed and send the start code
			uart_set_baudrate(UART1, DMX_DATA_BAUD);
			uart_set_config(UART1, FORMAT_8N2);	
			_current_slot = 0;
			_dmx_state = DMX_STATE_DATA;
			USF(1) = _dmxData[_current_slot++];	//send next slot (start code)
			break;		// <- DMX_STATE_START
		
		case DMX_STATE_DATA:
			// send the next data byte until the end is reached
			USF(1) = _dmxData[_current_slot++];	//send next slot
			if ( _current_slot > _slots ) {
				_dmx_state = DMX_STATE_IDLE;
				_idle_count = 0;
			}
			break;		// <- DMX_STATE_DATA
			
		case DMX_STATE_IDLE:
			// wait a number of interrupts to be sure last data byte is sent before changing baud
			_idle_count++;
			if ( _idle_count > DATA_END_WAIT ) {
				_dmx_state = DMX_STATE_BREAK;
			}
			break;		// <- DMX_STATE_IDLE
			
		case DMX_STATE_BREAK_SENT:
			//wait to insure MAB before changing baud back to data speed (takes longer at slower speed)
			_idle_count++;
			if ( _idle_count > BREAK_SENT_WAIT ) {			
				_dmx_state = DMX_STATE_START;
			}
			break;		// <- DMX_STATE_BREAK_SENT
	}
}
