#if defined(ARDUINO) && ARDUINO >= 100
  #include "Arduino.h"
  #include "SoftwareSerial.h"
  SoftwareSerial port(12,13);
#else // Arduino 0022 - use modified NewSoftSerial
  #include "WProgram.h"
  #include "NewSoftSerial.h"
  NewSoftSerial port(12,13);
#endif

#include <VirtualWire.h>
#include <EasyTransferVirtualWire.h>
#include <EEPROM.h>
#include <EasyVR.h>
EasyVR easyvr(port);

//Groups and Commands
enum Groups
{
  GROUP_0 = 0,
  GROUP_1 = 1,
};


enum Group0 
{
  G0_EVA = 0,
};

enum Group1 
{
  G1_BEDROOM = 0,
};

EasyVRBridge bridge;
int8_t group, idx;

const int led_pin = 13;
const int transmit_pin = 7;
unsigned int unique_device_id = 0;
unsigned int count = 0;

EasyTransferVirtualWire ET;


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

//ниже пару функций для записи данных типа unsigned int в EEPROM
void EEPROMWriteInt(int p_address, unsigned int p_value){
      byte lowByte = ((p_value >> 0) & 0xFF);
      byte highByte = ((p_value >> 8) & 0xFF);

      EEPROM.write(p_address, lowByte);
      EEPROM.write(p_address + 1, highByte);
}

unsigned int EEPROMReadInt(int p_address) {
      byte lowByte = EEPROM.read(p_address);
      byte highByte = EEPROM.read(p_address + 1);
      return ((lowByte << 0) & 0xFF) + ((highByte << 8) & 0xFF00);
}

void setup() {
   pinMode(10, OUTPUT);
  // bridge mode?
  if (bridge.check())
  {
    cli();
    bridge.loop(0, 1, 12, 13);
  }
  // run normally
  Serial.begin(9600);
  port.begin(9600);

  if (!easyvr.detect())
  {
    Serial.println("EasyVR not detected!");
    for (;;);
  }

  easyvr.setPinOutput(EasyVR::IO1, LOW);
  Serial.println("EasyVR detected!");
  easyvr.setTimeout(5);
  easyvr.setLanguage(0);

  group = EasyVR::TRIGGER; //<-- start group (customize)
  
  //Код инициализации передатчика
   ET.begin(details(mydata));
  vw_set_tx_pin(transmit_pin); //установка пина, к которому подключен data-вход передатчика
  vw_setup(2000);        //скорость передачи
  Serial.begin(9600);
  randomSeed(analogRead(0));


  // Читаем/записываем Device ID
  Serial.print("Getting Device ID... "); 
  unique_device_id=EEPROMReadInt(0);
  if (unique_device_id<10000 || unique_device_id>60000) {
   Serial.print("N/A, updating... "); 
   unique_device_id=random(10000, 60000);
   EEPROMWriteInt(0, unique_device_id);
  }
  Serial.println(unique_device_id);
}

void action();

void loop()
{
  easyvr.setPinOutput(EasyVR::IO1, HIGH); // LED on (listening)

  Serial.print("Say a command in Group ");
  Serial.println(group);
  easyvr.recognizeCommand(group);

  do
  {
    // can do some processing while waiting for a spoken command
  }
  while (!easyvr.hasFinished());
  
  easyvr.setPinOutput(EasyVR::IO1, LOW); // LED off

  idx = easyvr.getWord();
  if (idx >= 0)
  {
    // built-in trigger (ROBOT)
    // group = GROUP_X; <-- jump to another group X
    return;
  }
  idx = easyvr.getCommand();
  if (idx >= 0)
  {
    // print debug message
    uint8_t train = 0;
    char name[32];
    Serial.print("Command: ");
    Serial.print(idx);
    if (easyvr.dumpCommand(group, idx, name, train))
    {
      Serial.print(" = ");
      Serial.println(name);
    }
    else
      Serial.println();
    easyvr.playSound(0, EasyVR::VOL_FULL);
    // perform some action
    action();
  }
  else // errors or timeout
  {
    if (easyvr.isTimeout())
      Serial.println("Timed out, try again...");
    int16_t err = easyvr.getError();
    if (err >= 0)
    {
      Serial.print("Error ");
      Serial.println(err, HEX);
    }
  }
}

void action()
{
    switch (group)
    {
    case GROUP_0:
      switch (idx)
      {
      case G0_EVA:
       group = GROUP_1;
       easyvr.playSound(2, EasyVR::VOL_FULL);
        break;
      }
      break;
    case GROUP_1:
      switch (idx)
      {
      case G1_BEDROOM:
        mydata.device_id = unique_device_id;
        mydata.destination_id = 0;
        mydata.packet_id = random(65535);
        mydata.command = 1;
        mydata.data = 1;

        digitalWrite(led_pin, HIGH); // включаем светодиод для отображения процесса передачи
        Serial.print(" data: ");

        Serial.print(mydata.data);
        ET.sendData(); // отправка данных
      
        easyvr.playSound(1, EasyVR::VOL_FULL);
        digitalWrite(10, HIGH);
        group = GROUP_0;
        break;
      }
      break;
    }
}
