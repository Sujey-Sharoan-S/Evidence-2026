#include <Arduino.h>
#include<WiFi.h>
#include<Adafruit_GFX.h>
#include<Adafruit_SSD1306.h>
#include <Wire.h>
#include <time.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const char* ssid ="Airtel_sund_5553";
const char* password = "air07394";

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800;      // India = UTC+5:30
const int daylightOffset_sec = 0;

void setup() 
{
  Serial.begin(115200);
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println("OLED FAILED");
    while(1);
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

}

void loop() 
{
  struct tm timeinfo;

if (!getLocalTime(&timeinfo))
{
    return;
}

char timeString[20];
char dateString[20];

strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);
strftime(dateString, sizeof(dateString), "%d-%m-%Y", &timeinfo);

display.clearDisplay();

display.setTextSize(2);
display.setCursor(16, 10);
display.println(timeString);

display.setTextSize(1);
display.setCursor(30, 40);
display.println(dateString);

display.display();

delay(1000);


}


