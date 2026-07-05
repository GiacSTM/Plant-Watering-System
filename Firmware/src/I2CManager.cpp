#include "I2CManager.h"

I2C_Manager::I2C_Manager(uint8_t sda_pin, uint8_t scl_pin)
    : sda(sda_pin), scl(scl_pin)
{}

void I2C_Manager::begin()
{
    Wire.begin(sda, scl);
}



//==================================RTC==================================================

RTC_MANAGER::RTC_MANAGER()
{}


// Il PCF8563 scrive nei registri in formato BCD (23 -> 0010 0011)
static uint8_t toBCD(int v)   { return ((v / 10) << 4) | (v % 10); }
static int     fromBCD(uint8_t v) { return ((v >> 4) * 10) + (v & 0x0F); }

void RTC_MANAGER::writeReg(uint8_t reg, uint8_t val)
{
    Wire.beginTransmission(PCF8563_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

uint8_t RTC_MANAGER::readReg(uint8_t reg)
{
    Wire.beginTransmission(PCF8563_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom(PCF8563_ADDR, 1);
    return Wire.read();
}

bool RTC_MANAGER::begin()
{
    // verifica comunicazione
    Wire.beginTransmission(PCF8563_ADDR);
    bool ok = (Wire.endTransmission() == 0);

    // pulisce STAT1 e STAT2
    writeReg(PCF8563_STAT1_REG, 0x00);
    writeReg(PCF8563_STAT2_REG, 0x00);

    return ok;
}


void RTC_MANAGER::setDate(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second)
{
    writeReg(0x02, toBCD(second) & 0x7F);   // VL=0, secondi
    writeReg(0x03, toBCD(minute) & 0x7F);   // minuti
    writeReg(0x04, toBCD(hour)   & 0x3F);   // ore
    writeReg(0x05, toBCD(day)    & 0x3F);   // giorno
    writeReg(0x06, 0x00);                    // weekday (non usato)
    writeReg(0x07, toBCD(month)  & 0x1F);   // mese
    writeReg(0x08, toBCD(year % 100));       // anno (ultime 2 cifre)
}


void RTC_MANAGER::Update()
{
    _second = fromBCD(readReg(0x02) & 0x7F);
    _minute = fromBCD(readReg(0x03) & 0x7F);
    _hour   = fromBCD(readReg(0x04) & 0x3F);
    _day    = fromBCD(readReg(0x05) & 0x3F);
    _month  = fromBCD(readReg(0x07) & 0x1F);
    _year   = 2000 + fromBCD(readReg(0x08));
}

int RTC_MANAGER::getSecond() { return _second; }
int RTC_MANAGER::getMinute() { return _minute; }
int RTC_MANAGER::getHour()   { return _hour;   }
int RTC_MANAGER::getDay()    { return _day;    }
int RTC_MANAGER::getMount()  { return _month;  }
int RTC_MANAGER::getYear()   { return _year;   }



void RTC_MANAGER::setAlarmParms(uint8_t hours, uint8_t minutes)
{
    alarm_hours   = hours;
    alarm_minutes = minutes;
}

bool RTC_MANAGER::startAlarm()
{
    // controlla VL
    if (readReg(0x02) & 0x80) {
        Serial.println("VL=1: clock non affidabile, controllare Vbat");
    }

    Update();
    uint8_t hour   = _hour;
    uint8_t minute = _minute + alarm_minutes;

    if (minute >= 60) {
        minute -= 60;
        hour = hour + 1 + alarm_hours;
        if (hour >= 24) hour -= 24;
    } else {
        hour = hour + alarm_hours;
        if (hour >= 24) hour -= 24;
    }



    // scrivi registri allarme
    Wire.beginTransmission(PCF8563_ADDR);
    Wire.write(PCF8563_ALRM_MIN);
    Wire.write(toBCD(minute) & 0x7F);   // AE=0, minuti attivi
    Wire.write(toBCD(hour)   & 0x3F);   // AE=0, ore attive
    Wire.write(0x80);                    // day  ignorato
    Wire.write(0x80);                    // wday ignorato
    Wire.endTransmission();

    // abilita AIE, pulisce AF
    uint8_t stat2 = readReg(PCF8563_STAT2_REG);
    stat2 &= ~PCF8563_ALARM_AF;
    stat2 |=  PCF8563_ALARM_AIE;
    writeReg(PCF8563_STAT2_REG, stat2);

    return true;
}

void RTC_MANAGER::clearAlarm()
{
    uint8_t stat2 = readReg(PCF8563_STAT2_REG);
    stat2 &= ~(PCF8563_ALARM_AF | PCF8563_ALARM_AIE);
    writeReg(PCF8563_STAT2_REG, stat2);
}

void RTC_MANAGER::startTimer_sec(uint8_t sec)
{
    uint8_t stat2 = readReg(PCF8563_STAT2_REG);
    stat2 &= ~(PCF8563_TIMER_TF | PCF8563_TIMER_TIE | PCF8563_ALARM_AF);
    writeReg(PCF8563_STAT2_REG, stat2);

    writeReg(PCF8563_TIMER1_REG, 0x82);  // TE=1, TD=10 (1Hz)
    writeReg(PCF8563_TIMER2_REG, sec);   // countdown

    stat2 |= PCF8563_TIMER_TIE;
    writeReg(PCF8563_STAT2_REG, stat2);
}

void RTC_MANAGER::resetTimer()
{
    uint8_t stat2 = readReg(PCF8563_STAT2_REG);
    stat2 &= ~(PCF8563_TIMER_TF | PCF8563_TIMER_TIE);
    writeReg(PCF8563_STAT2_REG, stat2);
    writeReg(PCF8563_TIMER1_REG, 0x00);
}


uint32_t RTC_MANAGER::getTimeStamp()
{
    Update();
    tmElements_t tm;
    tm.Second = _second;
    tm.Minute = _minute;
    tm.Hour   = _hour;
    tm.Day    = _day;
    tm.Month  = _month;
    tm.Year   = _year - 1970;
    return (uint32_t)makeTime(tm) - TIMEZONE_OFFSET_SEC;
}

void RTC_MANAGER::getAlarmParms(int parms[]) { parms[0] = alarm_hours; parms[1] = alarm_minutes; }


//==================================OLED==================================================

OLED_MANAGER::OLED_MANAGER(): display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET)
{}

bool OLED_MANAGER::begin()
{
    if (!display.begin(SSD1306_SWITCHCAPVCC, DEVICE::OLED)) return false;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.display();
    return true;
}

void OLED_MANAGER::clearScreen()
{
    display.clearDisplay();
    display.display();
}

void OLED_MANAGER::displaySensorValues(float soilHum, float airHum, float tempC)
{
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0,  0); display.printf("Terra: %.1f%%", soilHum);
    display.setCursor(0, 11); display.printf("Aria : %.1f%%", airHum);
    display.setCursor(0, 22); display.printf("Temp : %.1f C",  tempC);
    display.display();
}

void OLED_MANAGER::setDisplayOn()
{
    if (!isDisplayAlreadOn) {
        display.clearDisplay();
        display.ssd1306_command(SSD1306_DISPLAYON);
        isDisplayAlreadOn = true;
    }
}

void OLED_MANAGER::setDisplayOff()
{
    display.clearDisplay();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    isDisplayAlreadOn = false;
}

void OLED_MANAGER::Print(const char* stringa)
{
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.printf(stringa);
    display.display();
}
