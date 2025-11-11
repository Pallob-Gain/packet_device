#include "BluetoothSerial.h"

#include "../../src/Packet_Device.h"
#include "../../src/Packet_Device.cpp"

#define MAX_COMMAND_LEN 128
typedef DevicePacket<char, MAX_COMMAND_LEN> PacketPortocol;

PacketPortocol *device_packet = NULL;  //Serial

TaskHandle_t SystemReceivingTask = NULL;

#define SENSOR_NUMBERS 5

float sensor_data[SENSOR_NUMBERS] = { 10.0, 20.0, 30.0, 40.0, 50.0 };

struct MassiveData {
  char name[100] = "Pallob Kumar Gain";
  float other_data[200];
};

MassiveData testData;

//data transfer defination between DSP and HOST
struct lockInInfo {
  float amplitude;
  float phase;
};

struct Bio{
  uint8_t age;
  uint8_t height;
};

void updateLockInfoX(lockInInfo *info) {
  device_packet->restOut("amplitude", info->amplitude);
}

void updateCounts(float *counts,uint16_t len){
  for(uint16_t i=0;i<len;i++){
    device_packet->restOut("count"+String(i),counts[i]);
  }
}

void updateBio(Bio* bio,uint8_t len){
  device_packet->restOut("len",len);
  uint8_t ages[len];
  for(uint16_t i=0;i<len;i++){
    ages[i]=bio[i].age;
  }
  device_packet->restArrayOut<uint8_t>("ages",ages,len);
}

void checkVersion() {
  //device_packet->restOutStr("test", "testing");
  //device_packet->restOutStr("test1", "testing");
  device_packet->restOutStr("version", "Packet Device");
}

//data thread
void systemReceivingProcess(void *parameter) {

  while (true) {
    //commands get
    device_packet->readSerialCommand();  //data receving without delay
    vTaskDelay(20);                      //20 ticks
  }

  //if the loop break mast the task need to be clear
  vTaskDelete(NULL);
}

void setup() {
  //pointer transfer

  Serial.begin(115200);  //start Seria

  device_packet = new PacketPortocol(&Serial, { '\r', '\n' });  //delimeter passed (it only nessary for the non raw transfer, raw byte are transmit with packet info)
  //device_packet->enableBulkRead(true); //if bulk read mode is enabled

  device_packet->onReceive("VNR", checkVersion);
  //set on receive function
  device_packet->onReceive<lockInInfo>("ULX", updateLockInfoX);  //raw mode
  device_packet->onReceive<float>("counts", updateCounts);  //raw mode
  device_packet->onReceive<Bio>("bio", updateBio);  //raw mode

  //data processing thread
  xTaskCreatePinnedToCore(
    systemReceivingProcess,  // Function to implement the task
    "recv",                  // Name of the task
    10000,                   // Stack size in bytes
    NULL,                    // Task input parameter
    2,                       // Priority of the task
    &SystemReceivingTask,    // Task handle.
    0                        // Core where the task should run or tskNO_AFFINITY
  );
}


void loop() {
  device_packet->processingQueueCommands();
  device_packet->restArrayOut<float>("env", sensor_data, SENSOR_NUMBERS);
  device_packet->restRawOut<MassiveData>("mdt", &testData);
  delay(500);
}