#include <WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Adafruit_AHTX0.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// Pin definitions
#define	SD_CARD_CS			5
#define	ONE_WIRE_BUS		33
#define	MOSFET_PUMP			32
#define	LIGHT_SENSOR		35
#define	SPIRULINA_SENSOR	34
#define SD_ERROR_LED		16

// delay for getting info
#define	DELAY	150000 // 2mn30 in milliseconds

// WiFi info
const char* ssid = "JRC-IspraNET-Wifi-Guest";

// Define NTP Client to get time
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

// DS18B20 sensor(s)
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// these are the preprogrammed adresses of the DS18B20 sensors
uint8_t DS18B20_panel[8] = { 0x28, 0xC2, 0x70, 0x63, 0x4F, 0x20, 0x01, 0x6B };
uint8_t DS18B20_water[8] = { 0x28, 0x5B, 0x7A, 0x70, 0x4F, 0x20, 0x01, 0x41 };

// + or - offsets to sensor temperature
int DS18B20_panel_error = 0;
int DS18B20_water_error = 0;

// AHT21 sensor
Adafruit_AHTX0 aht;

#pragma region SD CARD FUNCTIONS
void listDir(fs::FS &fs, const char * dirname, uint8_t levels)
{
	Serial.printf("Listing directory: %s\n", dirname);

	File root = fs.open(dirname);
		if(!root){
			Serial.println("Failed to open directory");
			return;
		}
		if(!root.isDirectory()){
			Serial.println("Not a directory");
			return;
	}

	File file = root.openNextFile();
	while(file){
		if(file.isDirectory()){
		Serial.print("  DIR : ");
		Serial.println(file.name());
		if(levels){
			listDir(fs, file.name(), levels -1);
		}
		} else {
		Serial.print("  FILE: ");
		Serial.print(file.name());
		Serial.print("  SIZE: ");
		Serial.println(file.size());
		}
		file = root.openNextFile();
	}
}

void createDir(fs::FS &fs, const char * path)
{
	Serial.printf("Creating Dir: %s\n", path);
	if(fs.mkdir(path)){
		Serial.println("Dir created");
	} else {
		Serial.println("mkdir failed");
	}
}

void removeDir(fs::FS &fs, const char * path)
{
	Serial.printf("Removing Dir: %s\n", path);
	if(fs.rmdir(path)){
		Serial.println("Dir removed");
	} else {
		Serial.println("rmdir failed");
	}
}

void readFile(fs::FS &fs, const char * path)
{
	Serial.printf("Reading file: %s\n", path);

	File file = fs.open(path);
	if(!file){
		Serial.println("Failed to open file for reading");
		return;
	}

	Serial.print("Read from file: ");
	while(file.available()){
		Serial.write(file.read());
	}
	file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message)
{
	Serial.printf("Writing file: %s\n", path);

	File file = fs.open(path, FILE_WRITE);
	if(!file){
		Serial.println("Failed to open file for writing");
		return;
	}
	if(file.print(message)){
		Serial.println("File written");
	} else {
		Serial.println("Write failed");
	}
	file.close();
}

bool appendFile(fs::FS &fs, const char * path, const char * message)
{
	Serial.printf("Appending to file: %s\n", path);

	File file = fs.open(path, FILE_APPEND);
	if(!file){
		Serial.println("Failed to open file for appending");
		return false;
	}
	if(file.print(message)){
		Serial.println("Message appended");
		file.close();
		return true;
	} else {
		Serial.println("Append failed");
		file.close();
		return false;
	}
}

bool testFile(fs::FS &fs, const char * path)
{
	File file = fs.open(path);
	if(!file){
		Serial.println("Failed to open file for reading");
		return false;
	}
	file.close();
	return true;
}

void renameFile(fs::FS &fs, const char * path1, const char * path2)
{
	Serial.printf("Renaming file %s to %s\n", path1, path2);
	if (fs.rename(path1, path2)) {
		Serial.println("File renamed");
	} else {
		Serial.println("Rename failed");
	}
}

void deleteFile(fs::FS &fs, const char * path)
{
	Serial.printf("Deleting file: %s\n", path);
	if(fs.remove(path)){
		Serial.println("File deleted");
	} else {
		Serial.println("Delete failed");
	}
}
#pragma endregion

void		SetupWiFi_Time()
{
	Serial.print("Connecting to ");
	Serial.println(ssid);
	WiFi.begin(ssid);
	while (WiFi.status() != WL_CONNECTED) {
		delay(500);
		Serial.print(".");
	}
	// Print local IP address and start web server
	Serial.println("");
	Serial.println("WiFi connected.");
	Serial.println("IP address: ");
	Serial.println(WiFi.localIP());

	// Initialize a NTPClient to get time
	timeClient.begin();
	timeClient.setTimeOffset(3600); // GMT + 1
}

void		setup()
{
	// Initialize Serial Monitor
	Serial.begin(9800);

	// Setup WiFi
	SetupWiFi_Time();

	// start OneWire (DS18B20) Devices
	sensors.begin();

	if (!aht.begin()) 
	{
		Serial.println("Could not find AHT? Check wiring");
	}

	// Pin Modes
	pinMode(MOSFET_PUMP, OUTPUT);
	pinMode(SPIRULINA_SENSOR, INPUT);
	pinMode(LIGHT_SENSOR, INPUT);
	pinMode(SD_ERROR_LED, OUTPUT);

	// SD setup
	while (!SD.begin(SD_CARD_CS))
	{
		Serial.println("Card Mount Failed");
		digitalWrite(SD_ERROR_LED, HIGH);
		delay(800);
		digitalWrite(SD_ERROR_LED, LOW);
		delay(800);
	}
	appendFile(SD, "/log.csv", "- boot -,,,,,,\n");
}

uint16_t	GetData(uint8_t _pin, uint32_t _iterations, uint32_t _delay)
{
	uint64_t data = 0;

	for (size_t i = 0; i < _iterations; i++)
	{
		data += analogRead(_pin);
		delay(_delay);
	}
	data = data / _iterations;
	return ((uint16_t)data);
}

// this function checks if the SD card is working
// if not it will blink the LED an not continue the program
// until it the SD reading is fixed
void		checkSD_Blocking()
{
	while (testFile(SD, "/log.csv") == false)
	{
		digitalWrite(SD_ERROR_LED, HIGH);
		delay(800);
		digitalWrite(SD_ERROR_LED, LOW);
		delay(800);
	}
}

void		loop()
{
	// check if SD is working
	checkSD_Blocking();

	// --- SENSOR STUFF ---
	// DS18B20
	sensors.requestTemperatures();
	float temperature_panel = sensors.getTempC(DS18B20_panel) + DS18B20_panel_error;
	Serial.printf("panel temperature: %.1f\n", temperature_panel);
	float temperature_water = sensors.getTempC(DS18B20_water) + DS18B20_water_error;
	Serial.printf("water temperature: %.1f\n", temperature_water);

	// AHT21
	sensors_event_t humidity, temp;
	aht.getEvent(&humidity, &temp);// populate temp and humidity objects with fresh data
	Serial.print("Atmospheric Temperature: "); Serial.print(temp.temperature); Serial.println(" degrees C");

	// Light intensity sensor (sun)
	uint16_t light_val = GetData(LIGHT_SENSOR, 200, 1); // 200 millis read
	Serial.printf("light intensity: %d\n", light_val);

	// spirulina density sensor
	uint16_t spirulina_density = GetData(SPIRULINA_SENSOR, 500, 2); // 1 second read
	Serial.printf("Spirulina density: %d\n", spirulina_density);

	// --- TIME STUFF ---
	timeClient.update();

	int day = timeClient.getDay();
	String formattedTime = timeClient.getFormattedTime();

	// --- LOGIC STUFF ---
	/*if (temperature_water > 37)
		digitalWrite(MOSFET_PUMP, LOW);
	else
		digitalWrite(MOSFET_PUMP, HIGH);*/

	// --- logging data into SD card file ---
	String data =
		String(day) + ", " + String(formattedTime) + ", " + // date time
		light_val + "," + String(temperature_panel) + ", " + String(temperature_water) + ", " + 
		String(temp.temperature) + ", " + String(spirulina_density) + "\n";
	//Serial.println(data);

	while (appendFile(SD, "/log.csv", data.c_str()) == false)
	{
		digitalWrite(SD_ERROR_LED, HIGH);
		delay(800);
		digitalWrite(SD_ERROR_LED, LOW);
		delay(800);
	}

	Serial.println();

	// --- loop delay ---
	int delay_tm = 0;
	while (delay_tm < DELAY)
	{
		delay(5000);
		checkSD_Blocking();
		delay_tm += 5000;
	}
}


/*
// to know adress of device on OneWire Bus
void printAddress(DeviceAddress deviceAddress)
{ 
	for (uint8_t i = 0; i < 8; i++)
	{
		Serial.print("0x");
		if (deviceAddress[i] < 0x10) Serial.print("0");
		Serial.print(deviceAddress[i], HEX);
		if (i < 7) Serial.print(", ");
	}
	Serial.println("");
}
*/