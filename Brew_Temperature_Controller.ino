/*
 * Brewing Temperature Controller
 * 
 * Copyright (C) 2020 Martyn Welch <martyn@warpedlogic.co.uk>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA  02110-1301, USA.
 */

/*
 * The following libraries are used:
 * - NTC Thermistor (MIT): https://github.com/YuriiSalimov/NTC_Thermistor
 * - U8g2 (2-Clause BSD): https://github.com/olikraus/u8g2
 * - FastPID (LGPL v2.1): https://github.com/mike-matera/FastPID
 */

#include <Arduino.h>
#include <U8g2lib.h>
#include <Thermistor.h>
#include <NTC_Thermistor.h>
#include <FastPID.h>
#include <EEPROM.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#define SWITCH_1 5
#define SWITCH_2 4
#define THERM_PIN A0
#define LIGHT_PIN 6
#define OUTPUT_PIN 3

#define REFERENCE_RESISTANCE   4700
#define NOMINAL_RESISTANCE     100000
#define NOMINAL_TEMPERATURE    25
#define B_VALUE                3950

#define DEBOUNCE_PERIOD 1000

#define LIGHT_PERIOD 2000
unsigned long light_time = 0;

float Kp=25, Ki=0.2, Kd=0.01, Hz=37;
int output_bits = 8;
bool output_signed = false;

FastPID myPID(Kp, Ki, Kd, Hz, output_bits, output_signed);

U8G2_PCD8544_84X48_F_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/ 13, /* data=*/ 11, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);  // Nokia 5110 Display

unsigned long debounce_switch_1 = 0;
unsigned long debounce_switch_2 = 0;

#define SETMAX 40
#define SETMIN 0
int setpoint = 25;
char setchar[4];
#define NUM_TEMPS 20
double temps[NUM_TEMPS];
int temp_cnt = 0;
int current = 69;
char curchar[4];
uint8_t output = 0;
char outchar[5];
unsigned long log_time;

int state1;
int hold1 = 0;
bool change1 = true;

int state2;
int hold2 = 0;
bool change2 = true;

Thermistor* thermistor;

#define SAVE_MAGIC 0xbe
#define SAVE_ADDRESS 0
struct saved_state {
  int magic;
  int temp;
};

struct saved_state saved;

void setup(void) {

  u8g2.begin();

  Serial.begin(115200);

  pinMode(SWITCH_1, INPUT_PULLUP);
  pinMode(SWITCH_2, INPUT_PULLUP);
  pinMode(LIGHT_PIN, OUTPUT);
  digitalWrite(LIGHT_PIN, HIGH);
  pinMode(OUTPUT_PIN, OUTPUT);
  digitalWrite(OUTPUT_PIN, LOW);

  thermistor = new NTC_Thermistor(
    THERM_PIN,
    REFERENCE_RESISTANCE,
    NOMINAL_RESISTANCE,
    NOMINAL_TEMPERATURE,
    B_VALUE
  );

  Serial.println("Setpoint, Output, Temperature");

  EEPROM.get(SAVE_ADDRESS, saved);

  if (saved.magic == SAVE_MAGIC) {
    setpoint = saved.temp;
  } else {
    saved.magic = SAVE_MAGIC;
  }

}

/*
uint8_t bitmap[] = {0x3F, 0xFF, 0xA5, 0xA5, 0xA1, 0xE1, 0x21, 0x1E};
*/
void refresh_display(char *curchar, char *setchar, char *powerchar) {
  // Write current temp and units
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_inb30_mf);
  u8g2.drawStr(18, 30, curchar);
  u8g2.setFont(u8g2_font_profont12_tf);
  u8g2.drawGlyph(70, 10, 176);
  u8g2.drawStr(77, 10, "C");

  // Draw set temp box
  u8g2.drawRFrame(15, 34, 20, 14, 3);
  u8g2.drawRBox(0, 36, 18, 9, 3);
  u8g2.setFont(u8g2_font_micro_tr);
  u8g2.setDrawColor(0);
  u8g2.drawStr(3, 43,"Set");
  u8g2.setDrawColor(1);

  // Draw power box
  u8g2.drawRFrame(51, 34, 33, 14, 3);
  u8g2.drawRBox(36, 36, 18, 9, 3);
  u8g2.setFont(u8g2_font_micro_tr);
  u8g2.setDrawColor(0);
  u8g2.drawStr(39, 43,"PWR");
  u8g2.setDrawColor(1);

  // Power state and set temp
  u8g2.setFont(u8g2_font_profont12_tf);
  u8g2.drawStr(56, 45, powerchar);
  u8g2.drawStr(20, 45, setchar);
/*
  // Beer Mug Graphic
  u8g2.drawBitmap(0,0,1,8,bitmap);
*/
  // Transfer buffer to the display
  u8g2.sendBuffer();
}


void loop(void) {
  unsigned long now;
  int state;
  double tot = 0;
  double avg_temp;
  char pwrchar[4];
  double current;
  uint8_t pcnt;

  now = millis();
  
  state = digitalRead(SWITCH_1);
  if (state == state1) {
    if ((now - debounce_switch_1) < DEBOUNCE_PERIOD) {
      if ((state == 0) & (change1 == true)) {
        if (setpoint < SETMAX)
          setpoint++;
        change1 = false;

      }
      debounce_switch_1 = now;
      hold1++;
      if ((state == 0) & (hold1 > 10))
        if (setpoint < SETMAX)
          setpoint++;

      if (state == 0) {
        light_time = now;
        digitalWrite(LIGHT_PIN, LOW);
      }
    }
  } else {
    debounce_switch_1 = now;
    state1 = state;
    change1 = true;
    hold1 = 0;
  }
  
  state = digitalRead(SWITCH_2);
  if (state == state2) {
    if ((now - debounce_switch_2) < DEBOUNCE_PERIOD) {
      if ((state == 0) & (change2 == true)) {
        if (setpoint > SETMIN)
          setpoint--;
        change2 = false;
      }
      debounce_switch_2 = now;
      hold2++;
      if ((state == 0) & (hold2 > 10))
        if (setpoint > SETMIN)
          setpoint--;

      if (state == 0) {
        light_time = now;
        digitalWrite(LIGHT_PIN, LOW);
      }
    }
  } else {
    debounce_switch_2 = now;
    state2 = state;
    change2 = true;
    hold2 = 0;
  }

  saved.temp = setpoint;
  EEPROM.put(SAVE_ADDRESS, saved);

  if (now - light_time > LIGHT_PERIOD)
    digitalWrite(LIGHT_PIN, HIGH);

  current = thermistor->readCelsius();
  temps[temp_cnt++] = current;
  temp_cnt = temp_cnt % 20;

  for (int i=0; i < NUM_TEMPS; i++)
    tot += temps[i];

  output = myPID.step(setpoint, current);

  avg_temp = tot / NUM_TEMPS;

  if (avg_temp < 0) {
    sprintf(curchar, "XX");
  } else {
    sprintf(curchar,"%d", round(avg_temp));
  }

  itoa(setpoint, setchar, 10);

  if (output > 255)
    output = 255;

  /* Protect against unplugged or broken temp sensor */
  if (current < 0) {
    output = 0;
  }

  analogWrite(OUTPUT_PIN, output);

  pcnt = (output * 100) / 255;
  if (output == 0) {
    sprintf(outchar, "Off");
  } else {
    sprintf(outchar,"%d%%", pcnt);
  }

  refresh_display(curchar, setchar, outchar);

  /* Limit logging rate */
  if (now - log_time > 1000) {
    log_time = log_time + 1000;
    Serial.print(setpoint);
    Serial.print(", ");
    Serial.print(output);
    Serial.print(", ");
    Serial.println(current);
  }
}
