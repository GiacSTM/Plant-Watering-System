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
    uint8_t getSoilHumTargetLOW();
    uint8_t getSoilHumTargetHIGH();
    void setSoilHumTarget(uint8_t low, uint8_t high);

    bool getPumpStatus();
    void setPumpStatus(bool status);
    
private:
    float SoilHumTargetLOW = 40.0;
    float SoilHumTargetHIGH = 60.0;
    bool PumpStatus = false;
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
