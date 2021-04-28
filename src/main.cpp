#include <Arduino.h>
#include "wiring_private.h"
#include <Keyboard.h>
#include "lin_stack.h"
#include <Wire.h>

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

  delay(400);
  tfp410_write(0x08, 0b0110101);
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
  if(Serial1.available() > 0) {
    String str = Serial1.readStringUntil('\n');
    
    if(str.equals("TUN:1")) {
      Keyboard.press(KEY_RIGHT_ARROW);
      delay(20);
      Keyboard.release(KEY_RIGHT_ARROW);
    } else if(str.equals("TUN:-1")) {
      Keyboard.press(KEY_LEFT_ARROW);
      delay(20);
      Keyboard.release(KEY_LEFT_ARROW);
    }

    if(str.equals("VOL:1")) {
      Serial.println(str);
    } else if(str.equals("VOL:-1")) {
      Serial.println(str);
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
      //pass temp
      Serial.println(str);
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