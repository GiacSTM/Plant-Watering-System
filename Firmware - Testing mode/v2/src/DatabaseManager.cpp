#include "DatabaseManager.h"

//===========================================WIFI===================================================================

WIFI_CONNECTION::WIFI_CONNECTION(String WIFI_NAME, String WIFI_PASS, DatabaseManager& db, RTC_MANAGER& rtc)
    : WIFI_NAME(WIFI_NAME), WIFI_PASS(WIFI_PASS), db_ref(db), rtc_ref(rtc), server(80)
{
    if (!LittleFS.begin()) {
        LittleFS.format();
        LittleFS.begin();
    }
    createHandlers();
}


void WIFI_CONNECTION::startAccessPoint()
{
    if (!isWifiOn) {
        WiFi.forceSleepWake();
        delay(10);
        WiFi.mode(WIFI_AP);
        WiFi.softAP(WIFI_NAME, WIFI_PASS);
        server.begin();
        isWifiOn = true;
    }
}

void WIFI_CONNECTION::stopAccessPoint()
{
    if (isWifiOn) {
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        delay(100);
        server.end();
        WiFi.forceSleepBegin();
        isWifiOn = false;
    }
}

void WIFI_CONNECTION::createHandlers()
{
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        if (LittleFS.exists("/www/index.html"))
            request->send(LittleFS, "/www/index.html", "text/html");
        else
            request->send(200, "text/plain", "FILE NON TROVATO");
    });

    server.on("/csv", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(LittleFS, "/Readings.csv", "text/plain");
    });

    server.on("/data", HTTP_GET, [this](AsyncWebServerRequest *request){
        const int LIMIT = 20;
        static Reading readings[LIMIT];
        PaginationInfo info = db_ref.getRecordsPage(readings, LIMIT, 0);

        // per evitare errori dovuti a letture corrotte
        auto fmtFloat = [](float v) -> String {
            if (isnan(v)) return "null";
            return String(v, 2);
        };

        String json;
        json.reserve(512);
        json = "{\"readings\":[";
        for (int i = info.recordsRead - 1; i >= 0; i--) {
            if (i < info.recordsRead - 1) json += ",";
            json += "{\"id\":";         json += (i + 1);
            json += ",\"timestamp\":";  json += readings[i].TimeStamp;
            json += ",\"temp\":";       json += fmtFloat(readings[i].AirTemp);
            json += ",\"humA\":";       json += fmtFloat(readings[i].AirHum);
            json += ",\"humS\":";       json += fmtFloat(readings[i].SoilHum);
            json += ",\"pump\":";       json += readings[i].isPumpToActivate ? "true" : "false";
            json += "}";
        }
        json += "],\"remaining\":"; json += info.remainingRecords;
        json += "}";

        request->send(200, "application/json", json);
    });

    server.on("/updateConfigParms", HTTP_GET, [this](AsyncWebServerRequest *request){
        int alarmParms[2];
        rtc_ref.getAlarmParms(alarmParms);
        int fullSpeedTime = db_ref.PARMS.getTimePumpFullPWM_sec();
        int holdPWM = db_ref.PARMS.getPWM_rate();

        String json;
        json.reserve(64);
        json += "{\"TargetHum\":";
        json += db_ref.PARMS.getSoilHumTarget();
        json += ",\"AlarmHours\":";
        json += alarmParms[0];
        json += ",\"AlarmMinutes\":";
        json += alarmParms[1];
        json += ",\"fullSpeedTime\":";
        json += fullSpeedTime;
        json += ",\"holdPWM\":";
        json += holdPWM;
        json +=  "}";

        request->send(200, "application/json", json);
    });

    server.on("/control/target", HTTP_POST,
        [](AsyncWebServerRequest *request){ request->send(200); },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            int target;
            sscanf((char*)data, "{\"soilTarget\":%d}", &target);
            db_ref.PARMS.setSoilHumTarget(target);  
        }
    );
    
    server.on("/control/pump", HTTP_POST,
        [](AsyncWebServerRequest *request){ request->send(200); },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            char onStr[6];
            sscanf((char*)data, "{\"on\":%5s}", onStr);
            bool on = (strncmp(onStr, "true", 4) == 0);
            db_ref.PARMS.setPumpStatus(on); 
        }
    );

    server.on("/control/pump-advanced", HTTP_POST,
        [](AsyncWebServerRequest *request){ request->send(200); },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            int pumpFullTime;
            int ragePWM;
            sscanf((char*)data, "{\"fullSpeedTime\":%d,\"holdPWM\":%d}", &pumpFullTime, &ragePWM);
            
            db_ref.PARMS.setTimePumpFullPWM_sec(pumpFullTime);
            db_ref.PARMS.setPWM_rate(ragePWM);
        }
    );

    server.on("/config/time", HTTP_POST,
        [](AsyncWebServerRequest *request){ request->send(200); },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            int year, month, day, hour, minute, second;
            sscanf((char*)data,
                "{\"year\":%d,\"month\":%d,\"day\":%d,\"hour\":%d,\"minute\":%d,\"second\":%d}",
                &year, &month, &day, &hour, &minute, &second);

    
            rtc_ref.setDate(year, month, day, hour, minute, second);
            rtc_ref.clearAlarm();
            rtc_ref.startAlarm();
        }
    );

    server.on("/config/alarm", HTTP_POST,
        [](AsyncWebServerRequest *request){ request->send(200); },
        NULL,
        [this](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
            int hour, minute;
            sscanf((char*)data, "{\"hour\":%d,\"minute\":%d}", &hour, &minute);

            rtc_ref.setAlarmParms((uint8_t)hour, (uint8_t)minute);
            rtc_ref.clearAlarm();
            rtc_ref.startAlarm();
        }
    );

    
}

//===========================================DATABASE===================================================================

DatabaseManager::DatabaseManager(String ssid, String pass, RTC_MANAGER& rtc)
    : WIFI(ssid, pass, *this, rtc)
{}

void DatabaseManager::insert(Reading row)
{
    char buffer[RECORD_SIZE + 1];
    int last_row = getLastRowID();
    snprintf(buffer, sizeof(buffer), "%05u,%10u,%06.2f,%06.2f,%06.2f,%d       \r\n",
        last_row + 1, row.TimeStamp, row.AirTemp, row.AirHum, row.SoilHum,
        row.isPumpToActivate ? 1 : 0);
    appendFile(buffer);
}

PaginationInfo DatabaseManager::getRecordsPage(Reading* arrayOutput, int limit, int pageOffset)
{
    File file = LittleFS.open(PATH, "r");
    PaginationInfo info = {0, 0};
    if (!file) return info;

    long totalRecords = file.size() / RECORD_SIZE;
    long recordsAvailable = totalRecords - pageOffset;
    if (recordsAvailable <= 0) { file.close(); return info; }

    int toRead = (recordsAvailable < limit) ? recordsAvailable : limit;
    long startByte = (totalRecords - pageOffset - toRead) * RECORD_SIZE;
    file.seek(startByte);

    for (int i = 0; i < toRead; i++) {
        char buffer[RECORD_SIZE + 1];
        file.readBytes(buffer, RECORD_SIZE);
        buffer[RECORD_SIZE] = '\0';
        int pumpVal;
        sscanf(buffer, "%*u,%u,%f,%f,%f,%d",
               &arrayOutput[i].TimeStamp,
               &arrayOutput[i].AirTemp,
               &arrayOutput[i].AirHum,
               &arrayOutput[i].SoilHum,
               &pumpVal);
        arrayOutput[i].isPumpToActivate = (pumpVal == 1);
    }

    info.recordsRead = toRead;
    info.remainingRecords = totalRecords - pageOffset - toRead;
    file.close();
    return info;
}

int DatabaseManager::appendFile(const char *message)
{
    File file = LittleFS.open(PATH, "a");
    if (!file) return 0;
    bool success = file.print(message);
    file.close();
    return success ? 1 : -1;
}

uint16_t DatabaseManager::getLastRowID()
{
    File file = LittleFS.open(PATH, "r");
    if (!file || file.size() < RECORD_SIZE) { if (file) file.close(); return 0; }
    long lastRowPos = file.size() - RECORD_SIZE;
    if (!file.seek(lastRowPos)) { file.close(); return 0; }
    char idBuffer[6];
    file.readBytes(idBuffer, 5);
    idBuffer[5] = '\0';
    file.close();
    return (uint16_t)atoi(idBuffer);
}

void DatabaseManager::_clear_file()
{
    File file = LittleFS.open(PATH, "w");
    if (file) file.close();
}


//====================================SystemParameters=========================================
uint8_t SystemParameters::getSoilHumTarget() { return SoilHumTarget; }
void SystemParameters::setSoilHumTarget(uint8_t target) { SoilHumTarget = target; }
bool SystemParameters::getPumpStatus() { return PumpStatus; }
void SystemParameters::setPumpStatus(bool status) { PumpStatus = status; }
uint8_t SystemParameters::getTimePumpFullPWM_sec() { return TimePumpFullPWM_sec; }
void SystemParameters::setTimePumpFullPWM_sec(uint8_t secs) { TimePumpFullPWM_sec = secs; }
uint8_t SystemParameters::getPWM_rate() { return PWM_rate; }
void SystemParameters::setPWM_rate(uint8_t rate) {PWM_rate = rate; }
