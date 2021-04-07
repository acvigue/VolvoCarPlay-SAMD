#include <Arduino.h>
#include "wiring_private.h"
#include <lin_stack.h>
#include <Keyboard.h>
//Serial - 115200b USB
//Serial1 - LIN Slave (to ICM)
//Serial2 - LIN Master (from SWSR)

#include <SlowSoftWire.h>
SlowSoftWire Wire = SlowSoftWire(13, 11);

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

uint8_t icm_req_data_size=8; // length of uint8_t array
uint8_t icm_req_data[8]; // uint8_t array for received data
uint8_t sws_resp_data_size=8;
uint8_t sws_resp_data[8];

#define DISP_ICM 1;
#define DISP_EXT 2;

byte tfp410_read(byte reg) {
  Wire.beginTransmission(0b0111000);
  for (byte i=1; i<1; i++) Wire.write(0x00);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(0b0111000,1);
  byte val = Wire.read();
  return val;
}

void tfp410_write(byte reg, byte value) {
  Wire.beginTransmission(0b0111000);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

byte tmds261b_read(byte reg) {
  Wire.beginTransmission(0b0101100);
  for (byte i=1; i<1; i++) Wire.write(0x00);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom(0b0101100,1);
  byte val = Wire.read();
  return val;
}

void tmds261b_write(byte reg, byte value) {
  Wire.beginTransmission(0b0101100);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

lin_stack LIN_slave(1, &Serial3); // Slave - sends responses upstream to ICM
lin_stack LIN_master(2, &Serial2); // Master - sends frames downstream to SWSR

void setup() {
  Serial.begin(115200);
  
  delay(100);
  pinMode(3, OUTPUT);
  digitalWrite(3, LOW);
  delay(200);
  digitalWrite(3, HIGH);

  Wire.begin();

  pinPeripheral(2, PIO_SERCOM_ALT);
  pinPeripheral(5, PIO_SERCOM_ALT);
  pinPeripheral(10, PIO_SERCOM_ALT);
  pinPeripheral(12, PIO_SERCOM_ALT);

  Keyboard.begin();

  delay(400);
  tfp410_write(0x08, 0b00110101);
  delay(2);
  tfp410_write(0x09, 0b00000001);
  delay(2);
  tfp410_write(0x0A, 0b10000000);
  delay(2);
  tfp410_write(0x33, 0b00000000);
  delay(50);

  tmds261b_write(0x01, 0b10010000);
  delay(2);
  tmds261b_write(0x03, 0x80);
}

int current_disp = 2; //due to lineage ECID requirements, display must always be connected on boot.
int last_disp = 0;
uint8_t current_sws_data[8];
uint8_t current_sws_base[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
bool base_set = false;
uint8_t sws_offsets[8];
int scrl_dir = 1;

void fire_scroll_event() {
  Serial.println("scroll evt");
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

bool exit_hold_started_flag = false;
unsigned long long exit_hold_started_time = 0;

bool offset(int bitno, int val) {
  int diff = 0-(current_sws_base[bitno] - current_sws_data[bitno]);
  if(val == diff) {
    return true;
  }

  //check for overflows
  if(val > 0) {
    //upper wraparound
    int distance_to_top = 255-current_sws_base[bitno];
    if(current_sws_data[bitno] == (val - distance_to_top)) {
      return true;
    }
  } else {
    //lower wraparound
    int distance_to_zero = current_sws_base[bitno];
    if(current_sws_data[bitno] == 255-(abs(val) - abs(distance_to_zero))) {
      return true;
    }
  }

  return false;
}

void debug_print_sws() {
  Serial.print("Base: ");
  for(int i = 0; i >= 8; i++) {
    Serial.print(current_sws_base[i]);
    Serial.print(" ");
  }
  Serial.println();

  Serial.print("Data: ");
  for(int i = 0; i >= 8; i++) {
    Serial.print(current_sws_data[i]);
    Serial.print(" ");
  }
  Serial.println();
}

unsigned long last_disp_switch;

void loop() {
  /*
  Serial.println("test");
  // put your main code here, to run repeatedly:
  uint8_t icm_req_ident = 0;
  memset(icm_req_data, 0, icm_req_data_size);
  uint8_t icm_req_avail = LIN_slave.read(icm_req_data, icm_req_data_size, icm_req_ident);
  
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
      uint8_t sws_resp_avail = LIN_master.readStream(sws_resp_data, sws_resp_data_size);
      if(sws_resp_avail == 1) {
        if(icm_req_ident == 0x19) {
          memcpy(current_sws_data, sws_resp_data, sizeof(sws_resp_data));

          if(!base_set) {
            base_set = true;
            memcpy(current_sws_base, current_sws_data, sizeof(current_sws_data));
          }

          //handle scroll
          if(offset(5, 2) && offset(7, -2)) {
            memcpy(current_sws_base, current_sws_data, sizeof(current_sws_data));
            fire_scroll_event();
          }

          if(offset(5, 34) && offset(6, 1) && offset(7, -35)) {
            Serial.println("down dir");
            scrl_dir = 0;
          }

          if(offset(5, 34) && offset(6, 0) && offset(7, -34)) {
            Serial.println("up dir");
            scrl_dir = 1;
          }

          if(current_sws_data != current_sws_base) {
            Serial.println("CHANGE on SWSR!");
            debug_print_sws();
            if(current_disp == 1) {
              Serial.println("Proxying to ICM.");
              //if icm displaying, proxy all keypresses.
              LIN_slave.writeResponse(sws_resp_data, sws_resp_data_size);

              //Handle hold exit to switch
              if(offset(4, 64) && offset(7, -64)) {
                //Exit
                if(exit_hold_started_flag == false) {
                  Serial.println("Exit hold start.");
                  exit_hold_started_flag = true;
                  exit_hold_started_time = millis();
                } else {
                  if(millis() - exit_hold_started_time > 3000) {
                    Serial.println("Exit released, switching to LineageOS");
                    current_disp = 2;
                    exit_hold_started_time = millis();
                  } else {
                    Serial.println("Exit released too quickly");
                  }
                }
              } else {
                exit_hold_started_flag = false;
              }
            } else {
              Serial.println("Sending to LineageOS");
              //if ext input displaying, proxy only volume up/down
              if(offset(5, 1) && offset(7, -1)) {
                //volume up
                LIN_slave.writeResponse(sws_resp_data, sws_resp_data_size);
              }

              if(offset(4, 128) && offset(7, -128)) {
                //volume down
                LIN_slave.writeResponse(sws_resp_data, sws_resp_data_size);
              }

              //proxy scroll events
              if(offset(5, 2) && offset(7, -2)) {
                LIN_slave.writeResponse(sws_resp_data, sws_resp_data_size);
              }

              //Handle all other keypresses & generate HID events
              if(offset(4, 64) && offset(7, -64)) {
                //Exit
                if(exit_hold_started_flag == false) {
                  Serial.println("Exit hold start.");
                  exit_hold_started_flag = true;
                  exit_hold_started_time = millis();
                } else {
                  if(millis() - exit_hold_started_time > 3000) {
                    Serial.println("Exit released, switching to ICM");
                    current_disp = 1;
                  } else {
                    Serial.println("Exit released too quickly");
                    Keyboard.press(KEY_ESC);
                  }
                }
              } else {
                exit_hold_started_flag = false;
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
          } else {
            //No change, send base val to ICM for sync purposrs
            LIN_slave.writeResponse(current_sws_base, sws_resp_data_size);
          }
        } else {
          LIN_slave.writeResponse(sws_resp_data, sws_resp_data_size);
        }
        break;
      } else {
        Serial.println("Waiting for LIN frame response from slave...");
      }
    }
  }
  */

  if(millis() - last_disp_switch > 5000) {
    last_disp_switch = millis();
    if(current_disp == 1) {
      current_disp = 2;
    } else {
      current_disp = 1;
    }
  }

  if(current_disp != last_disp) {
    last_disp = current_disp;

    if(current_disp == 1) {
      Serial.println("cmd: SWITCH to ICM");
      tmds261b_write(0x01, 0b11010000);
    } else {
      Serial.println("cmd: SWITCH to EXT");
      tmds261b_write(0x01, 0b10010000);

      //todo: switch to AUX input automatically.
    }
  }
}