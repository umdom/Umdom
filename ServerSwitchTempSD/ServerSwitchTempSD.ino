#include <OneWire.h>
#include <SPI.h>
#include <Ethernet.h>
#include <SD.h>
#include <DallasTemperature.h>
#include <VirtualWire.h>
#include <EasyTransferVirtualWire.h>
#include <EEPROM.h>

#define ONE_WIRE_BUS 9 //pin for DS1820
#define TEMPERATURE_PRECISION 9
#define REQ_BUF_SZ 60 // size of buffer used to capture HTTP requests

byte mac[] = { 0x2E, 0x3D, 0xBE, 0xEF, 0xFE, 0xED }; // MAC address from Ethernet
IPAddress ip(192, 168, 0, 33); // IP address
EthernetServer server(80); // Port 80
File webFile; // Page file on the SD card
char HTTP_req[REQ_BUF_SZ] = {0}; // buffered HTTP request stored as null terminated string
char req_index = 0; // index into HTTP_req buffer
boolean RELE_state[2] = {0}; // stores the states of the LEDs
float temp1, temp2; // Temperature degree from DS1820
unsigned long lastUpdate = 0;
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature sensors(&oneWire);
// arrays to hold device addresses
DeviceAddress insideThermometer, outsideThermometer;

const int receive_pin = 22; // pin приемника от Евы
unsigned int unique_device_id = 0; //ID приемника
EasyTransferVirtualWire ET; //объект структуры
char buf[120];

const int rele1_pin = 5;
const int rele2_pin = 6;
const int rele3_pin = 3;


struct SEND_DATA_STRUCTURE{
  //наша структура данны. она должна быть определена одинаково на приёмнике и передатчике
  //кроме того, размер структуры не должен превышать 26 байт (ограничение VirtualWire)
  unsigned int device_id;
  unsigned int destination_id;  
  unsigned int packet_id;
  byte command;
  int data;
};

//переменная с данными нашей структуры
SEND_DATA_STRUCTURE mydata;


void setup()
{
    // disable Ethernet chip
    pinMode(10, OUTPUT);
    digitalWrite(10, HIGH);
    
    Serial.begin(9600); // for debugging
    Serial.println("Initializing SD card...");
    if (!SD.begin(4)) {
        Serial.println("ERROR - SD card initialization failed!");
        return; // init failed
    }
    Serial.println("SUCCESS - SD card initialized.");
    // check for index.htm file
    if (!SD.exists("index.htm")) {
        Serial.println("ERROR - Can't find index.htm file!");
        return; // can't find index file
    }
    Serial.println("SUCCESS - Found index.htm file.");
    

    pinMode(rele1_pin, OUTPUT); //Pin for Second relay
    pinMode(rele2_pin, OUTPUT); //Pin for First relay
    pinMode(rele3_pin, OUTPUT); //Pin for Third relay

    Ethernet.begin(mac, ip); // initialize Ethernet device
    server.begin(); // start to listen for clients
    sensors.begin(); //start to listen for temp sensors

  if (!sensors.getAddress(insideThermometer, 0)) Serial.println("Unable to find address for Device 0");
  if (!sensors.getAddress(outsideThermometer, 1)) Serial.println("Unable to find address for Device 1");

  // set the resolution to 9 bit
  sensors.setResolution(insideThermometer, TEMPERATURE_PRECISION);
  sensors.setResolution(outsideThermometer, TEMPERATURE_PRECISION);
 
  //код инициализации приемника
  ET.begin(details(mydata));
    // Initialise the IO and ISR
    vw_set_rx_pin(receive_pin);
    vw_setup(2000);      // Скорость приёма
    vw_rx_start();       // Запуск режима приёма
    
  // Device ID
  Serial.print("Getting Device ID... "); 
  unique_device_id=EEPROMReadInt(0);
  if (unique_device_id<10000 || unique_device_id>60000) {
   Serial.print("N/A, updating... "); 
   unique_device_id=random(10000, 60000);
   EEPROMWriteInt(0, unique_device_id);
  }
  Serial.println(unique_device_id);
}

void loop() {
    updateTemperature(); //update temp function

    if(ET.receiveData()) // получили пакет данных, обрабатываем
    {
      Serial.print(mydata.data); 
        if (mydata.data == 1){
            digitalWrite(rele1_pin, HIGH);
            RELE_state[1] = 1;
        }
    }

    EthernetClient client = server.available(); // try to get client

    if (client) { // got client?
        boolean currentLineIsBlank = true;
        while (client.connected()) {
            if (client.available()) {
                char c = client.read(); // leave last element in array as 0 to null terminate string (REQ_BUF_SZ - 1)
                if (req_index < (REQ_BUF_SZ - 1)) {
                    HTTP_req[req_index] = c;
                    req_index++;
                }
                if (c == '\n' && currentLineIsBlank) {
                    client.println("HTTP/1.1 200 OK");
                    if (StrContains(HTTP_req, "ajax_inputs")) {
                        client.println("Content-Type: text/xml");
                        client.println("Connection: keep-alive");
                        client.println();
                        SetLEDs();
                        XML_response(client);
                    }
                    else {
                        client.println("Content-Type: text/html");
                        client.println("Connection: keep-alive");
                        client.println();
                        webFile = SD.open("index.htm");
                        if (webFile) {
                            while(webFile.available()) {
                                client.write(webFile.read());
                            }
                            webFile.close();
                        }
                    }
                    req_index = 0;
                    StrClear(HTTP_req, REQ_BUF_SZ);
                    break;
                }
                if (c == '\n') {
                    currentLineIsBlank = true;
                }
                else if (c != '\r') {
                    currentLineIsBlank = false;
                }
            } // end if (client.available())
        } // end while (client.connected())
        delay(1); // give the web browser time to receive the data
        client.stop();
    } // end if (client)
}

void updateTemperature(){
  unsigned long time = millis();
  if((time - lastUpdate) > 10000 || lastUpdate == -100.0){
    lastUpdate = time;
    sensors.requestTemperatures();
    temp1 = sensors.getTempC(insideThermometer);
    temp2 = sensors.getTempC(outsideThermometer);
  } else {
    return;
  }
}

//converter stroki
String floatToString(float value, byte precision){
  int intVal = int(value);
  unsigned int frac;
  if(intVal >= 0){
    frac = (value - intVal) * precision;
  }
  else {
    frac = (intVal - value) * precision;
  }
  return String(intVal) + "." + String(frac);
}
 

// checks if received HTTP request is switching on/off LEDs
// also saves the state of the LEDs
void SetLEDs(void)
{
    // RELE 1 (pin 6)
    if (StrContains(HTTP_req, "RELE1=1")) {
        RELE_state[0] = 1;
        digitalWrite(rele2_pin, HIGH);
    }
    else if (StrContains(HTTP_req, "RELE1=0")) {
        RELE_state[0] = 0;
        digitalWrite(rele2_pin, LOW);
    }
 
    // RELE 2 (pin 5)
    if (StrContains(HTTP_req, "RELE2=1")) {
        RELE_state[1] = 1;
        digitalWrite(rele1_pin, HIGH);
    }
    else if (StrContains(HTTP_req, "RELE2=0")) {
        RELE_state[1] = 0;
        digitalWrite(rele1_pin, LOW);
    }
     // RELE 3 (pin 3)
    if (StrContains(HTTP_req, "RELE3=1")) {
        RELE_state[2] = 1;
        digitalWrite(rele3_pin, HIGH);
    }
    else if (StrContains(HTTP_req, "RELE3=0")) {
        RELE_state[3] = 0;
        digitalWrite(rele3_pin, LOW);
    }
  
}

void XML_response(EthernetClient cl)
{
    String temp_val1, temp_val2;
    
    if(temp1 != -100.0){
      temp_val1 = floatToString(temp1, 100);
    }
    if(temp2 != -100.0){
      temp_val2 = floatToString(temp2, 100);
    }
    cl.print("<?xml version = \"1.0\" ?>");
    cl.print("<inputs>");
    
        cl.print("<analog>");
        cl.print(temp_val1);
        cl.println("</analog>");
        cl.print("<analog>");
        cl.print(temp_val2);
        cl.println("</analog>");
    // RELE1
    cl.print("<RELE1>");
    if (RELE_state[0]) {
        cl.print("on");
    }
    else {
        cl.print("off");
    }
    cl.println("</RELE1>");
    
    // RELE2
    cl.print("<RELE2>");
    if (RELE_state[1]) {
        cl.print("on");
    }
    else {
        cl.print("off");
    }
    cl.println("</RELE2>");
    
    // RELE3
    cl.print("<RELE3>");
    if (RELE_state[2]) {
        cl.print("on");
    }
    else {
        cl.print("off");
    }
    cl.println("</RELE3>");
    
    cl.print("</inputs>");
}

// sets every element of str to 0 (clears array)
void StrClear(char *str, char length) {
    for (int i = 0; i < length; i++) {
        str[i] = 0;
    }
}

// returns 1 if string found
// returns 0 if string not found
char StrContains(char *str, char *sfind) {
    char found = 0;
    char index = 0;
    char len;
    
    len = strlen(str);
    if (strlen(sfind) > len) {
        return 0;
    }
    while (index < len) {
        if (str[index] == sfind[found]) {
            found++;
            if (strlen(sfind) == found) {
                return 1;
            }
        } else {
            found = 0;
        } 
        index++;
    }
    return 0;
}

//ниже пару функций для записи данных типа unsigned int в EEPROM
void EEPROMWriteInt(int p_address, unsigned int p_value){
      byte lowByte = ((p_value >> 0) & 0xFF);
      byte highByte = ((p_value >> 8) & 0xFF);
      EEPROM.write(p_address, lowByte);
      EEPROM.write(p_address + 1, highByte);
}

unsigned int EEPROMReadInt(int p_address){
      byte lowByte = EEPROM.read(p_address);
      byte highByte = EEPROM.read(p_address + 1);
      return ((lowByte << 0) & 0xFF) + ((highByte << 8) & 0xFF00);
}

