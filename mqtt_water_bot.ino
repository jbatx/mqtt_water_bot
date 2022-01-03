#include <WiFi.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <HTTPClient.h>
#include <AWS_IOT.h>
 
#define SCREEN_WIDTH 128    // OLED display width, in pixels
#define SCREEN_HEIGHT 64    // OLED display height, in pixels
#define OLED_RESET -1       // Reset pin # (or -1 if sharing Arduino reset pin)
 
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
 
const char *ssid = "smartypants";     // replace with your wifi ssid and wpa2 key
const char *password = "123123123";

// AWS IoT Core params
#define CLIENT_ID "water_bot" // thing unique ID, this id should be unique among all things associated with your AWS account.
#define MQTT_TOPIC "$aws/things/water_bot/shadow/update" //topic for the MQTT data
#define AWS_HOST "123123123-ats.iot.us-east-2.amazonaws.com" // your host for uploading data to AWS,

AWS_IOT aws;

#define LED_BUILTIN 16
#define SENSOR  15 //2
 
long currentMillis = 0;
long previousMillis = 0;
long previousHeartBeat = 0;
int interval = 1000;
boolean ledState = LOW;
float calibrationFactor = 5.5;
volatile byte pulseCount;
byte pulse1Sec = 0;
float flowRate;
unsigned long flowMilliLitres;
unsigned int totalMilliLitres;
float flowLitres;
float totalLitres;
const int relay = 13;
bool waterFlowing = false;

void IRAM_ATTR pulseCounter()
{
  pulseCount++;
}
  
void setup()
{
  Serial.begin(115200);

  // Connect to wifi
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  // Connect to AWS MQTT
  if(aws.connect(AWS_HOST, CLIENT_ID) == 0){ // connects to host and returns 0 upon success
    Serial.println("  Connected to AWS\n  Done.");
  }else {
    Serial.println("  Connection failed!\n make sure your subscription to MQTT in the test page");
  }

  // Set up the display
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); //initialize with the I2C addr 0x3C (128x64)
  display.display();
  delay(10);
 
  // Set up the flow sensor
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(SENSOR, INPUT_PULLUP);
 
  pulseCount = 0;
  flowRate = 0.0;
  flowMilliLitres = 0;
  totalMilliLitres = 0;
  previousMillis = 0;
  previousHeartBeat = 0;
 
  attachInterrupt(digitalPinToInterrupt(SENSOR), pulseCounter, FALLING);

  // Set up the relay
  pinMode(relay, OUTPUT);
  setRelayClosed(false);
}
 
void loop()
{
  
  if(millis() > 7200000){
    publishMqtt("{\"device_id\": \"water_bot\", \"restart\":" + (String)millis() + "}\n");
    delay(3000);
    ESP.restart();    
  }


  currentMillis = millis();
  
  if(currentMillis - previousHeartBeat > 30000){
      Serial.println("heartbeat");
      publishMqtt("{\"device_id\": \"water_bot\", \"heartbeat\":" + (String)millis() + "}\n");
      previousHeartBeat = millis();
  }


  if (currentMillis - previousMillis > interval) 
  {
    
    Serial.println("millis " + (String)millis());

    pulse1Sec = pulseCount;
    pulseCount = 0;
 
    // scale the output
    flowRate = ((1000.0 / (millis() - previousMillis)) * pulse1Sec) / calibrationFactor;
    previousMillis = millis();
 
    // convert to ml
    flowMilliLitres = (flowRate / 60) * 1000;
    flowLitres = (flowRate / 60);
 
    // Add to the total
    totalMilliLitres += flowMilliLitres;
    totalLitres += flowLitres;
    
    Serial.print("Flow rate: ");
    Serial.print(float(flowRate));  
    Serial.print("L/min");
    Serial.print("\t");
 
    display.clearDisplay();
    
    display.setCursor(10,0);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.print("MQTT Water Bot");
    
    display.setCursor(0,20);
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.print("R:");
    display.print(float(flowRate));
    display.setCursor(100,28);
    display.setTextSize(1);
    display.print("L/M");

    Serial.print("Output Liquid Quantity: ");
    Serial.print(totalMilliLitres);
    Serial.print("mL / ");
    Serial.print(totalLitres);
    Serial.println("L");
 
    display.setCursor(0,45);
    display.setTextSize(2);
    display.setTextColor(WHITE);
    display.print("V:");
    display.print(totalLitres);
    display.setCursor(100,53);
    display.setTextSize(1);
    display.print("L");
    display.display();

    // open/close the relay
    if( flowRate > 0 ){
      setRelayClosed(true);
      publishMqtt("{\"device_id\": \"water_bot\", \"flowRate\":" + (String)flowRate + ",\"total_volume\":" + (String)totalLitres + "}\n");
      waterFlowing = true;
    } else {
      if( waterFlowing ){
        setRelayClosed(false);
        publishMqtt("{\"device_id\": \"water_bot\", \"flowRate\":" + (String)flowRate + ",\"total_volume\":" + (String)totalLitres + "}\n");
        waterFlowing = false;
      }
    }
  }
}

void setRelayClosed(bool close){
  if(close){
    digitalWrite(relay, HIGH);
    Serial.println("Current Flowing");
    publishMqtt("{\"device_id\": \"water_bot\", \"uv_status\":1}\n");

  } else {
    Serial.println("Current not Flowing");
    publishMqtt("{\"device_id\": \"water_bot\", \"uv_status\":0}\n");
    delay(3000);
    digitalWrite(relay, LOW);
  }
}

void publishMqtt(String msg){
  //create string payload for publishing
  int len = msg.length();
  char payload[len];
  msg.toCharArray(payload, len);

  Serial.println("Publishing:- ");
  Serial.println(payload);
  if (aws.publish(MQTT_TOPIC, payload) == 0){// publishes payload and returns 0 upon success
    Serial.println("Success\n");
  }else{
    Serial.println("Failed! waiting and then will try again....\n");
    delay(1000);
    Serial.println("trying again....");
    
    if (aws.publish(MQTT_TOPIC, payload) == 0){// publishes payload and returns 0 upon success
      Serial.println("Success\n");
    }else{
      Serial.println("Failed on second attempt! moving on...\n");
    
    }
  }
}