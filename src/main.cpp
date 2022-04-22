#include <Arduino.h>
#include "wiring_private.h"
#include <Keyboard.h>
#include "lin_stack.h"
#include <Wire.h>
#include "functions.h"

//TMDS261B
// Port 1 - Input from TFP410 - Volvo Orig ICM screen (11)
// Port 2 - Input from HDMI plug (10)

//Setup LIN serials on SERCOMs

//LIN slave connects to ICM with RX=13, TX=11
Uart LinSlave(&sercom1, 13, 11, SERCOM_RX_PAD_1, UART_TX_PAD_0); 
void SERCOM1_Handler()
{
  LinSlave.IrqHandler();
}

//LIN master connects to SWSR with RX=7, TX=6
Uart LinMaster(&sercom5, 7, 6, SERCOM_RX_PAD_3, UART_TX_PAD_2); 
void SERCOM5_Handler()
{
  LinMaster.IrqHandler();
}

uint8_t icm_req_data_size=8; // length of uint8_t array
uint8_t icm_req_data[8]; // uint8_t array for received data
uint8_t sws_resp_data_size=8;
uint8_t sws_resp_data[8];

#define MODE_ICM 1
#define MODE_EXT 2

lin_stack master(&LinMaster, 6);
lin_stack slave(&LinSlave, 11);

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);

  Wire.begin();
  Keyboard.begin();

  master.begin();
  slave.begin();

  pinMode(3, OUTPUT);
  digitalWrite(3, HIGH);
  delay(20);
  digitalWrite(3, LOW);
  delay(20);
  digitalWrite(3, HIGH);

  //Power up LIN transcievers
  pinMode(2, OUTPUT);
  pinMode(5, OUTPUT);
  digitalWrite(2, HIGH);
  digitalWrite(5, HIGH);

  pinMode(8, OUTPUT);

  delay(10);
  tfp410_write(0x08, 0b00110101);
  tfp410_write(0x09, 0b00000001);
  tfp410_write(0x0A, 0b10000000);
  tfp410_write(0x33, 0b00000000);
  delay(2);
  tmds261b_write(0x01, 0b11010000);
  tmds261b_write(0x03, 0x80);
}

uint8_t current_swsr_data[5] = {0x00, 0x00, 0x00, 0x00, 0xFF};
uint8_t current_swsr_base[5] = {0x00, 0x00, 0x00, 0x00, 0xFF};
uint8_t currdtax[5] = {0x00, 0x00, 0x00, 0x00, 0x00};

bool needCheckSWSR = false;
bool needSendData = false;
bool needProcessKeypress = false;
bool keyWasScrollUp = false;
bool ignoreNextKeypress = false;
bool ignoreNextEvent = false;

int current_mode = 1;
int last_mode = 1;

unsigned long long lastLinUpdate = 0;

String lastKey = "";
unsigned long long keyPressTime = 0;
unsigned long long lastScrollUp = 0;

int offset(int pos) {
  return current_swsr_data[pos]-current_swsr_base[pos];
}

void loop() {
  //Return requests for SWSR position - inner circle of hell.
  if(LinSlave.available() > 0) {
    //Check for valid synch byte (0x55)
    if(LinSlave.read() == 0x55) {
      delayMicroseconds(2200);
      byte h2 = LinSlave.read();
      //h2 = PID byte from frame (0x20 - SWSR, 0x50 - SWSR lighting)
      if(h2 == 0x20) {
        needCheckSWSR = true;
        if(needSendData == true || current_mode == MODE_ICM) {
          slave.writeResponse(current_swsr_data, 4);
        } else {
          slave.writeResponse(current_swsr_base, 4);
        }
      } else if(h2 == 0x50) {
        delayMicroseconds(2000);
        uint8_t t1 = LinSlave.read();
        uint8_t t2 = LinSlave.read();
        uint8_t t3 = LinSlave.read();
        uint8_t currdta[3] = {t1,0x00,t3};
        master.writeRequest(0x10);
        LinMaster.write(currdta, 3);
      }
    }
  }

  //Process potential keypresses.
  if(needProcessKeypress) {

    //If data equals the base value, then all keys are released.
    //Get last key and release it from the HID report.
    if(memcmp(current_swsr_base, current_swsr_data, 5) == 0) {
      needSendData = false;
      if(ignoreNextKeypress) {
        ignoreNextKeypress = false;
        if(lastKey != "") {
          if(lastKey == "exit") {
            int keyHeldFor = millis() - keyPressTime;

            //If key was held for > 500ms, switch inputs, else send key.
            if(keyHeldFor > 500) {
              if(current_mode == MODE_EXT) {
                current_mode = MODE_ICM;
              } else {
                current_mode = MODE_EXT;
              }
            } else {
              if(current_mode == MODE_EXT) {
                Keyboard.press(KEY_BACKSPACE);
                delay(50);
                Keyboard.releaseAll();
              }
            }
          }

          if(lastKey == "voice") {
            Keyboard.release('V');
          }

          if(lastKey == "ff") {
            Keyboard.release(KEY_RIGHT_ARROW);
          }

          if(lastKey == "rw") {
            Keyboard.release(KEY_LEFT_ARROW);
          }

          if(lastKey == "enter") {
            Keyboard.release(KEY_RETURN);
          }
        }
      }
      //If the offset of bits 2 & 4 are even and one is equal to the other ignoring negatives, then the wheel was scrolled.
    } else if(offset(2)%2==0 && offset(4)%2==0 && (offset(2) == -1*offset(4) || offset(4) == -1*offset(2))) {
      memcpy(current_swsr_base, currdtax, 5);
      if(ignoreNextEvent) {
        ignoreNextEvent = false;
      } else {
        lastKey = "up";
        ignoreNextEvent = true;
        Serial.println("scroll up");
        if(current_mode == MODE_EXT) {
          Keyboard.press(KEY_UP_ARROW);
          delay(25);
          Keyboard.releaseAll();
        }
      }
    } else if(offset(2) == 34 && offset(4) == -35) {
      Serial.println("scroll down");
      ignoreNextEvent = true;
      lastKey = "down";
      if(current_mode == MODE_EXT) {
        Keyboard.press(KEY_DOWN_ARROW);
        delay(25);
        Keyboard.releaseAll();
      }
    } else if(offset(1) == 32 && offset(4) == -32) {
      Serial.println("enter");
      lastKey = "enter";
      if(current_mode == MODE_EXT) {
        Keyboard.press(KEY_RETURN);
      }
    } else if(offset(1) == 64 && offset(4) == -64) {
      Serial.println("exit");
      lastKey = "exit";
      keyPressTime = millis();
    } else if(offset(1) == 4 && offset(4) == -4) {
      Serial.println("voice");
      lastKey = "voice";
      if(current_mode == MODE_EXT) {
        Keyboard.press('V');
      }
    } else if(offset(1) == 16 && offset(4) == -16) {
      Serial.println("fw");
      lastKey = "fw";
      if(current_mode == MODE_EXT) {
        Keyboard.press(KEY_RIGHT_ARROW);
      }
    } else if(offset(1) == 2 && offset(4) == -2) {
      Serial.println("rw");
      lastKey = "rw";
      if(current_mode == MODE_EXT) {
        Keyboard.press(KEY_LEFT_ARROW);
      }
    } else if(offset(2) == 1 && offset(4) == -1) {
      lastKey = "volup";
      needSendData = true;
      Serial.println("VOL:1");
    } else if(offset(1) == 128 && offset(4) == -128) {
      lastKey = "voldn";
      needSendData = true;
      Serial.println("VOL:-1");
    }
    ignoreNextKeypress = true;
    needProcessKeypress = false;
  }
  
  //Master requested SWSR, may as well ask ourselves to get new state.
  if(needCheckSWSR == true) {
    needCheckSWSR = false;
    master.writeRequest(0x20);
    delayMicroseconds(2200);
    //flush input buffer
    while(LinMaster.available() > 0) {
      LinMaster.read();
    }
    delayMicroseconds(7800);

    int dtapos = 0;
    //read data bytes from slave bus
    while(LinMaster.available() > 0) {
      uint8_t t = LinMaster.read();
      currdtax[dtapos] = t;
      dtapos++;
    }
    
    //if there was a change, we need to process potential keypresses.
    if(memcmp(current_swsr_data, currdtax, 5) != 0) {
      memcpy(current_swsr_data, currdtax, 5);
      needProcessKeypress = true;
    } else {
      needProcessKeypress = false;
    }
  }

  //Handle switching HDMI inputs
  if(current_mode != last_mode) {
    last_mode = current_mode;
    if(current_mode == MODE_ICM) {
      tmds261b_write(0x01, 0b11010000);
    } else {
      tmds261b_write(0x01, 0b10010000);
    }
  }
  
  //Handle CANbus events from waterfall console.
  if(Serial1.available() > 0) {
    String str = Serial1.readStringUntil('\n');
    Serial.println(str);
    if(str.indexOf("nums_all:rls") != -1) {
      Keyboard.release('1');
      Keyboard.release('2');
      Keyboard.release('3');
      Keyboard.release('4');
      Keyboard.release('5');
      Keyboard.release('6');
      Keyboard.release('7');
      Keyboard.release('8');
      Keyboard.release('9');
      Keyboard.release('0');
      Keyboard.release('*');
      Keyboard.release('#');
    }

    if(str.indexOf("1:prs") != -1) {
      Keyboard.press('1');
    }

    if(str.indexOf("2:prs") != -1) {
      Keyboard.press('2');
    }

    if(str.indexOf("3:prs") != -1) {
      Keyboard.press('3');
    }

    if(str.indexOf("4:prs") != -1) {
      Keyboard.press('4');
    }

    if(str.indexOf("5:prs") != -1) {
      Keyboard.press('5');
    }

    if(str.indexOf("6:prs") != -1) {
      Keyboard.press('6');
    }

    if(str.indexOf("7:prs") != -1) {
      Keyboard.press('7');
    }

    if(str.indexOf("8:prs") != -1) {
      Keyboard.press('8');
    }

    if(str.indexOf("9:prs") != -1) {
      Keyboard.press('9');
    }

    if(str.indexOf("*:prs") != -1) {
      Keyboard.press('*');
    }

    if(str.indexOf("0:prs") != -1) {
      Keyboard.press('0');
    }

    if(str.indexOf("#:prs") != -1) {
      Keyboard.press('#');
    }
  }
}
