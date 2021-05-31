#include <Arduino.h>
#include "wiring_private.h"
#include <Keyboard.h>
#include "lin_stack.h"
#include <Wire.h>

//TMDS261B
// Port 1 - Input from TFP410 - Volvo Orig ICM screen (11)
// Port 2 - Input from HDMI plug (10)

//Setup LIN serials on SERCOMs
Uart LinSlave(&sercom1, 37, 35, SERCOM_RX_PAD_1, UART_TX_PAD_0); 
void SERCOM1_Handler()
{
  LinSlave.IrqHandler();
}

Uart LinMaster(&sercom5, 7, 6, SERCOM_RX_PAD_3, UART_TX_PAD_2); 
void SERCOM5_Handler()
{
  LinMaster.IrqHandler();
}

uint8_t icm_req_data_size=8; // length of uint8_t array
uint8_t icm_req_data[8]; // uint8_t array for received data
uint8_t sws_resp_data_size=8;
uint8_t sws_resp_data[8];

#define DISP_ICM 1;
#define DISP_EXT 2;

lin_stack master(&LinMaster);
lin_stack slave(&LinSlave);

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

void setup() {
  pinPeripheral(37, PIO_SERCOM);
  pinPeripheral(35, PIO_SERCOM);
  pinPeripheral(6, PIO_SERCOM);
  pinPeripheral(7, PIO_SERCOM);

  Serial.begin(115200);
  Serial1.begin(115200);
  Wire.begin();
  Keyboard.begin();

  master.begin();
  slave.begin();

  delay(100);
  pinMode(3, OUTPUT);
  digitalWrite(3, HIGH);
  delay(200);
  digitalWrite(3, LOW);
  delay(200);
  digitalWrite(3, HIGH);

  pinMode(2, OUTPUT);
  pinMode(5, OUTPUT);
  pinMode(8, OUTPUT);
  digitalWrite(2, HIGH);
  digitalWrite(5, HIGH);

  delay(10);
  tfp410_write(0x08, 0b00110101);
  tfp410_write(0x09, 0b00000001);
  tfp410_write(0x0A, 0b10000000);
  tfp410_write(0x33, 0b00000000);
  delay(2);
  tmds261b_write(0x01, 0b11010000);
  tmds261b_write(0x03, 0x80);
}

uint8_t current_sws_data[5] = {0x00, 0x00, 0x00, 0x00, 0xFF};
uint8_t current_sws_base[5] = {0x00, 0x00, 0x00, 0x00, 0xFF};
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
int current_mode = 1;
int last_mode = 1;
int return_to_mode = 0;

unsigned long long lastPollSWSR = 0;
unsigned long long lastExitPressTime = 0;
unsigned long exitHeldSecs = 0;
void loop() {
  //Process LIN input
  if(millis() - lastPollSWSR > 100) {
    lastPollSWSR = millis();
    master.writeRequest(0x20);
    delayMicroseconds(2200);
    while(LinMaster.available() > 0) {
      LinMaster.read();
    }
    delayMicroseconds(7800);
    uint8_t currdta[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    int dtapos = 0;
    while(LinMaster.available() > 0) {
      uint8_t t = LinMaster.read();
      currdta[dtapos] = t;
      dtapos++;
    }

    if(memcmp(current_sws_data, currdta, 5) != 0) {
      memcpy(current_sws_data, currdta, 5);

      if(offset(3, 1)) {
        Serial.println("scroll down");
        if(current_mode == 2) {
          Keyboard.press(KEY_DOWN_ARROW);
          delay(20);
          Keyboard.release(KEY_DOWN_ARROW);
        }
      }

      if(offset(2, 34) && offset(4, -34)) {
        Serial.println("scroll up");
        if(current_mode == 2) {
          Keyboard.press(KEY_UP_ARROW);
          delay(20);
          Keyboard.release(KEY_UP_ARROW);
        }
      }

      if(offset(0,0) && offset(1, 64) && offset(2, 0) && offset(3, 0) && offset(4, -64)) {
        Serial.println("exit");
        lastExitPressTime = millis();
        if(current_mode == 2) {
          Keyboard.press(KEY_BACKSPACE);
          delay(20);
          Keyboard.release(KEY_BACKSPACE);
        }
      }

      else if(offset(0,0) && offset(1, 4) && offset(2, 0) && offset(3, 0) && offset(4, -4)) {
        Serial.println("voice");
        if(current_mode == 2) {
          Keyboard.press('v');
          delay(20);
          Keyboard.release('v');
        }
      }

      else if(offset(0,0) && offset(1, 0) && offset(2, 1) && offset(3, 0) && offset(4, -1)) {
        Serial.println("vol up");
        if(current_mode == 2) {
          while(LinSlave.read() != 32) { }
          slave.writeResponse(current_sws_data, 5);
        }
      }

      else if(offset(0,0) && offset(1, 128) && offset(2, 0) && offset(3, 0) && offset(4, -128)) {
        Serial.println("vol dn");
        if(current_mode == 2) {
          while(LinSlave.read() != 32) { }
          slave.writeResponse(current_sws_data, 5);
        }
      }

      else if(offset(0,0) && offset(1, 2) && offset(2, 0) && offset(3, 0) && offset(4, -2)) {
        Serial.println("reverse");
        if(current_mode == 2) {
          Keyboard.press(KEY_LEFT_ARROW);
          delay(20);
          Keyboard.release(KEY_LEFT_ARROW);
        }
      }

      else if(offset(0,0) && offset(1, 16) && offset(2, 0) && offset(3, 0) && offset(4, -16)) {
        Serial.println("forward");
        if(current_mode == 2) {
          Keyboard.press(KEY_RIGHT_ARROW);
          delay(20);
          Keyboard.release(KEY_RIGHT_ARROW);
        }
      }

      else if(offset(0,0) && offset(1, 32) && offset(2, 0) && offset(3, 0) && offset(4, -32)) {
        Serial.println("click!");
        if(current_mode == 2) {
          Keyboard.press(KEY_RETURN);
          delay(20);
          Keyboard.release(KEY_RETURN);
        }
      } else {
        if(offset(4, 255) || offset(4, -255)) {

        } else {
          if(offset(0, 0) && offset(1, 0) && offset(2, 0) && offset(3, 0) && offset(4, 0)) {
            
          } else {
            memcpy(current_sws_base, current_sws_data, 5);
            while(LinSlave.read() != 32) { }
            slave.writeResponse(current_sws_base, 5);
          }
        }
      }
    }
  }

  if(millis() - lastExitPressTime < 1000) {
    exitHeldSecs++;
    if(exitHeldSecs > 150) {
      if(current_mode == 2) {
        current_mode = 1;
      } else {
        current_mode = 2;
      }
      exitHeldSecs = 0;
    }
  } else {
    exitHeldSecs = 0;
  }


  //Proxy all if in ICM mode.
  if(current_mode == 1) {
    slave.writeResponse(current_sws_data, 5);
    delayMicroseconds(16500);
  }

  if(Serial1.available() > 0) {
    String str = Serial1.readStringUntil('\n');
    Serial.println(str);

    if(str.equals("TUN:1")) {
      Keyboard.press(KEY_UP_ARROW);
      delay(20);
      Keyboard.release(KEY_UP_ARROW);
    } else if(str.equals("TUN:-1")) {
      Keyboard.press(KEY_DOWN_ARROW);
      delay(20);
      Keyboard.release(KEY_DOWN_ARROW);
    }

    if(str.equals("REW:prs")) {
      Keyboard.press(KEY_LEFT_ARROW);
    } else if(str.equals("REW:rls")) {
      Keyboard.release(KEY_LEFT_ARROW);
    }

    if(str.equals("FOR:prs")) {
      Keyboard.press(KEY_RIGHT_ARROW);
    } else if(str.equals("FOR:rls")) {
      Keyboard.release(KEY_RIGHT_ARROW);
    }

    if(str.equals("VOL:1")) {
      Serial.println(str);
    } else if(str.equals("VOL:-1")) {
      Serial.println(str);
    }

    if(str.equals("CAM:prs") || str.equals("NAV:prs") || str.equals("TEL:prs")) {
      current_mode = 1;
    }

    if(str.equals("EXIT:prs")) {
      Keyboard.press(KEY_BACKSPACE);
    } else if(str.equals("EXIT:rls")) {
      Keyboard.release(KEY_BACKSPACE);
    }

    if(str.equals("OK:prs")) {
      Keyboard.press(KEY_RETURN);
    } else if(str.equals("OK:rls")) {
      Keyboard.release(KEY_RETURN);
    }

    if(str.equals("PWR:prs")) {
      Serial.println(str);
    } else if(str.equals("PWR:rls")) {
      Serial.println(str);
    }

    if(str.indexOf("PT") != -1) {
      //pass temp
      Serial.println(str);
    }
    if(str.indexOf("FS") != -1) {
      //fan speed
      Serial.println(str);
    }
    if(str.indexOf("DT") != -1) {
      //driver temp
      Serial.println(str);
    }

    if(str.equals("nums_all:rls")) {
      Keyboard.releaseAll();
    }

    if(str.equals("1:prs")) {
      Keyboard.press('1');
    }

    if(str.equals("2:prs")) {
      Keyboard.press('2');
    }

    if(str.equals("3:prs")) {
      Keyboard.press('3');
    }

    if(str.equals("4:prs")) {
      Keyboard.press('4');
    }

    if(str.equals("5:prs")) {
      Keyboard.press('5');
    }

    if(str.equals("6:prs")) {
      Keyboard.press('6');
    }

    if(str.equals("7:prs")) {
      Keyboard.press('7');
    }

    if(str.equals("8:prs")) {
      Keyboard.press('8');
    }

    if(str.equals("9:prs")) {
      Keyboard.press('9');
    }

    if(str.equals("*:prs")) {
      Keyboard.press('*');
    }

    if(str.equals("0:prs")) {
      Keyboard.press('0');
    }

    if(str.equals("#:prs")) {
      Keyboard.press('#');
    }
    
    if(str.equals("DRIVE")) {
      current_mode = return_to_mode;
      Serial.println("returning to return mode");
    }

    if(str.equals("REVERSE")) {
      Serial.println("REVERSING");
      return_to_mode = current_mode;
      current_mode = 1;
    }
  }

  if(current_mode != last_mode) {
    last_mode = current_mode;

    if(current_mode == 1) {
      Serial.println("cmd: SWITCH to ICM");
      tmds261b_write(0x01, 0b11010000);
    } else {
      Serial.println("cmd: SWITCH to EXT");
      tmds261b_write(0x01, 0b10010000);
    }
  }
}