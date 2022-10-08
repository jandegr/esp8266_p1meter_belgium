//#include <FS.h>
#include <arduino.h>
//#include <EEPROM.h>
//#include <DNSServer.h>
//#include <Wifi.h>
HardwareSerial receivingSerial = Serial2;
#define RXD2 13 // AZDelivery
#define TXD2 14 // not used
//#include <Ticker.h>
//#include <WiFiManager.h>
#include <time.h>
#include "esp_log.h"

// * Include settings
#include "settings.h"

#define TAG "P1_APP"

struct tm testTimeInfo;
struct tm testTimeInfoLast;
float kwartierVermogen = 0;
float piekkwartierVermogenMaand = 0;
float piekkwartierVermogenVorigeMaand = 0;
float tellerstand = 0;
int startup = 2;
unsigned int currentCRC = 0;
// * Set to store received telegram
char telegram[P1_MAXLINELENGTH];
xTaskHandle readerTask;
int badCRC = 0;



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
        ESP_LOGE(TAG, "malformed input");
        return 0;
    }

    if (strncpy(res, buffer + s + 1, l))
    {
        if (endchar == '*')
        {
            if (isNumber(res, l))
            {
                // preserve round() below to get around some
                // float quircks, otherwise "000016.005" becomes
                // 16004 and "000016.025" becomes 16024
                return round((1000 * atof(res)));
            }
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
        //    Serial.print(telegram[cnt]);
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
            ESP_LOGE(TAG, "CRC Invalid!");
            badCRC++;
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
                Serial.println(telegram);
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
                        char buff[30]; 
                        char temp[] = {telegram[10], telegram[11], 0};
                        testTimeInfo.tm_year = 100 + atoi(temp); // telt vanaf 1900
                        strncpy(temp, &telegram[12], 2);
                        testTimeInfo.tm_mon = atoi(temp) - 1; // tm_mon = 0..11
                        strncpy(temp, &telegram[14], 2);
                        testTimeInfo.tm_mday = atoi(temp);
                        strncpy(temp, &telegram[16], 2);
                        testTimeInfo.tm_hour = atoi(temp);
                        strncpy(temp, &telegram[18], 2);
                        testTimeInfo.tm_min = atoi(temp);
                        strncpy(temp, &telegram[20], 2);
                        testTimeInfo.tm_sec = atoi(temp);
                        testTimeInfo.tm_isdst = (telegram[22] == 'S');
                        
                        
                        strftime(buff, 30, "%B %d %Y %H:%M:%S\n" , &testTimeInfo);
                        ESP_LOGE(TAG, "%s", buff);
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
    if (!timeInfo.tm_isdst)
    {
        kwartier = kwartier + 4;
        printf("wintertijd ");
    }
    else
    {
        printf("zomertijd ");
    }
    printf("kwartier = ");
    printf("%d\n", kwartier);
    return kwartier;
}

void processKwartier()
{
    int deltaKwartier = getKwartier(testTimeInfo) > getKwartier(testTimeInfoLast);

    // eerste poging om nieuwe maand te verwerken
    if (testTimeInfo.tm_mday < testTimeInfoLast.tm_mday)
    {
        kwartierVermogen = (consumptionHighTarif + consumptionLowTarif - tellerstand) * 4;
        if (kwartierVermogen > piekkwartierVermogenMaand)
        {
            piekkwartierVermogenMaand = kwartierVermogen;
        }
        piekkwartierVermogenVorigeMaand = piekkwartierVermogenMaand;
        piekkwartierVermogenMaand = 0;
        tellerstand = consumptionHighTarif + consumptionLowTarif; // tellerstand aan begin van het kwartier
        kwartierVermogen = (float)consumptionPower;
        memcpy(&testTimeInfoLast, &testTimeInfo, sizeof(testTimeInfo));
        printf("nieuwe maand begonnen\n");
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

        { // Indien er een nieuwe maand zou begonnen zijn is tellerstand hogerop al aangepast en
          // zal kwartiervermogen hier op 0 uitkomen en geen invloed hebben op de nieuwe maand
            kwartierVermogen = (consumptionHighTarif + consumptionLowTarif - tellerstand) * 4;
            tellerstand = consumptionHighTarif + consumptionLowTarif; // tellerstand aan begin van het kwartier
            if (kwartierVermogen > piekkwartierVermogenMaand)
            {
                piekkwartierVermogenMaand = kwartierVermogen;
            }
            kwartierVermogen = consumptionPower; // wegens nog geen tijd in het nieuwe kwartier
        }

        memcpy(&testTimeInfoLast, &testTimeInfo, sizeof(testTimeInfo));
    }
    else
    {
        // nu lopend kwartiervermogen bepalen
        // onderstaande nog verrekenen met verstreken tijd
        int secondenverstreken;
        secondenverstreken = (testTimeInfo.tm_min % 15) * 60;
        secondenverstreken = secondenverstreken + testTimeInfo.tm_sec;
        if (!startup && secondenverstreken > 0)
        {
            kwartierVermogen = (float)(consumptionHighTarif + consumptionLowTarif - tellerstand) / ((float)secondenverstreken / 3600);
        }
        else // is waarschijnlijk overbodig
        {
            kwartierVermogen = (float)consumptionPower;
        }
        printf("secondenverstreken = %d\n", secondenverstreken);
        
    }

    printf("vermogen = %ld\n", consumptionPower);
    printf("piekkwartiervermogen vorige maand = %f\n", piekkwartierVermogenVorigeMaand);
    printf("piekkwartiervermogen maand = %f\n", piekkwartierVermogenMaand);
    printf("kwartiervermogen = %f\n", kwartierVermogen);
    // Serial.print("badCRC = ");
    // Serial.println(badCRC);
    printf("\n");
}

bool processLine(int len)
{
    telegram[len] = '\n';
    telegram[len + 1] = 0;
    //yield();

    return (decodeTelegram(len + 1));
}

void readP1Hardwareserial(void *parameter)
{
    while (true)
    {
        if (receivingSerial.available())
        {
            //digitalWrite(LED_BUILTIN, LOW);
            memset(telegram, 0, sizeof(telegram));

            while (receivingSerial.available())
            {
                int len = receivingSerial.readBytesUntil('\n', telegram, P1_MAXLINELENGTH);

                if (processLine(len))
                {
                    processKwartier();
                    // send_data_to_broker();
                    
                    //digitalWrite(LED_BUILTIN, HIGH);
                }
            }
        }
        vTaskDelay(5);
    }
    vTaskDelete(readerTask);
}

void setup()
{
    memset(&testTimeInfo, 0, sizeof(testTimeInfo));
    memset(&testTimeInfoLast, 0, sizeof(testTimeInfoLast));

    //Serial.begin(BAUD_RATE1);
    Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2, true); // INVERT
    receivingSerial = Serial2;

    esp_log_level_set(TAG, ESP_LOG_INFO);

    printf("Serial port is ready to recieve.\n");

    xTaskCreate(readP1Hardwareserial, "readerTask", 2048, nullptr, 2, &readerTask);
}

void loop()
{
    sleep(1500);
}
