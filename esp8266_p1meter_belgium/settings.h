// **********************************
// * Settings                       *
// **********************************

// * Max telegram length
#define P1_MAXLINELENGTH 1050

// * The hostname of our little creature
#define HOSTNAME "p1meter"

// * The password used for OTA
#define OTA_PASSWORD "admin"

// * Wifi timeout in milliseconds
#define WIFI_TIMEOUT 30000

// * MQTT network settings
#define MQTT_MAX_RECONNECT_TRIES 10

// * MQTT root topic
#define MQTT_ROOT_TOPIC "sensors/power/p1meter"


// * Set to store the data values read
long consumptionLowTarif;
long consumptionHighTarif;

long injectedLowTarif;
long injectedHighTarif;

long consumptionPower;
long injectionPower;
long GAS_METER_M3;

long L1ConsumptionPower;
long L2ConsumptionPower;
long L3ConsumptionPower;
long L1Current;
long L2Current;
long L3Current;
long L1Voltage;
long L2Voltage;
long L3Voltage;

// Set to store data counters read
long actualTarif;
long SHORT_POWER_OUTAGES;
long LONG_POWER_OUTAGES;
long SHORT_POWER_DROPS;
long SHORT_POWER_PEAKS;


