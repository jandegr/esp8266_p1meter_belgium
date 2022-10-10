#include <stdio.h>
#include <math.h>
#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"

#include <time.h>
#include "driver/uart.h"
#include "esp_log.h"

// * Include settings
#include "settings.h"

#define TAG "P1_APP"
#define RXD2 13 // AZDelivery
#define TXD2 14 // not used

struct tm testTimeInfo;
struct tm testTimeInfoLast;
float kwartierVermogen = 0;
float piekkwartierVermogenMaand = 0;
float piekkwartierVermogenVorigeMaand = 0;
float tellerstand = 0;
unsigned int currentCRC = 0;
int receivingSerial;
xTaskHandle readerTask;

extern "C"
{
    void app_main(void);
}

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

// returns true if line is a valid CRC
bool decodeLine(int len, char line[])
{
    int startChar = FindCharInArrayRev(line, '/', len);
    int endChar = FindCharInArrayRev(line, '!', len);
    bool validCRCFound = false;

    for (int cnt = 0; cnt < len; cnt++)
    {
        //    Serial.print(line[cnt]);
    }

    if (startChar >= 0)
    {
        // * Start found. Reset CRC calculation
        currentCRC = CRC16(0x0000, (unsigned char *)line + startChar, len - startChar);
    }
    else if (endChar >= 0)
    {
        char messageCRC[5];
        memset(messageCRC, 0, sizeof(messageCRC));
        // * Add to crc calc
        currentCRC = CRC16(currentCRC, (unsigned char *)line + endChar, 1);

        strncpy(messageCRC, line + endChar + 1, 4);
        validCRCFound = (strtol(messageCRC, NULL, 16) == currentCRC);
        // Serial.print("CRC = ");
        // Serial.println(currentCRC, HEX);

        if (!validCRCFound)
        {
            ESP_LOGE(TAG, "CRC Invalid!");
            //badCRC++;
        }

        currentCRC = 0;
    }
    else
    {
        currentCRC = CRC16(currentCRC, (unsigned char *)line, len);
    }

    // L1, L2 en L3 power injectie ontbreken (1-0:22.7.0) enzovoort

    if (strncmp(line, "1", 1) == 0)
    {
        // 1-0:1.8.1(000992.992*kWh)
        // 1-0:1.8.1 = Elektriciteit verbruik laag tarief (DSMR v5.0)
        if (strncmp(line, "1-0:1.8.2", strlen("1-0:1.8.2")) == 0)
        {
            consumptionLowTarif = getValue(line, len, '(', '*');
        }
        else

            // 1-0:1.8.2(000560.157*kWh)
            // 1-0:1.8.2 = Elektriciteit verbruik hoog tarief (DSMR v5.0)
            if (strncmp(line, "1-0:1.8.1", strlen("1-0:1.8.1")) == 0)
            {
                //printf("%s", line);
                consumptionHighTarif = getValue(line, len, '(', '*');
            }
            else

                // 1-0:2.8.1(000560.157*kWh)
                // 1-0:2.8.1 = Elektriciteit injectie laag tarief (DSMR v5.0)
                if (strncmp(line, "1-0:2.8.2", strlen("1-0:2.8.2")) == 0)
                {
                    injectedLowTarif = getValue(line, len, '(', '*');
                }
                else

                    // 1-0:2.8.2(000560.157*kWh)
                    // 1-0:2.8.2 = Elektriciteit injectie hoog tarief (DSMR v5.0)
                    if (strncmp(line, "1-0:2.8.1", strlen("1-0:2.8.1")) == 0)
                    {
                        injectedHighTarif = getValue(line, len, '(', '*');
                    }
                    else

                        // 1-0:1.7.0(00.424*kW) Actueel verbruik
                        // 1-0:1.7.x = Electricity consumption actual usage (DSMR v5.0)
                        if (strncmp(line, "1-0:1.7.0", strlen("1-0:1.7.0")) == 0)
                        {
                            consumptionPower = getValue(line, len, '(', '*');
                        }
                        else

                            // 1-0:2.7.0(00.000*kW) Actuele injectie in 1 Watt resolution
                            if (strncmp(line, "1-0:2.7.0", strlen("1-0:2.7.0")) == 0)
                            {
                                injectionPower = getValue(line, len, '(', '*');
                            }
                            else

                                // 1-0:21.7.0(00.378*kW)
                                // 1-0:21.7.0 = Instantaan vermogen Elektriciteit levering L1
                                if (strncmp(line, "1-0:21.7.0", strlen("1-0:21.7.0")) == 0)
                                {
                                    L1ConsumptionPower = getValue(line, len, '(', '*');
                                }
                                else

                                    // 1-0:41.7.0(00.378*kW)
                                    // 1-0:41.7.0 = Instantaan vermogen Elektriciteit levering L2
                                    if (strncmp(line, "1-0:41.7.0", strlen("1-0:41.7.0")) == 0)
                                    {
                                        L2ConsumptionPower = getValue(line, len, '(', '*');
                                    }
                                    else

                                        // 1-0:61.7.0(00.378*kW)
                                        // 1-0:61.7.0 = Instantaan vermogen Elektriciteit levering L3
                                        if (strncmp(line, "1-0:61.7.0", strlen("1-0:61.7.0")) == 0)
                                        {
                                            L3ConsumptionPower = getValue(line, len, '(', '*');
                                        }
                                        else

                                            // 1-0:31.7.0(002*A)
                                            // 1-0:31.7.0 = stroom Elektriciteit L1
                                            if (strncmp(line, "1-0:31.7.0", strlen("1-0:31.7.0")) == 0)
                                            {
                                                L1Current = getValue(line, len, '(', '*');
                                            }
                                            else
                                                // 1-0:51.7.0(002*A)
                                                // 1-0:51.7.0 = stroom Elektriciteit L2
                                                if (strncmp(line, "1-0:51.7.0", strlen("1-0:51.7.0")) == 0)
                                                {
                                                    L2Current = getValue(line, len, '(', '*');
                                                }
                                                else
                                                    // 1-0:71.7.0(002*A)
                                                    // 1-0:71.7.0 = stroom Elektriciteit L3
                                                    if (strncmp(line, "1-0:71.7.0", strlen("1-0:71.7.0")) == 0)
                                                    {
                                                        L3Current = getValue(line, len, '(', '*');
                                                    }
                                                    else

                                                        // 1-0:32.7.0(232.0*V)
                                                        // 1-0:32.7.0 = Voltage L1
                                                        if (strncmp(line, "1-0:32.7.0", strlen("1-0:32.7.0")) == 0)
                                                        {
                                                            L1Voltage = getValue(line, len, '(', '*');
                                                        }
                                                        else
                                                            // 1-0:52.7.0(232.0*V)
                                                            // 1-0:52.7.0 = Voltage L2
                                                            if (strncmp(line, "1-0:52.7.0", strlen("1-0:52.7.0")) == 0)
                                                            {
                                                                L2Voltage = getValue(line, len, '(', '*');
                                                            }
                                                            else
                                                                // 1-0:72.7.0(232.0*V)
                                                                // 1-0:72.7.0 = Voltage L3
                                                                if (strncmp(line, "1-0:72.7.0", strlen("1-0:72.7.0")) == 0)
                                                                {
                                                                    L3Voltage = getValue(line, len, '(', '*');
                                                                }
                                                                else

                                                                    // 1-0:32.32.0(00000)
                                                                    // 1-0:32.32.0 = Aantal korte spanningsdalingen Elektriciteit in fase 1
                                                                    if (strncmp(line, "1-0:32.32.0", strlen("1-0:32.32.0")) == 0)
                                                                    {
                                                                        SHORT_POWER_DROPS = getValue(line, len, '(', ')');
                                                                    }
                                                                    else

                                                                        // 1-0:32.36.0(00000)
                                                                        // 1-0:32.36.0 = Aantal korte spanningsstijgingen Elektriciteit in fase 1
                                                                        if (strncmp(line, "1-0:32.36.0", strlen("1-0:32.36.0")) == 0)
                                                                        {
                                                                            SHORT_POWER_PEAKS = getValue(line, len, '(', ')');
                                                                        }
    }
    else if (strncmp(line, "0", 1) == 0)
    {

        // 0-1:24.2.1(150531200000S)(00811.923*m3)
        // 0-1:24.2.1 = Gas (DSMR v5.0)
        if (strncmp(line, "0-1:24.2.3", strlen("0-1:24.2.3")) == 0)
        {
            GAS_METER_M3 = getValue(line, len, '(', '*');
        }
        else

            // 0-0:96.14.0(0001)
            // 0-0:96.14.0 = Actual Tarif
            if (strncmp(line, "0-0:96.14.0", strlen("0-0:96.14.0")) == 0)
            {
                actualTarif = getValue(line, len, '(', ')');
            }
            else

                // 0-0:96.7.21(00003)
                // 0-0:96.7.21 = Aantal onderbrekingen Elektriciteit
                if (strncmp(line, "0-0:96.7.21", strlen("0-0:96.7.21")) == 0)
                {
                    SHORT_POWER_OUTAGES = getValue(line, len, '(', ')');
                }
                else

                    // 0-0:96.7.9(00001)
                    // 0-0:96.7.9 = Aantal lange onderbrekingen Elektriciteit
                    if (strncmp(line, "0-0:96.7.9", strlen("0-0:96.7.9")) == 0)
                    {
                        LONG_POWER_OUTAGES = getValue(line, len, '(', ')');
                    }
                    // 0-0:1.0.0(220616152735S)
                    // TST
                    else if ((strncmp(line, "0-0:1.0.0", strlen("0-0:1.0.0")) == 0) && (len >= 24))
                    {
                        char buff[30];
                        char temp[] = {line[10], line[11], 0};
                        testTimeInfo.tm_year = 100 + atoi(temp); // telt vanaf 1900
                        strncpy(temp, &line[12], 2);
                        testTimeInfo.tm_mon = atoi(temp) - 1; // tm_mon = 0..11
                        strncpy(temp, &line[14], 2);
                        testTimeInfo.tm_mday = atoi(temp);
                        strncpy(temp, &line[16], 2);
                        testTimeInfo.tm_hour = atoi(temp);
                        strncpy(temp, &line[18], 2);
                        testTimeInfo.tm_min = atoi(temp);
                        strncpy(temp, &line[20], 2);
                        testTimeInfo.tm_sec = atoi(temp);
                        testTimeInfo.tm_isdst = (line[22] == 'S');

                        strftime(buff, 30, "%B %d %Y %H:%M:%S\n", &testTimeInfo);
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

void processKwartier(int* startup)
{
    bool deltaKwartier = getKwartier(testTimeInfo) > getKwartier(testTimeInfoLast);

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

    if (*startup > 1 && deltaKwartier)
    {
        memcpy(&testTimeInfoLast, &testTimeInfo, sizeof(testTimeInfo));
        // onderstaande is misschien niet goed ??
        tellerstand = consumptionHighTarif + consumptionLowTarif;

        *startup = 1;
    }
    else if (deltaKwartier)
    {
        if (*startup)
        {
            *startup = 0;
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
        if (!*startup && secondenverstreken > 0)
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
    printf("\n");
}

void readP1Hardwareserial(void *updater)
{
#define PACKET_READ_TICS (650 / portTICK_PERIOD_MS)
    // * Set to store received line
    //char telegram[P1_MAXLINELENGTH];
    char *telegramGlob = (char *)malloc(P1_MAXLINELENGTH);
    int startup = 2;
    printf("readertask started\n");
    while (true)
    {
        size_t avail = 0;
        uart_get_buffered_data_len(receivingSerial, &avail);

        if (avail > 0)
        {
            //printf("bytes avail = %d\n", avail);
            //  digitalWrite(LED_BUILTIN, LOW);
            memset(telegramGlob, 0, P1_MAXLINELENGTH);
            int len = 0;
            bool stop = false;
            while (avail > 0 && !stop && len < (P1_MAXLINELENGTH - 5)) //??
            {
                int gelezen = uart_read_bytes(receivingSerial, &telegramGlob[len], 1, 100 / portTICK_PERIOD_MS);
                // len = receivingSerial.readBytesUntil('\n', telegram, P1_MAXLINELENGTH);
                //printf("%d bytes gelezen = %s\n\n\n", gelezen, telegramGlob);

                if (telegramGlob[len] == '\n') // '\n'
                {
                    stop = true;
                }

                else
                {
                    uart_get_buffered_data_len(receivingSerial, &avail);
                    //printf("bytes avail2 = %d\n", avail);
                }
                len = len + gelezen;
            }
            //printf("%d bytes gelezen = %s\n", len, telegramGlob);
            telegramGlob[len] = 0; // moet na memset waarschijnlijk niet meer
            if (decodeLine(len, telegramGlob))
            {
                processKwartier(&startup);
                // send_data_to_broker();
            }
        }
        vTaskDelay(1);
    }
    vTaskDelete(readerTask);
}

void app_main()
{
    // const int uart_num = 2;
    const int READ_BUF_SIZE = 1024;
    memset(&testTimeInfo, 0, sizeof(testTimeInfo));
    memset(&testTimeInfoLast, 0, sizeof(testTimeInfoLast));

    // Serial.begin(BAUD_RATE1);
    // Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2, true); // INVERT
    receivingSerial = UART_NUM_2;

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
        //   .source_clk = UART_SCLK_DEFAULT, (is voor IDF 5.XX !!!!!)
        .source_clk = UART_SCLK_APB,
    };

    // Install UART driver (we don't need an event queue here)
    // In this example we don't even use a buffer for sending data.
    ESP_ERROR_CHECK(uart_driver_install(receivingSerial, READ_BUF_SIZE * 2, 0, 0, NULL, 0));

    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(receivingSerial, &uart_config));

    // Set UART pins
    ESP_ERROR_CHECK(uart_set_pin(receivingSerial, TXD2, RXD2, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // ESP_ERROR_CHECK(uart_set_mode(receivingSerial, UART_MODE_UART)); // regualr UART mode
    ESP_ERROR_CHECK(uart_set_line_inverse(receivingSerial, UART_SIGNAL_RXD_INV)); // invert receive
    // Set read timeout of UART TOUT feature
    // ESP_ERROR_CHECK(uart_set_rx_timeout(uart_num, ECHO_READ_TOUT));

    esp_log_level_set(TAG, ESP_LOG_INFO);

    printf("Serial port is ready to recieve.\n");

    xTaskCreate(readP1Hardwareserial, "readerTask", 2048, nullptr, 2, &readerTask);
}
