#include <Arduino.h>
#include "wiring_private.h"
#include <lin_stack.h>
#include <Keyboard.h>
#include <Serial_CAN_Module.h>

Serial_CAN can;

//Serial - CAN Bus
//Serial1 - LIN Slave (to ICM)
//Serial2 - LIN Master (from SWSR)
//SerialUSB - Serial comms to/from LineageOS

//Setup LIN serials on SERCOMs
Uart Serial2(&sercom1, 12, 10, SERCOM_RX_PAD_3, UART_TX_PAD_2); 
void SERCOM1_Handler()
{
  Serial2.IrqHandler();
}

Uart Serial3(&sercom2, 5, 2, SERCOM_RX_PAD_3, UART_TX_PAD_2);
void SERCOM2_Handler()
{
  Serial3.IrqHandler();
}

byte icm_req_data_size=8; // length of byte array
byte icm_req_data[8]; // byte array for received data
byte sws_resp_data_size=8;
byte sws_resp_data[8];

#define DISP_ICM 1;
#define DISP_EXT 2;

lin_stack LIN_slave(1, &Serial3); // Slave - sends responses upstream to ICM
lin_stack LIN_master(2, &Serial2); // Master - sends frames downstream to SWSR
  
void setup() {
  can.begin(&Serial, 9600);      // tx, rx

  pinPeripheral(2, PIO_SERCOM_ALT);
  pinPeripheral(5, PIO_SERCOM_ALT);
  pinPeripheral(10, PIO_SERCOM_ALT);
  pinPeripheral(12, PIO_SERCOM_ALT);

  Keyboard.begin(); // useful to detect host capslock state and LEDs
  SerialUSB.begin(115200);
}

int current_disp = 1;
byte current_sws_data[8];
byte current_sws_base[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF};
byte sws_offsets[8];
int scrl_dir = 1;

void fire_scroll_event() {
  if(scrl_dir == 1) {
    Keyboard.press(KEY_UP_ARROW);
    delayMicroseconds(50);
    Keyboard.release(KEY_UP_ARROW);
  } else {
    Keyboard.press(KEY_DOWN_ARROW);
    delayMicroseconds(50);
    Keyboard.release(KEY_DOWN_ARROW);
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  byte icm_req_ident = 0;
  memset(icm_req_data, 0, icm_req_data_size);
  byte icm_req_avail = LIN_slave.read(icm_req_data, icm_req_data_size, icm_req_ident);
  if(icm_req_avail == 1) {
    bool empty = 1;
    for(int i = 0; i <= icm_req_data_size; i++) {
      if(icm_req_data[i] != 0x00) {
        empty = 0;
      }
    }
    if(empty) {
      LIN_master.writeRequest(icm_req_ident);
    } else {
      LIN_master.write(icm_req_ident, icm_req_data, icm_req_data_size);
    }

    while(true) {
      byte sws_resp_avail = LIN_master.readStream(sws_resp_data, sws_resp_data_size);
      if(sws_resp_avail == 1) {
        if(icm_req_ident == 0x19) {
          memcpy(current_sws_data, sws_resp_data, sizeof(sws_resp_data));

          //handle scroll
          if(offset(5, 2) && offset(7, -2)) {
            memcpy(current_sws_base, current_sws_data, sizeof(current_sws_data));
            fire_scroll_event();
          }

          if(offset(5, 34) && offset(6, 1) && offset(7, -35)) {
            scrl_dir = 0;
          }

          if(offset(5, 34) && offset(6, 0) && offset(7, -34)) {
            scrl_dir = 1;
          }

          if(current_sws_base == current_sws_data) {
            if(current_disp == 1) {
              //if icm displaying, proxy all keypresses.
              LIN_slave.writeResponse(sws_resp_data, sws_resp_data_size);
            } else {
            //if ext input displaying, proxy only volume up/down
            if(offset(5, 1) && offset(7, -1)) {
              //volume up
              LIN_slave.writeResponse(sws_resp_data, sws_resp_data_size);
            }

            if(offset(4, 128) && offset(7, -128)) {
              //volume down
              LIN_slave.writeResponse(sws_resp_data, sws_resp_data_size);
            }

            //Handle all other keypresses & generate HID events
            if(offset(4, 64) && offset(7, -64)) {
              //Exit
              Keyboard.press(KEY_ESC);
            } else {
              Keyboard.release(KEY_ESC);
            }

            if(offset(4, 4) && offset(7, -4)) {
              //Voice
              Keyboard.press('v');
            } else {
              Keyboard.release('v');
            }

            if(offset(4, 2) && offset(7, -2)) {
              //reverse
              Keyboard.press('r');
            } else {
              Keyboard.release('r');
            }

            if(offset(4, -16) && offset(7, 16)) {
              //forward
              Keyboard.press('f');
            } else {
              Keyboard.release('f');
            }
          }
          }
        } else {
          LIN_slave.writeResponse(sws_resp_data, sws_resp_data_size);
        }
        break;
      } else {
        SerialUSB.println("Waiting for LIN frame response from slave...");
      }
    }
  }
}

bool offset(int bitno, int val) {
  int diff = 0-(current_sws_data[bitno] - current_sws_base[bitno]);
  if(val == diff) {
    return true;
  }

  //check for overflows
  if(diff == 0-(255-val) && val > 0) {
    return true;
  }

  if(diff == 255+val && val < 0) {
    return true;
  }

  return false;
}