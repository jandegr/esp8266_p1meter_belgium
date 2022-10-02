//#include <FS.h>
#include <arduino.h>
#include <EEPROM.h>
#include <DNSServer.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
HardwareSerial receivingSerial = Serial;
#endif
#if defined(ESP32)
#include <Wifi.h>
HardwareSerial receivingSerial = Serial2;
#define RXD2 15
#define TXD2 14
#endif
#include <Ticker.h>
#include <WiFiManager.h>
#if defined(ESP8266)
#include <ESP8266mDNS.h>
#endif
#if defined(ESP32)
#include <ESPmDNS.h>
#endif
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <time.h>

// * Include settings
#include "settings.h"

// * Initiate led blinker library
Ticker ticker;

// * Initiate WIFI client
WiFiClient espClient;

// * Initiate MQTT client
PubSubClient mqtt_client(espClient);

struct tm testTimeInfo;
struct tm testTimeInfoLast;
float kwartierVermogen = 0;
float kwartierVermogenMaand = 0;
float kwartierVermogenVorigeMaand = 0;
float tellerstand = 0;
bool shouldSaveConfig = false;
int startup = 2;
// * Set during CRC checking
unsigned int currentCRC = 0;
// * Set to store received telegram
char telegram[P1_MAXLINELENGTH];
xTaskHandle readerTask;
int badCRC = 0;

// **********************************
// * Ticker (System LED Blinker)    *
// **********************************

// * Blink on-board Led
void tick()
{
    // * Toggle state
    int state = digitalRead(LED_BUILTIN); // * Get the current state of GPIO1 pin
    digitalWrite(LED_BUILTIN, !state);    // * Set pin to the opposite state
}

// **********************************
// * WIFI                           *
// **********************************

// * Gets called when WiFiManager enters configuration mode
void configModeCallback(WiFiManager *myWiFiManager)
{
    Serial.println(F("Entered config mode"));
    Serial.println(WiFi.softAPIP());

    // * If you used auto generated SSID, print it
    Serial.println(myWiFiManager->getConfigPortalSSID());

    // * Entered config mode, make led toggle faster
    ticker.attach(0.2, tick);
}

// **********************************
// * MQTT                           *
// **********************************

// * Send a message to a broker topic
void send_mqtt_message(const char *topic, char *payload)
{
    Serial.printf("MQTT Outgoing on %s: ", topic);
    Serial.println(payload);

    bool result = mqtt_client.publish(topic, payload, false);

    if (!result)
    {
        Serial.printf("MQTT publish to topic %s failed\n", topic);
    }
}

// * Reconnect to MQTT server and subscribe to in and out topics
bool mqtt_reconnect()
{
    // * Loop until we're reconnected
    int MQTT_RECONNECT_RETRIES = 0;

    while (!mqtt_client.connected() && MQTT_RECONNECT_RETRIES < MQTT_MAX_RECONNECT_TRIES)
    {
        MQTT_RECONNECT_RETRIES++;
        Serial.printf("MQTT connection attempt %d / %d ...\n", MQTT_RECONNECT_RETRIES, MQTT_MAX_RECONNECT_TRIES);

        // * Attempt to connect
        if (mqtt_client.connect(HOSTNAME, MQTT_USER, MQTT_PASS))
        {
            Serial.println(F("MQTT connected!"));

            // * Once connected, publish an announcement...
            char *message = new char[16 + strlen(HOSTNAME) + 1];
            strcpy(message, "p1 meter alive: ");
            strcat(message, HOSTNAME);
            mqtt_client.publish("hass/status", message);

            Serial.printf("MQTT root topic: %s\n", MQTT_ROOT_TOPIC);
        }
        else
        {
            Serial.print(F("MQTT Connection failed: rc="));
            Serial.println(mqtt_client.state());
            Serial.println(F(" Retrying in 5 seconds"));
            Serial.println("");

            // * Wait 5 seconds before retrying
            delay(5000);
        }
    }

    if (MQTT_RECONNECT_RETRIES >= MQTT_MAX_RECONNECT_TRIES)
    {
        Serial.printf("*** MQTT connection failed, giving up after %d tries ...\n", MQTT_RECONNECT_RETRIES);
        return false;
    }

    return true;
}

void send_metric(String name, long metric)
{
    // Serial.print(F("Sending metric to broker: "));
    // Serial.print(name);
    // Serial.print(F("="));
    // Serial.println(metric);

    ///////////////////////////////

    // char output[10];
    // ltoa(metric, output, sizeof(output));

    // String topic = String(MQTT_ROOT_TOPIC) + "/" + name;
    // send_mqtt_message(topic.c_str(), output);
}

void send_data_to_broker()
{
    send_metric("consumption_low_tarif", consumptionLowTarif);
    send_metric("consumption_high_tarif", consumptionHighTarif);
    send_metric("injected_low_tarif", injectedLowTarif);
    send_metric("injected_high_tarif", injectedHighTarif);
    send_metric("actual_consumption", consumptionPower);
    send_metric("actual_injection", injectionPower);

    send_metric("l1_instant_power_usage", L1ConsumptionPower);
    send_metric("l2_instant_power_usage", L2ConsumptionPower);
    send_metric("l3_instant_power_usage", L3ConsumptionPower);
    send_metric("l1_current", L1Current);
    send_metric("l2_current", L2Current);
    send_metric("l3_current", L3Current);
    send_metric("l1_voltage", L1Voltage);
    send_metric("l2_voltage", L2Voltage);
    send_metric("l3_voltage", L3Voltage);

    send_metric("gas_meter_m3", GAS_METER_M3);

    send_metric("actual_tarif_group", actualTarif);
    send_metric("short_power_outages", SHORT_POWER_OUTAGES);
    send_metric("long_power_outages", LONG_POWER_OUTAGES);
    send_metric("short_power_drops", SHORT_POWER_DROPS);
    send_metric("short_power_peaks", SHORT_POWER_PEAKS);

    send_metric("kwartiervermogen", kwartierVermogen);
    send_metric("kwartiervermogen_maand0", kwartierVermogenMaand);
}

// **********************************
// * P1                             *
// **********************************

unsigned int CRC16(unsigned int crc, unsigned char *buf, int len)
{
    for (int pos = 0; pos < len; pos++)
    {
        crc ^= (unsigned int)buf[pos]; // * XOR byte into least sig. byte of crc
                                       // * Loop over each bit
        for (int i = 8; i != 0; i--)
        {
            // * If the LSB is set
            if ((crc & 0x0001) != 0)
            {
                // * Shift right and XOR 0xA001
                crc >>= 1;
                crc ^= 0xA001;
            }
            // * Else LSB is not set
            else
                // * Just shift right
                crc >>= 1;
        }
    }
    return crc;
}

bool isNumber(char *res, int len)
{
    for (int i = 0; i < len; i++)
    {
        if (((res[i] < '0') || (res[i] > '9')) && (res[i] != '.' && res[i] != 0))
            return false;
    }
    return true;
}

int FindCharInArrayRev(char array[], char c, int len)
{
    for (int i = len - 1; i >= 0; i--)
    {
        if (array[i] == c)
            return i;
    }
    return -1;
}

long getValue(char *buffer, int maxlen, char startchar, char endchar)
{
    int s = FindCharInArrayRev(buffer, startchar, maxlen - 2);
    int l = FindCharInArrayRev(buffer, endchar, maxlen - 2) - s - 1;

    char res[16];
    memset(res, 0, sizeof(res));

    if (!((l + s + 1) > s))
    {
        Serial.println("malformed input");
        return 0;
    }

    if (strncpy(res, buffer + s + 1, l))
    {
        if (endchar == '*')
        {
            if (isNumber(res, l))
                // * Lazy convert float to long
                return (1000 * atof(res));
        }
        else if (endchar == ')')
        {
            if (isNumber(res, l))
                return atof(res);
        }
    }
    return 0;
}

bool decodeTelegram(int len)
{
    int startChar = FindCharInArrayRev(telegram, '/', len);
    int endChar = FindCharInArrayRev(telegram, '!', len);
    bool validCRCFound = false;

    for (int cnt = 0; cnt < len; cnt++)
    {
        Serial.print(telegram[cnt]);
    }

    if (startChar >= 0)
    {
        // * Start found. Reset CRC calculation
        currentCRC = CRC16(0x0000, (unsigned char *)telegram + startChar, len - startChar);
    }
    else if (endChar >= 0)
    {
        char messageCRC[5];
        memset(messageCRC, 0, sizeof(messageCRC));
        // * Add to crc calc
        currentCRC = CRC16(currentCRC, (unsigned char *)telegram + endChar, 1);

        strncpy(messageCRC, telegram + endChar + 1, 4);
        validCRCFound = (strtol(messageCRC, NULL, 16) == currentCRC);
        // Serial.print("CRC = ");
        // Serial.println(currentCRC, HEX);

        if (!validCRCFound)
        {    
            Serial.println(F("CRC Invalid!"));
            badCRC ++;
        }

        currentCRC = 0;
    }
    else
    {
        currentCRC = CRC16(currentCRC, (unsigned char *)telegram, len);
    }

    // L1, L2 en L3 power injectie ontbreken (1-0:22.7.0) enzovoort

    if (strncmp(telegram, "1", 1) == 0)
    {
        // 1-0:1.8.1(000992.992*kWh)
        // 1-0:1.8.1 = Elektriciteit verbruik laag tarief (DSMR v5.0)
        if (strncmp(telegram, "1-0:1.8.2", strlen("1-0:1.8.2")) == 0)
        {
            consumptionLowTarif = getValue(telegram, len, '(', '*');
        }
        else

            // 1-0:1.8.2(000560.157*kWh)
            // 1-0:1.8.2 = Elektriciteit verbruik hoog tarief (DSMR v5.0)
            if (strncmp(telegram, "1-0:1.8.1", strlen("1-0:1.8.1")) == 0)
            {
                consumptionHighTarif = getValue(telegram, len, '(', '*');
            }
            else

                // 1-0:2.8.1(000560.157*kWh)
                // 1-0:2.8.1 = Elektriciteit injectie laag tarief (DSMR v5.0)
                if (strncmp(telegram, "1-0:2.8.2", strlen("1-0:2.8.2")) == 0)
                {
                    injectedLowTarif = getValue(telegram, len, '(', '*');
                }
                else

                    // 1-0:2.8.2(000560.157*kWh)
                    // 1-0:2.8.2 = Elektriciteit injectie hoog tarief (DSMR v5.0)
                    if (strncmp(telegram, "1-0:2.8.1", strlen("1-0:2.8.1")) == 0)
                    {
                        injectedHighTarif = getValue(telegram, len, '(', '*');
                    }
                    else

                        // 1-0:1.7.0(00.424*kW) Actueel verbruik
                        // 1-0:1.7.x = Electricity consumption actual usage (DSMR v5.0)
                        if (strncmp(telegram, "1-0:1.7.0", strlen("1-0:1.7.0")) == 0)
                        {
                            consumptionPower = getValue(telegram, len, '(', '*');
                        }
                        else

                            // 1-0:2.7.0(00.000*kW) Actuele injectie in 1 Watt resolution
                            if (strncmp(telegram, "1-0:2.7.0", strlen("1-0:2.7.0")) == 0)
                            {
                                injectionPower = getValue(telegram, len, '(', '*');
                            }
                            else

                                // 1-0:21.7.0(00.378*kW)
                                // 1-0:21.7.0 = Instantaan vermogen Elektriciteit levering L1
                                if (strncmp(telegram, "1-0:21.7.0", strlen("1-0:21.7.0")) == 0)
                                {
                                    L1ConsumptionPower = getValue(telegram, len, '(', '*');
                                }
                                else

                                    // 1-0:41.7.0(00.378*kW)
                                    // 1-0:41.7.0 = Instantaan vermogen Elektriciteit levering L2
                                    if (strncmp(telegram, "1-0:41.7.0", strlen("1-0:41.7.0")) == 0)
                                    {
                                        L2ConsumptionPower = getValue(telegram, len, '(', '*');
                                    }
                                    else

                                        // 1-0:61.7.0(00.378*kW)
                                        // 1-0:61.7.0 = Instantaan vermogen Elektriciteit levering L3
                                        if (strncmp(telegram, "1-0:61.7.0", strlen("1-0:61.7.0")) == 0)
                                        {
                                            L3ConsumptionPower = getValue(telegram, len, '(', '*');
                                        }
                                        else

                                            // 1-0:31.7.0(002*A)
                                            // 1-0:31.7.0 = stroom Elektriciteit L1
                                            if (strncmp(telegram, "1-0:31.7.0", strlen("1-0:31.7.0")) == 0)
                                            {
                                                L1Current = getValue(telegram, len, '(', '*');
                                            }
                                            else
                                                // 1-0:51.7.0(002*A)
                                                // 1-0:51.7.0 = stroom Elektriciteit L2
                                                if (strncmp(telegram, "1-0:51.7.0", strlen("1-0:51.7.0")) == 0)
                                                {
                                                    L2Current = getValue(telegram, len, '(', '*');
                                                }
                                                else
                                                    // 1-0:71.7.0(002*A)
                                                    // 1-0:71.7.0 = stroom Elektriciteit L3
                                                    if (strncmp(telegram, "1-0:71.7.0", strlen("1-0:71.7.0")) == 0)
                                                    {
                                                        L3Current = getValue(telegram, len, '(', '*');
                                                    }
                                                    else

                                                        // 1-0:32.7.0(232.0*V)
                                                        // 1-0:32.7.0 = Voltage L1
                                                        if (strncmp(telegram, "1-0:32.7.0", strlen("1-0:32.7.0")) == 0)
                                                        {
                                                            L1Voltage = getValue(telegram, len, '(', '*');
                                                        }
                                                        else
                                                            // 1-0:52.7.0(232.0*V)
                                                            // 1-0:52.7.0 = Voltage L2
                                                            if (strncmp(telegram, "1-0:52.7.0", strlen("1-0:52.7.0")) == 0)
                                                            {
                                                                L2Voltage = getValue(telegram, len, '(', '*');
                                                            }
                                                            else
                                                                // 1-0:72.7.0(232.0*V)
                                                                // 1-0:72.7.0 = Voltage L3
                                                                if (strncmp(telegram, "1-0:72.7.0", strlen("1-0:72.7.0")) == 0)
                                                                {
                                                                    L3Voltage = getValue(telegram, len, '(', '*');
                                                                }
                                                                else

                                                                    // 1-0:32.32.0(00000)
                                                                    // 1-0:32.32.0 = Aantal korte spanningsdalingen Elektriciteit in fase 1
                                                                    if (strncmp(telegram, "1-0:32.32.0", strlen("1-0:32.32.0")) == 0)
                                                                    {
                                                                        SHORT_POWER_DROPS = getValue(telegram, len, '(', ')');
                                                                    }
                                                                    else

                                                                        // 1-0:32.36.0(00000)
                                                                        // 1-0:32.36.0 = Aantal korte spanningsstijgingen Elektriciteit in fase 1
                                                                        if (strncmp(telegram, "1-0:32.36.0", strlen("1-0:32.36.0")) == 0)
                                                                        {
                                                                            SHORT_POWER_PEAKS = getValue(telegram, len, '(', ')');
                                                                        }
    }
    else if (strncmp(telegram, "0", 1) == 0)
    {

        // 0-1:24.2.1(150531200000S)(00811.923*m3)
        // 0-1:24.2.1 = Gas (DSMR v5.0)
        if (strncmp(telegram, "0-1:24.2.3", strlen("0-1:24.2.3")) == 0)
        {
            GAS_METER_M3 = getValue(telegram, len, '(', '*');
        }
        else

            // 0-0:96.14.0(0001)
            // 0-0:96.14.0 = Actual Tarif
            if (strncmp(telegram, "0-0:96.14.0", strlen("0-0:96.14.0")) == 0)
            {
                actualTarif = getValue(telegram, len, '(', ')');
            }
            else

                // 0-0:96.7.21(00003)
                // 0-0:96.7.21 = Aantal onderbrekingen Elektriciteit
                if (strncmp(telegram, "0-0:96.7.21", strlen("0-0:96.7.21")) == 0)
                {
                    SHORT_POWER_OUTAGES = getValue(telegram, len, '(', ')');
                }
                else

                    // 0-0:96.7.9(00001)
                    // 0-0:96.7.9 = Aantal lange onderbrekingen Elektriciteit
                    if (strncmp(telegram, "0-0:96.7.9", strlen("0-0:96.7.9")) == 0)
                    {
                        LONG_POWER_OUTAGES = getValue(telegram, len, '(', ')');
                    }
                    // 0-0:1.0.0(220616152735S)
                    // TST
                    else if ((strncmp(telegram, "0-0:1.0.0", strlen("0-0:1.0.0")) == 0) && (len >= 24))
                    {
                        char temp[] = {telegram[10], telegram[11], 0};
                        testTimeInfo.tm_year = 100 + atoi(temp); // telt vanaf 1900
                        strncpy(temp, &telegram[12], 2);
                        testTimeInfo.tm_mon = atoi(temp);
                        strncpy(temp, &telegram[14], 2);
                        testTimeInfo.tm_mday = atoi(temp);
                        strncpy(temp, &telegram[16], 2);
                        testTimeInfo.tm_hour = atoi(temp);
                        strncpy(temp, &telegram[18], 2);
                        testTimeInfo.tm_min = atoi(temp);
                        strncpy(temp, &telegram[20], 2);
                        testTimeInfo.tm_sec = atoi(temp);
                        testTimeInfo.tm_isdst = (telegram[22] == 'S');
                        // https://randomnerdtutorials.com/esp32-date-time-ntp-client-server-arduino/
                        // Sunday, November 01 2019 09:03:45
                        // Serial.println(&testTimeInfo, "%A, %B %d %Y %H:%M:%S"); // struct tm testTimeInfo is Posix !!
                        // November 01 2019 09:03:45
                        // Serial.println(&testTimeInfo, "%B %d %Y %H:%M:%S");
                    }
    }

    return validCRCFound;
}

int getKwartier(tm timeInfo)
{
    int kwartier;
    kwartier = timeInfo.tm_hour * 4;
    kwartier = kwartier + ((timeInfo.tm_mday - 1) * 96);
    kwartier = kwartier + (timeInfo.tm_min / 15);
    // Serial.print("kwartier = ");
    // Serial.println(kwartier);
    return kwartier;
}

void processKwartier()
{
    int deltaKwartier = getKwartier(testTimeInfo) > getKwartier(testTimeInfoLast);

    // eerste poging om nieuwe maand te verwerken
    if (testTimeInfo.tm_mday < testTimeInfoLast.tm_mday)
    {
        kwartierVermogen = (consumptionHighTarif + consumptionLowTarif - tellerstand) * 4;
        if (kwartierVermogen > kwartierVermogenMaand)
        {
            kwartierVermogenMaand = kwartierVermogen;
        }
        kwartierVermogenVorigeMaand = kwartierVermogenMaand;
        kwartierVermogenMaand = 0;
        tellerstand = consumptionHighTarif + consumptionLowTarif; // tellerstand aan begin van het kwartier
        kwartierVermogen = (float)consumptionPower;
        memcpy(&testTimeInfoLast, &testTimeInfo, sizeof(testTimeInfo));
        Serial.println("nieuwe maand begonnen");
    }

    if (startup > 1 && deltaKwartier > 0)
    {
        memcpy(&testTimeInfoLast, &testTimeInfo, sizeof(testTimeInfo));
        // onderstaande is misschien niet goed ??
        tellerstand = consumptionHighTarif + consumptionLowTarif;

        startup = 1;
    }
    else if (deltaKwartier > 0)
    {
        if (startup)
        {
            startup = 0;
            tellerstand = consumptionHighTarif + consumptionLowTarif;
        }
        else

        { // mag wel niet uitgevoerd worden bij aanvang nieuwe maand
            kwartierVermogen = (consumptionHighTarif + consumptionLowTarif - tellerstand) * 4;
            tellerstand = consumptionHighTarif + consumptionLowTarif; // tellerstand aan begin van het kwartier
            if (kwartierVermogen > kwartierVermogenMaand)
            {
                kwartierVermogenMaand = kwartierVermogen;
                // kwartierVermogen = consumptionPower;
            }
        }

        memcpy(&testTimeInfoLast, &testTimeInfo, sizeof(testTimeInfo));
    }
    else
    {
        // nu lopend kwartiervermogen bepalen
        // onderstaande nog verrekenen met verstreken tijd
        float secondenverstreken;
        secondenverstreken = (testTimeInfo.tm_min % 15) * 60;
        secondenverstreken = secondenverstreken + testTimeInfo.tm_sec;
        if (!startup && secondenverstreken > 0)
        {
            kwartierVermogen = (consumptionHighTarif + consumptionLowTarif - tellerstand) / (secondenverstreken / 3600);
        } else
        {
            kwartierVermogen = (float)consumptionPower;
        }
        Serial.print("secondenverstreken = ");
        Serial.println(secondenverstreken);
    }

    Serial.print("kwartiervermogen vorige maand = ");
    Serial.println(kwartierVermogenVorigeMaand);
    Serial.print("kwartiervermogen maand = ");
    Serial.println(kwartierVermogenMaand);
    Serial.print("kwartiervermogen = ");
    Serial.println(kwartierVermogen);
    Serial.print("badCRC = ");
    Serial.println(badCRC);
}

bool processLine(int len)
{
    telegram[len] = '\n';
    telegram[len + 1] = 0;
    yield();

    return (decodeTelegram(len + 1));
}

void readP1Hardwareserial(void *parameter)
{
    while (true)
    {
        if (receivingSerial.available())
        {
            digitalWrite(LED_BUILTIN, LOW);
            memset(telegram, 0, sizeof(telegram));

            while (receivingSerial.available())
            {
#if defined(ESP8266)
                ESP.wdtDisable();
#endif
                int len = receivingSerial.readBytesUntil('\n', telegram, P1_MAXLINELENGTH);
#if defined(ESP8266)
                ESP.wdtEnable(1);
#endif

                if (processLine(len))
                {
                    processKwartier();
                    send_data_to_broker();
                    LAST_UPDATE_SENT = millis();
                    // Serial.println("send data to broker");
                    digitalWrite(LED_BUILTIN, HIGH);
                } 
            }
        }
    }
    vTaskDelete(readerTask);
}

// **********************************
// * EEPROM helpers                 *
// **********************************

String read_eeprom(int offset, int len)
{
    Serial.print(F("read_eeprom()"));

    String res = "";
    for (int i = 0; i < len; ++i)
    {
        res += char(EEPROM.read(i + offset));
    }
    return res;
}

void write_eeprom(int offset, int len, String value)
{
    Serial.println(F("write_eeprom()"));
    for (int i = 0; i < len; ++i)
    {
        if ((unsigned)i < value.length())
        {
            EEPROM.write(i + offset, value[i]);
        }
        else
        {
            EEPROM.write(i + offset, 0);
        }
    }
}

// ******************************************
// * Callback for saving WIFI config        *
// ******************************************

// * Callback notifying us of the need to save config
void save_wifi_config_callback()
{
    Serial.println(F("Should save config"));
    shouldSaveConfig = true;
}

// **********************************
// * Setup OTA                      *
// **********************************

void setup_ota()
{
    Serial.println(F("Arduino OTA activated."));

    // * Port defaults to 8266
    ArduinoOTA.setPort(8266);

    // * Set hostname for OTA
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.setPassword(OTA_PASSWORD);

    ArduinoOTA.onStart([]()
                       { Serial.println(F("Arduino OTA: Start")); });

    ArduinoOTA.onEnd([]()
                     { Serial.println(F("Arduino OTA: End (Running reboot)")); });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                          { Serial.printf("Arduino OTA Progress: %u%%\r", (progress / (total / 100))); });

    ArduinoOTA.onError([](ota_error_t error)
                       {
        Serial.printf("Arduino OTA Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR)
            Serial.println(F("Arduino OTA: Auth Failed"));
        else if (error == OTA_BEGIN_ERROR)
            Serial.println(F("Arduino OTA: Begin Failed"));
        else if (error == OTA_CONNECT_ERROR)
            Serial.println(F("Arduino OTA: Connect Failed"));
        else if (error == OTA_RECEIVE_ERROR)
            Serial.println(F("Arduino OTA: Receive Failed"));
        else if (error == OTA_END_ERROR)
            Serial.println(F("Arduino OTA: End Failed")); });

    ArduinoOTA.begin();
    Serial.println(F("Arduino OTA finished"));
}

// **********************************
// * Setup MDNS discovery service   *
// **********************************

void setup_mdns()
{
    Serial.println(F("Starting MDNS responder service"));

    bool mdns_result = MDNS.begin(HOSTNAME);
    if (mdns_result)
    {
        MDNS.addService("http", "tcp", 80);
    }
}

// **********************************
// * Setup Main                     *
// **********************************

void setup()
{
    memset(&testTimeInfo, 0, sizeof(testTimeInfo));
    memset(&testTimeInfoLast, 0, sizeof(testTimeInfoLast));
    // * Configure EEPROM
    EEPROM.begin(512);

#if defined(ESP8266)
    // Serial port setup for ESP8266
    // Setup a hw serial connection for communication with the P1 meter and logging (not yet using inversion)
    Serial.begin(BAUD_RATE, SERIAL_8N1, SERIAL_FULL);
    Serial.println("");
    Serial.println("Swapping UART0 RX to inverted");
    Serial.flush();
    // Invert the RX serialport by setting a register value, this way the TX might continue normally allowing the arduino serial monitor to read printlns
    USC0(UART0) = USC0(UART0) | BIT(UCRXI);
    receivingSerial = Serial;
#endif

#if defined(ESP32)
    Serial.begin(BAUD_RATE1);
    Serial2.begin(BAUD_RATE, SERIAL_8N1, RXD2, TXD2, true); // INVERT
    receivingSerial = Serial2;
#endif

    Serial.println("Serial port is ready to recieve.");

    // * Set led pin as output
    pinMode(LED_BUILTIN, OUTPUT);

    // * Start ticker with 0.5 because we start in AP mode and try to connect
    ticker.attach(0.6, tick);

    // * Get MQTT Server settings
    String settings_available = read_eeprom(134, 1);

    if (settings_available == "1")
    {
        read_eeprom(0, 64).toCharArray(MQTT_HOST, 64);   // * 0-63
        read_eeprom(64, 6).toCharArray(MQTT_PORT, 6);    // * 64-69
        read_eeprom(70, 32).toCharArray(MQTT_USER, 32);  // * 70-101
        read_eeprom(102, 32).toCharArray(MQTT_PASS, 32); // * 102-133
    }

    WiFiManagerParameter CUSTOM_MQTT_HOST("host", "MQTT hostname", MQTT_HOST, 64);
    WiFiManagerParameter CUSTOM_MQTT_PORT("port", "MQTT port", MQTT_PORT, 6);
    WiFiManagerParameter CUSTOM_MQTT_USER("user", "MQTT user", MQTT_USER, 32);
    WiFiManagerParameter CUSTOM_MQTT_PASS("pass", "MQTT pass", MQTT_PASS, 32);

    // * WiFiManager local initialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    // * Reset settings - uncomment for testing
    // wifiManager.resetSettings();

    // * Set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
    wifiManager.setAPCallback(configModeCallback);

    // * Set timeout
    wifiManager.setConfigPortalTimeout(WIFI_TIMEOUT);

    // * Set save config callback
    wifiManager.setSaveConfigCallback(save_wifi_config_callback);

    // * Add all your parameters here
    // wifiManager.addParameter(&CUSTOM_MQTT_HOST);
    // wifiManager.addParameter(&CUSTOM_MQTT_PORT);
    // wifiManager.addParameter(&CUSTOM_MQTT_USER);
    // wifiManager.addParameter(&CUSTOM_MQTT_PASS);

    // * Fetches SSID and pass and tries to connect
    // * Reset when no connection after 10 seconds
    // if (!wifiManager.autoConnect())
    //{
    //    Serial.println(F("Failed to connect to WIFI and hit timeout"));

    // * Reset and try again, or maybe put it to deep sleep
    // ESP.reset();
    //    delay(WIFI_TIMEOUT);
    //}

    // * Read updated parameters
    // strcpy(MQTT_HOST, CUSTOM_MQTT_HOST.getValue());
    // strcpy(MQTT_PORT, CUSTOM_MQTT_PORT.getValue());
    // strcpy(MQTT_USER, CUSTOM_MQTT_USER.getValue());
    // strcpy(MQTT_PASS, CUSTOM_MQTT_PASS.getValue());

    // * Save the custom parameters to FS
    if (shouldSaveConfig)
    {
        Serial.println(F("Saving WiFiManager config"));

        write_eeprom(0, 64, MQTT_HOST);   // * 0-63
        write_eeprom(64, 6, MQTT_PORT);   // * 64-69
        write_eeprom(70, 32, MQTT_USER);  // * 70-101
        write_eeprom(102, 32, MQTT_PASS); // * 102-133
        write_eeprom(134, 1, "1");        // * 134 --> always "1"
        EEPROM.commit();
    }

    // * If you get here you have connected to the WiFi
    // Serial.println(F("Connected to WIFI..."));

    // * Keep LED on
    ticker.detach();
    digitalWrite(LED_BUILTIN, LOW);

    // * Configure OTA
    // setup_ota();

    // * Startup MDNS Service
    // setup_mdns();

    // * Setup MQTT
    // Serial.printf("MQTT connecting to: %s:%s\n", MQTT_HOST, MQTT_PORT);

    // mqtt_client.setServer(MQTT_HOST, atoi(MQTT_PORT));

    xTaskCreate(readP1Hardwareserial, "readerTask", 2048, nullptr, 2, &readerTask);
}

// **********************************
// * Loop                           *
// **********************************

void loop()
{
    // ArduinoOTA.handle();
    long now = millis();

    // if (!mqtt_client.connected())
    //{
    //     if (now - LAST_RECONNECT_ATTEMPT > 5000)
    //     {
    //         LAST_RECONNECT_ATTEMPT = now;

    //        if (mqtt_reconnect())
    //        {
    //            LAST_RECONNECT_ATTEMPT = 0;
    //        }
    //    }
    //}
    // else
    //{
    //    mqtt_client.loop();
    //}

    if (now - LAST_UPDATE_SENT > UPDATE_INTERVAL)
    {
        //     digitalWrite(LED_BUILTIN, HIGH);
        //     readP1Hardwareserial();
        //     digitalWrite(LED_BUILTIN, LOW);
    }
    sleep(50);
}
