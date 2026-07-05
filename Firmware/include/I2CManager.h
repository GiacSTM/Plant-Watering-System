#ifndef I2CManager_H  
#define I2CManager_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_GFX.h>
#include <TimeLib.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 32
#define OLED_RESET   -1

#define PCF8563_ADDR        0x51
#define PCF8563_STAT1_REG   0x00
#define PCF8563_STAT2_REG   0x01
#define PCF8563_ALRM_MIN    0x09
#define PCF8563_ALARM_AF    0x08
#define PCF8563_ALARM_AIE   0x02
#define PCF8563_TIMER_TF    0x04
#define PCF8563_TIMER_TIE   0x01
#define PCF8563_TIMER1_REG  0x0E
#define PCF8563_TIMER2_REG  0x0F
#define TIMEZONE_OFFSET_SEC (2 * 3600)  // UTC+2

enum DEVICE { OLED = 0x3C, RTC = 0x51 };

class OLED_MANAGER {
public:
    OLED_MANAGER();
    bool begin();
    void clearScreen();
    void Print(const char*);
    void displaySensorValues(float soilHum, float airHum, float tempC);
    void setDisplayOn();
    void setDisplayOff();
private:
    Adafruit_SSD1306 display;
    bool isDisplayAlreadOn = false;
};

class RTC_MANAGER {
public:
    RTC_MANAGER();
    bool begin();

    void setDate(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);
    void Update();
    int getSecond();
    int getMinute();
    int getHour();
    int getDay();
    int getMount();
    int getYear();
    bool startAlarm();
    void clearAlarm();
    void startTimer_sec(uint8_t sec);
    void resetTimer();
    uint32_t getTimeStamp();
    void setAlarmParms(uint8_t hours, uint8_t minutes);

    void getAlarmParms(int parms[]);

private:
    int _second, _minute, _hour, _day, _month, _year;
    int alarm_hours   = 0;
    int alarm_minutes = 2;

    uint8_t readReg(uint8_t reg);
    void    writeReg(uint8_t reg, uint8_t val);
};

class I2C_Manager {
public:
    OLED_MANAGER OLED;
    RTC_MANAGER  RTC;
    I2C_Manager(uint8_t sda, uint8_t scl);
    void begin();
private:
    uint8_t sda;
    uint8_t scl;
};

#endif
