#ifndef DatabaseManager_H  
#define DatabaseManager_H

#include <FS.h>
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <I2CManager.h>

struct Reading {
    uint32_t TimeStamp;
    float AirHum;
    float AirTemp;
    float SoilHum;
    bool isPumpToActivate;
};

struct PaginationInfo {
    int recordsRead;
    long remainingRecords;
};

class DatabaseManager;

class WIFI_CONNECTION {  
public:
    WIFI_CONNECTION(String WIFI_NAME, String WIFI_PASS, DatabaseManager& db, RTC_MANAGER& rtc);
    void startAccessPoint();
    void stopAccessPoint();

private:
    void createHandlers();
    String WIFI_NAME;
    String WIFI_PASS;
    AsyncWebServer server;
    bool isWifiOn = false;

    DatabaseManager& db_ref;
    RTC_MANAGER& rtc_ref;
};


class SystemParameters 
{
public:  
    uint8_t getSoilHumTarget();
    void setSoilHumTarget(uint8_t target);

    bool getPumpStatus();
    void setPumpStatus(bool status);

    uint8_t getTimePumpFullPWM_sec();
    void setTimePumpFullPWM_sec(uint8_t secs);

    uint8_t getPWM_rate();
    void setPWM_rate(uint8_t rate);

private:
    float SoilHumTarget = 10.0;
    bool PumpStatus = false;
    int TimePumpFullPWM_sec = 2;
    int PWM_rate = 50;
};



class DatabaseManager {
public:
    DatabaseManager(String ssid, String pass, RTC_MANAGER& rtc);
    WIFI_CONNECTION WIFI;
    SystemParameters PARMS;

    void insert(Reading row);
    PaginationInfo getRecordsPage(Reading* arrayOutput, int limit, int pageOffset);
    void _clear_file();

private:
    int appendFile(const char *message);
    uint16_t getLastRowID();
    const String PATH = "/Readings.csv";
    const int RECORD_SIZE = 48;
};

#endif
