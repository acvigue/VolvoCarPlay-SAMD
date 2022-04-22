#include <lin_stack.h>
#include <wiring_private.h>

// CONSTRUCTORS
lin_stack::lin_stack(Uart *linSerial, int txPin) {
	linSerialObj = linSerial;
	_txPin = txPin;
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
	serial_pause(20);
	linSerialObj->write(0x55); // write Synch Byte to serial
	linSerialObj->write(ident); // write Identification Byte to serial
	for(int i=0;i<data_size;i++) linSerialObj->write(data[i]); // write data to serial
	linSerialObj->write(checksum); // write Checksum Byte to serial
	return 1;
}

int lin_stack::writeRequest(byte ident){
	// Create Header
	byte identByte = (ident&0x3f) | calcIdentParity(ident);
	byte header[2]= {0x55, identByte};
	// Start interface
	// Synch Break
	serial_pause(20);
	// Send data via Serial interface
	linSerialObj->write(header,2); // write data to serial
	return 1;
}

int lin_stack::writeResponse(byte data[], byte data_size){
	// Calculate checksum
	byte suma = 0;
	for(int i=0;i<data_size;i++) suma = suma + data[i];
	//suma = suma + 1;
	byte checksum = 255 - suma;
	// Send data via Serial interface
	linSerialObj->write(data, data_size); // write data to serial
	linSerialObj->write(checksum); // write data to serial
	return 1;
}

int lin_stack::writeStream(byte data[], byte data_size){
	// Synch Break
	serial_pause(20);
	// Send data via Serial interface
	for(int i=0;i<data_size;i++) linSerialObj->write(data[i]);
	return 1;
}

int lin_stack::read() {
	byte rec[8];
	linSerialObj->readBytes(rec,8);
	Serial.print(rec[0]);
	Serial.print(rec[1]);
	Serial.print(rec[2]);
	Serial.print(rec[3]);
	Serial.print(rec[4]);
	Serial.print(rec[5]);
	return 1;
}

int lin_stack::readRaw() {
	return linSerialObj->read();
}

int lin_stack::readStream(byte data[],byte data_size){
	byte rec[data_size];
	if(linSerialObj->read() != -1){ // Check if there is an event on LIN bus
		linSerialObj->readBytes(rec,data_size);
		for(int j=0;j<data_size;j++){
			data[j] = rec[j];
		}
		return 1;
	}
	return 0;
}

int lin_stack::begin() {
	linSerialObj->begin(9600);
	return 1;
}


// PRIVATE METHODS
int lin_stack::serial_pause(int no_bits){

	//Bring bus low via transistor
	digitalWrite(8, HIGH);
	delayMicroseconds(100*(no_bits));
	digitalWrite(8, LOW);

	//Generate delimeter
	delayMicroseconds(100*0.98);
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

/* Create the Lin ID parity */
#define BIT(data,shift) ((ident&(1<<shift))>>shift)
byte lin_stack::calcIdentParity(byte ident)
{
  byte p0 = BIT(ident,0) ^ BIT(ident,1) ^ BIT(ident,2) ^ BIT(ident,4);
  byte p1 = ~(BIT(ident,1) ^ BIT(ident,3) ^ BIT(ident,4) ^ BIT(ident,5));
  return (p0 | (p1<<1))<<6;
}
