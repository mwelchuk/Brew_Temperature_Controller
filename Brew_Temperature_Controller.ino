/*
 * Brewing Temperature Controller
 * 
 * The following libraries need to be installed for this to work:
 * - NTC Thermistor (MIT): https://github.com/YuriiSalimov/NTC_Thermistor
 * - U8g2 (2-Clause BSD): https://github.com/olikraus/u8g2
 * - FastPID (LGPL v2.1): https://github.com/mike-matera/FastPID
 */

#include <Arduino.h>
#include <U8g2lib.h>
#include <Thermistor.h>
#include <NTC_Thermistor.h>
#include <FastPID.h>

#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif

#define SWITCH_1 5
#define SWITCH_2 4
#define THERM_PIN A0

#define REFERENCE_RESISTANCE   4700
#define NOMINAL_RESISTANCE     100000
#define NOMINAL_TEMPERATURE    25
#define B_VALUE                3950

#define DEBOUNCE_PERIOD 1000

float Kp=0.1, Ki=0.5, Kd=0.1, Hz=10;
int output_bits = 8;
bool output_signed = false;

FastPID myPID(Kp, Ki, Kd, Hz, output_bits, output_signed);

U8G2_PCD8544_84X48_F_4W_SW_SPI u8g2(U8G2_R0, /* clock=*/ 13, /* data=*/ 11, /* cs=*/ 10, /* dc=*/ 9, /* reset=*/ 8);  // Nokia 5110 Display

unsigned long debounce_switch_1 = 0;
unsigned long debounce_switch_2 = 0;

int setpoint = 42;
char setchar[4];
#define NUM_TEMPS 20
double temps[NUM_TEMPS];
int temp_cnt = 0;
int current = 69;
char curchar[4];

int state1;
int hold1 = 0;
bool change1 = true;

int state2;
int hold2 = 0;
bool change2 = true;

Thermistor* thermistor;



void setup(void) {
  u8g2.begin();

  Serial.begin(115200);

  pinMode(SWITCH_1, INPUT_PULLUP);
  pinMode(SWITCH_2, INPUT_PULLUP);

  thermistor = new NTC_Thermistor(
    THERM_PIN,
    REFERENCE_RESISTANCE,
    NOMINAL_RESISTANCE,
    NOMINAL_TEMPERATURE,
    B_VALUE
  );
}

uint8_t bitmap[] = {0x3F, 0xFF, 0xA5, 0xA5, 0xA1, 0xE1, 0x21, 0x1E};

void refresh_display(char *curchar, char *setchar, char *powerchar) {
  // Write current temp and units
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_inb30_mn);
  u8g2.drawStr(18, 30, curchar);
  u8g2.drawGlyph(70, 10, 176);
  u8g2.drawStr(77, 10, "C");

  // Draw set temp box
  u8g2.drawRFrame(15, 34, 28, 14, 3);
  u8g2.drawRBox(0, 36, 18, 9, 3);
  u8g2.setFont(u8g2_font_micro_tr);
  u8g2.setDrawColor(0);
  u8g2.drawStr(3, 43,"Set");
  u8g2.setDrawColor(1);

  // Draw power box
  u8g2.drawRFrame(59, 34, 25, 14, 3);
  u8g2.drawRBox(44, 36, 18, 9, 3);
  u8g2.setFont(u8g2_font_micro_tr);
  u8g2.setDrawColor(0);
  u8g2.drawStr(47, 43,"PWR");
  u8g2.setDrawColor(1);

  // Power state and set temp
  u8g2.setFont(u8g2_font_profont12_tf);
  u8g2.drawStr(64, 45, powerchar);
  u8g2.drawStr(20, 45, setchar);

  // Beer Mug Graphic
  u8g2.drawBitmap(0,0,1,8,bitmap);
  
  // Transfer buffer to the display
  u8g2.sendBuffer();
}


void loop(void) {
  unsigned long now;
  int state;
  double tot = 0;
  char pwrchar[4];
  uint8_t output;
  double current;

  now = millis();

  state = digitalRead(SWITCH_1);
  if (state == state1) {
    if ((now - debounce_switch_1) < DEBOUNCE_PERIOD) {
      if ((state == 0) & (change1 == true)) {
        setpoint++;
        change1 = false;
      }
      debounce_switch_2 = now;
      hold1++;
      if ((state == 0) & (hold1 > 10))
        setpoint++;
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
        setpoint--;
        change2 = false;
      }
      debounce_switch_2 = now;
      hold2++;
      if ((state == 0) & (hold2 > 10))
        setpoint--;
    }
  } else {
    debounce_switch_2 = now;
    state2 = state;
    change2 = true;
    hold2 = 0;
  }

  current = thermistor->readCelsius();
  temps[temp_cnt++] = current;
  temp_cnt = temp_cnt % 20;
  Serial.println(temp_cnt);
  for (int i=0; i < NUM_TEMPS; i++)
    tot += temps[i];

  output = myPID.step(setpoint, current);

  sprintf(curchar,"%d", round(tot / NUM_TEMPS));
  itoa(setpoint, setchar, 10);

  if (output == 0)
    refresh_display(curchar, setchar, "Off");
  else
    refresh_display(curchar, setchar, "On");

  Serial.print("Temperature: ");
  Serial.print(current);
  Serial.println(" C");
}
