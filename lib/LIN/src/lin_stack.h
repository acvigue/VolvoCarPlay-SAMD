#include <Arduino.h>

class lin_stack
{
	public:
		// Constructors
		lin_stack(Uart *linSerial, int txPin); // Constructor for Master Node
		
		// Methods
		
		// Writing data to bus
		int begin();
		int write(byte add, byte data[], byte data_size); // write whole package
		int writeRequest(byte add); // Write header only
		int writeResponse(byte data[], byte data_size); // Write response only
		int writeStream(byte data[], byte data_size); // Writing user data to LIN bus
		int readRequest();
		int readRaw();
		int read(); // read data from LIN bus, checksum and ident validation
		int readStream(byte data[],byte data_size); // read data from LIN bus
	
	// Private methods and variables
	private:
		int _txPin;
		const unsigned long bound_rate = 10000; // 10417 is best for LIN Interface, most device should work
		const unsigned int period = 100; // in microseconds, 1s/10417
		Uart *linSerialObj;
		byte identByte; // user defined Identification Byte
		int serial_pause(int no_bits); // for generating Synch Break
		boolean validateParity(byte ident); // for validating Identification Byte, can be modified for validating parity
		boolean validateChecksum(byte data[], byte data_size); // for validating Checksum Byte
		byte calcIdentParity(byte ident);
		
};