/*  Copyright (c) 2016 Macchina
 *
 *  Permission is hereby granted, free of charge, to any person obtaining
 *  a copy of this software and associated documentation files (the
 *  "Software"), to deal in the Software without restriction, including
 *  without limitation the rights to use, copy, modify, merge, publish,
 *  distribute, sublicense, and/or sell copies of the Software, and to
 *  permit persons to whom the Software is furnished to do so, subject to
 *  the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included
 *  in all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 *  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 *  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 *  SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *  LIN STACK for TJA1021 
 *  v2.0
 *
 *  Short description: 
 *  Comunication stack for LIN and TJA1021 LIN transceiver. 
 *  Can be modified for any Arduino board with UART available and any LIN slave.
 *  
 *  Author: Blaž Pongrac B.S., RoboSap, Institute of Technology, Ptuj (www.robosap-institut.eu)
 *  
 *  Arduino IDE 1.6.9
 *  RoboSap, Institute of Technology, September 2016
 */ 

#include <lin_stack.h>
#include <Arduino.h>

/* LIN PACKET:
   It consist of:
    ___________ __________ _______ ____________ _________ 
   |           |          |       |            |         |
   |Synch Break|Synch Byte|ID byte| Data Bytes |Checksum |
   |___________|__________|_______|____________|_________|
   
   Every byte have start bit and stop bit and it is send LSB first.
   Synch Break - 13 bits of dominant state ("0"), followed by 1 bit recesive state ("1")
   Synch Byte - Byte for Bound rate syncronization, always 0x55
   ID Byte - consist of parity, length and address; parity is determined by LIN standard and depends from address and message length
   Data Bytes - user defined; depend on devices on LIN bus
   Checksum - inverted 256 checksum; data bytes are sumed up and then inverted
*/

// CONSTRUCTORS
lin_stack::lin_stack(byte Ch, Uart *ser) {
	ch = Ch;
	serialClass = ser;
}

// PUBLIC METHODS
// WRITE methods
// Creates a LIN packet and then send it via USART(Serial) interface.
int lin_stack::write(byte ident, byte data[], byte data_size){
	// Calculate checksum
	byte suma = 0;
	for(int i=0;i<data_size;i++) 
		suma = suma + data[i];
	//suma = suma + 1;
	byte checksum = 255 - suma;
	// Synch Break
	serial_pause(13);
	// Send data via Serial interface
	serialClass->begin(bound_rate); // config Serial
	serialClass->write(0x55); // write Synch Byte to serial
	serialClass->write(ident); // write Identification Byte to serial
	for(int i=0;i<data_size;i++) serialClass->write(data[i]); // write data to serial
	serialClass->write(checksum); // write Checksum Byte to serial
	serialClass->end(); // clear Serial config
	return 1;
}

int lin_stack::writeRequest(byte ident){
	// Create Header
	byte identByte = (ident&0x3f) | calcIdentParity(ident);
	byte header[2]= {0x55, identByte};
	// Synch Break
	serial_pause(13);
	// Send data via Serial interface
	serialClass->begin(bound_rate); // config Serial
	serialClass->write(header,2); // write data to serial
	serialClass->end(); // clear Serial config
	return 1;
}

int lin_stack::writeResponse(byte data[], byte data_size){
	// Calculate checksum
	byte suma = 0;
	for(int i=0;i<data_size;i++) suma = suma + data[i];
	//suma = suma + 1;
	byte checksum = 255 - suma;
	// Send data via Serial interface
	serialClass->begin(bound_rate); // config Serial
	serialClass->write(data, data_size); // write data to serial
	serialClass->write(checksum); // write data to serial
	serialClass->end(); // clear Serial config
	return 1;
}

int lin_stack::writeStream(byte data[], byte data_size){
	// Synch Break
	serial_pause(13);
	// Send data via Serial interface
	serialClass->begin(bound_rate); // config Serial
	for(int i=0;i<data_size;i++) serialClass->write(data[i]);
	serialClass->end(); // clear Serial config
	return 1;
}

// READ methods
// Read LIN traffic and then proces it.
int lin_stack::setSerial(){ // Only needed when receiving signals
	serialClass->begin(bound_rate);
	//PIOA->PIO_PUER = PIO_PA10; // We need software Pull-Up because there is no hardware Pull-Up resistor
	return 1;
}

int lin_stack::read(byte data[], byte data_size, byte ident){
	byte rec[data_size+3];
	if(serialClass->read() != -1){ // Check if there is an event on LIN bus
		serialClass->readBytes(rec,data_size+3);
		if((validateParity(rec[1]))&(validateChecksum(rec,data_size+3))){
			ident = rec[1];
			for(int j=0;j<data_size;j++){
			data[j] = rec[j+2];
			}
			return 1;
		}else{
			return -1;
		}	
	}
	return 0;
}

int lin_stack::readStream(byte data[],byte data_size){
	byte rec[data_size];
	if(serialClass->read() != -1){ // Check if there is an event on LIN bus
		serialClass->readBytes(rec,data_size);
		for(int j=0;j<data_size;j++){
			data[j] = rec[j];
		}
		return 1;
	}
	return 0;
}


// PRIVATE METHODS
int lin_stack::serial_pause(int no_bits){
	// Calculate delay needed for 13 bits, depends on bound rate
	unsigned int del = period*no_bits; // delay for number of bits (no-bits) in microseconds, depends on period
	/*
	if(ch==2){
		PIOA->PIO_PER = PIO_PA13; // enable PIO register
		PIOA->PIO_OER = PIO_PA13; // enable PA13 as output
		PIOA->PIO_CODR = PIO_PA13; // clear PA13
		delayMicroseconds(del); // delay
		PIOA->PIO_SODR = PIO_PA13; // set pin high
		PIOA->PIO_ODR = PIO_PA13; // enable PA13 as output
		PIOA->PIO_PDR = PIO_PA13; // clear configuration for PIO, needs to be done because Serial wont work with it
	}else if(ch==1){
		PIOA->PIO_PER = PIO_PA11; // enable PIO register
		PIOA->PIO_OER = PIO_PA11; // enable PA11 as output
		PIOA->PIO_CODR = PIO_PA11; // clear PA11
		delayMicroseconds(del); // delay
		PIOA->PIO_SODR = PIO_PA11; // set pin high
		PIOA->PIO_ODR = PIO_PA11; // enable PA13 as output
		PIOA->PIO_PDR = PIO_PA11; // clear configuration for PIO, needs to be done because Serial wont work with it
	}
	*/
	return 1;
}

boolean lin_stack::validateParity(byte ident) {
	return true;
}

boolean lin_stack::validateChecksum(unsigned char data[], byte data_size){
	byte checksum = data[data_size-1];
	byte suma = 0;
	for(int i=2;i<data_size-1;i++) 
		suma = suma + data[i];
	byte v_checksum = 255 - suma - 1;
	if(checksum==v_checksum)
		return true;
	else
		return false;
} 

int lin_stack::busWakeUp()
{
  /*
  unsigned int del = period*10; // random delay for dominant signal, has to be in the timeframe from 250us ... 5ms
  if(ch==2)
  {
    PIOA->PIO_PER = PIO_PA13; // enable PIO register
    PIOA->PIO_OER = PIO_PA13; // enable PA13 as output
    PIOA->PIO_CODR = PIO_PA13; // clear PA13
    delayMicroseconds(del); // delay
    PIOA->PIO_SODR = PIO_PA13; // set pin high
    PIOA->PIO_PDR = PIO_PA13; // clear configuration for PIO, needs to be done because Serial wont work with it
  }
  else if(ch==1)
  {
    PIOA->PIO_PER = PIO_PA11; // enable PIO register
    PIOA->PIO_OER = PIO_PA11; // enable PA11 as output
    PIOA->PIO_CODR = PIO_PA11; // clear PA11
    delayMicroseconds(del); // delay
    PIOA->PIO_SODR = PIO_PA11; // set pin high
    PIOA->PIO_PDR = PIO_PA11; // clear configuration for PIO, needs to be done because Serial wont work with it
  }
  */
  return 1;
}

/* Create the Lin ID parity */
#define BIT(data,shift) ((ident&(1<<shift))>>shift)
byte lin_stack::calcIdentParity(byte ident)
{
  byte p0 = BIT(ident,0) ^ BIT(ident,1) ^ BIT(ident,2) ^ BIT(ident,4);
  byte p1 = ~(BIT(ident,1) ^ BIT(ident,3) ^ BIT(ident,4) ^ BIT(ident,5));
  return (p0 | (p1<<1))<<6;
}
