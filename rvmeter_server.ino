#include "esp_adc_cal.h"
#include "QuickMedianLib.h"
#include "esp_adc_cal.h"
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Preferences.h>

Preferences preferences;

// TEMP - Display for debugging
#include "Free_Fonts.h"
#include "SPI.h"
#include "TFT_eSPI.h"

// Use hardware SPI
TFT_eSPI tft = TFT_eSPI();

//END TEMP

#define SECONDS_BETWEEN_SAMPLES     30   //TODO: Set this to 120 before production
#define NUM_SAMPLES                 10
#define TOUCHPIN                    T5
#define VOLTPIN                     32
#define VOLTCHAN                    ADC1_CHANNEL_4

#define SERVER_NAME                 "RV Server"
#define SERVICE_UUID                "68f9860f-4946-4031-8107-9327cd9f92ca"
#define TOUCH_CHARACTERISTIC_UUID   "bcdd0001-b67f-46c7-b2b8-e8a385ac70fc"
#define VOLTAGE_CHARACTERISTIC_UUID "bcdd0002-b67f-46c7-b2b8-e8a385ac70fc"
#define TOUCH_CALIBRATION_CHARACTERISTIC_UUID "bcdd0011-b67f-46c7-b2b8-e8a385ac70fc"

#define TOUCH_CALIBRATION_PREF_KEY "calibration"
#define RW_MODE                     false

static esp_adc_cal_characteristics_t adc1_value;

int voltRawValues[NUM_SAMPLES];
int touchValues[NUM_SAMPLES];
BLECharacteristic *bleTouchValue;
BLECharacteristic *bleVoltageValue;
BLECharacteristic *bleTouchCalibration;

std::string calibrationData = "0:0"; // format: value:percentage,value:percentage,...

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      pServer->startAdvertising(); // restart advertising
    };

    void onDisconnect(BLEServer* pServer) {
      pServer->startAdvertising(); // restart advertising
    }
};

class CalibrationWriteCallback : public BLECharacteristicCallbacks {

  void onWrite(BLECharacteristic *pCharacteristic) {
    calibrationData = pCharacteristic->getValue();
    Serial.print("Received calibration data:'");
    Serial.print(calibrationData.c_str());
    Serial.println("'");
    if (!validCalibrationData(calibrationData)) {
      Serial.println("Calibration invalid - ignoring");
    } else {
      preferences.begin("rvsetting", RW_MODE);
      preferences.putString(TOUCH_CALIBRATION_PREF_KEY, calibrationData.c_str());
      preferences.end();
    }
  }

  bool validCalibrationData(std::string data) {
    return data.length() > 10;
    //TODO: Implement a more robust validation
  }
    
};

void setup() {
  Serial.begin(115200);
  delay(1000); // give me time to bring up serial monitor
  Serial.println("Starting RV Meter");
  pinMode(VOLTPIN, INPUT);
  if (adc1_config_width(ADC_WIDTH_BIT_12) != ESP_OK) {
    Serial.println("Error configuring ADC1");
  }
  if (adc1_config_channel_atten(VOLTCHAN, ADC_ATTEN_DB_11) != ESP_OK) {
    Serial.println("Error configuring channel atten.");
  }
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 0, &adc1_value);
  BLEDevice::init(SERVER_NAME);
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  pServer->setCallbacks(new MyServerCallbacks());
  bleTouchValue = pService->createCharacteristic(
                                         TOUCH_CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ
                                       );
  bleVoltageValue = pService->createCharacteristic(
                                         VOLTAGE_CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ
                                       );
  bleTouchCalibration = pService->createCharacteristic(
                                         TOUCH_CALIBRATION_CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ  |
                                          BLECharacteristic::PROPERTY_WRITE
                                       );
  pService->start();
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);  // functions that help with iPhone connections issue
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
  preferences.begin("rvsetting", RW_MODE);
  if (preferences.isKey(TOUCH_CALIBRATION_PREF_KEY)) {
    String prefsValue = preferences.getString(TOUCH_CALIBRATION_PREF_KEY);
    calibrationData = std::string(prefsValue.c_str());
    Serial.print("Prefs value for touch config read: '");
    Serial.print(prefsValue);
    Serial.println("'");
  } else {
    // Need to initialize
    preferences.putString(TOUCH_CALIBRATION_PREF_KEY, "TEST");
    Serial.println("No prefs calibration data - using default");
  }
  preferences.end();
  bleTouchCalibration->setValue(calibrationData);
  bleTouchCalibration->setCallbacks(new CalibrationWriteCallback());
  // 
  Serial.println("RV Service Started");

  //BEGIN DEBUG
    tft.begin();

  tft.setRotation(1);
  tft.setTextDatum(MC_DATUM);

  // Set text colour to orange with black background
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  tft.fillScreen(TFT_BLACK);            // Clear screen
  tft.setFreeFont(FF18);                 // Select the font
  String TEXT = "Started...";
  tft.drawString(TEXT, 160, 120, GFXFF);
  //END DEBUG
}

int count = 0;
void loop() {
  for (int i = 0; i < NUM_SAMPLES; i++) {
    voltRawValues[i] = adc1_get_raw(VOLTCHAN);
    touchValues[i] = touchRead(TOUCHPIN);
    delay(100);
  }
  int voltRaw = QuickMedian<int>::GetMedian(voltRawValues, NUM_SAMPLES);
  float milliVoltSample = esp_adc_cal_raw_to_voltage(voltRaw, &adc1_value) * 5.7;
  Serial.print("Voltage: ");
  Serial.println(milliVoltSample / 1000);
  Serial.print("Raw voltage: ");
  Serial.println(voltRaw);
  int milliVoltSampleInt = int(trunc(milliVoltSample));
  bleVoltageValue->setValue(milliVoltSampleInt); // send in millivolts
  int touchSample = QuickMedian<int>::GetMedian(touchValues, NUM_SAMPLES);
  Serial.print("Touch: ");
  Serial.println(touchSample);
  bleTouchValue->setValue(touchSample);

  // BEGIN DEBUG
  String TEXT = "Loop number " + String(count);
  count++;
  tft.drawString(TEXT, 10, 120, GFXFF);
  // END DEBUG

  delay(SECONDS_BETWEEN_SAMPLES * 1000);
}
