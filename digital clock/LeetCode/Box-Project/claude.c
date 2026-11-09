#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <Wire.h>
 
/* =========================================================
   BOARD : Waveshare ESP32-S3-POE-ETH-8DI-8DO (also 8DI-8RO)
   ========================================================= */
 
/* ---------------- BOX SELECT ---------------- */
// Uncomment ONE box
#define BOX_WHITE_WOODEN
//#define BOX_STEEL
 
/* ---------------- DIGITAL INPUTS (DI1..DI8 = GPIO4..GPIO11) ---------------- */
#define DOOR1   4     // DI1
#define DOOR2   5     // DI2
 
// Waveshare DI is optocoupled: signal present -> pin reads LOW.
// Old code treated HIGH = door closed. readDoor() keeps that meaning.
// If door logic comes out inverted, change this to HIGH.
#define DI_ACTIVE_LEVEL  LOW
 
/* ---------------- DIGITAL OUTPUTS (via TCA9554 I/O expander) ---------------- */
#define I2C_SDA        42
#define I2C_SCL        41
#define TCA9554_ADDR   0x20
#define TCA_REG_OUTPUT 0x01
#define TCA_REG_CONFIG 0x03
 
// DO outputs are active-LOW: expander pin LOW -> DO conducts to GND (ON).
// Expander pin HIGH -> DO open (OFF). If outputs come out inverted, change to HIGH.
#define DO_ACTIVE_LEVEL  LOW
 
// DO channel numbers (0 = DO1 ... 7 = DO8)
#define LOCK1      0   // DO1
#define LOCK2      1   // DO2
#define REJ_OUT    2   // DO3 - ON when "REJ" received
#define OK_OUT     3   // DO4 - ON when "OK" received
 
/* ---------------- W5500 ETHERNET PINS ---------------- */
#define ETH_SCK   15
#define ETH_MISO  14
#define ETH_MOSI  13
#define ETH_CS    16
#define ETH_RST   39
 
/* ---------------- NETWORK ---------------- */
#if defined(BOX_WHITE_WOODEN)
/*-------White Wooden BOX Mac and IP---------- */
  byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
  IPAddress ip(192, 168, 1, 76);
#elif defined(BOX_STEEL)
/*-------Steel BOX Mac and IP---------- */
  byte mac[] = { 0xD0, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
  IPAddress ip(192, 168, 1, 81);
#endif
 
EthernetServer server(5111);
EthernetClient client;
 
/* ---------------- TIMING ---------------- */
const unsigned long LOCK_PULSE_TIMER1 = 2000;
const unsigned long LOCK_PULSE_TIMER2 = 5000;
const unsigned long LED_ON_TIME = 5000;
 
/* ---------------- IDLE ALIVE ---------------- */
//const unsigned long IDLE_TIME_BEFORE_ALIVE = 1800000; // 30 minutes
const unsigned long IDLE_TIME_BEFORE_ALIVE = 60000;
const unsigned long ALIVE_INTERVAL = 30000;
 
unsigned long lastRxTime = 0;
unsigned long aliveStartTime = 0;
unsigned long lastAliveTime = 0;
bool aliveActive = false;
 
/* ---------------- STATE ---------------- */
bool door1State, door2State;
bool prevDoor1State, prevDoor2State;
bool handshakeComplete = false;
bool initialStateSent = false;
 
unsigned long lock1Timer = 0;
unsigned long lock2Timer = 0;
unsigned long okOutTimer = 0;
unsigned long rejOutTimer = 0;
 
uint8_t doShadow = 0x00;   // logical state of DO1..DO8 (1 = ON)
 
/* ---------------- PROTOTYPES ---------------- */
void handleIdleAlive();
void handleClient();
void handleTimers();
void sendMsg(const String &msg);
void pulseLock(int ch, unsigned long &timer);
void sendDoorState(int door, bool closed);
void sendInitialDoorState();
bool isDoorClosed(int doorPin);
 
/* ---------------- BOARD I/O LAYER ---------------- */
void tcaWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(TCA9554_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}
 
void doWrite(uint8_t ch, uint8_t level) {
  if (level) doShadow |=  (1 << ch);
  else       doShadow &= ~(1 << ch);
  // Convert logical ON/OFF to the expander pin level
  uint8_t pins = (DO_ACTIVE_LEVEL == LOW) ? (uint8_t)~doShadow : doShadow;
  tcaWrite(TCA_REG_OUTPUT, pins);
}
 
// Returns HIGH = closed, LOW = open (same meaning as old board)
int readDoor(int pin) {
  return (digitalRead(pin) == DI_ACTIVE_LEVEL) ? HIGH : LOW;
}
 
/* ---------------- SETUP ---------------- */
void setup() {
  Serial.begin(115200);
 
  // Digital inputs
  pinMode(DOOR1, INPUT_PULLUP);
  pinMode(DOOR2, INPUT_PULLUP);
 
  // Digital outputs (TCA9554): all 8 outputs OFF by default.
  // An output only turns ON by command and goes OFF again by its timer.
  Wire.begin(I2C_SDA, I2C_SCL);
  // Set output latch to OFF level first, then switch pins to output mode
  doShadow = 0x00;
  doWrite(0, LOW);
  tcaWrite(TCA_REG_CONFIG, 0x00);
 
  // W5500 hardware reset
  pinMode(ETH_RST, OUTPUT);
  digitalWrite(ETH_RST, LOW);
  delay(50);
  digitalWrite(ETH_RST, HIGH);
  delay(200);
 
  SPI.begin(ETH_SCK, ETH_MISO, ETH_MOSI, ETH_CS);
  Ethernet.init(ETH_CS);
  if (Ethernet.begin(mac) == 0) {
    Ethernet.begin(mac, ip);
  }
 
  server.begin();
  Serial.print("IP: ");
  Serial.println(Ethernet.localIP());
 
  prevDoor1State = readDoor(DOOR1);
  prevDoor2State = readDoor(DOOR2);
}
 
/* ---------------- MAIN LOOP ---------------- */
void loop() {
 
  if (!client || !client.connected()) {
    EthernetClient newClient = server.available();
    if (newClient) {
      client = newClient;
      handshakeComplete = false;
      initialStateSent = false;
      aliveActive = false;
      lastRxTime = millis();
      Serial.println("Client connected");
    }
  }
 
  if (client && client.connected()) {
    handleClient();
    handleIdleAlive();
  }
 
  handleTimers();
}
 
/* ---------------- CLIENT HANDLING ---------------- */
void handleClient() {
 
  door1State = readDoor(DOOR1);
  door2State = readDoor(DOOR2);
 
  if (client.available()) {
    String cmd = client.readStringUntil('\n');
    cmd.trim();
    Serial.println("RX: " + cmd);
 
    lastRxTime = millis();
    aliveActive = false;
 
    // ---- HANDSHAKE ----
    if (cmd == "MAE" && !handshakeComplete) {
      sendMsg("MAE WELCOME");
      handshakeComplete = true;
      initialStateSent = false;
    }
 
    // ---- DOOR COMMANDS ----
    else if (cmd == "1DoorOpen") {
      sendMsg("1DoorOpened");
      pulseLock(LOCK1, lock1Timer);
    }
    else if (cmd == "2DoorOpen") {
      sendMsg("2DoorOpened");
      pulseLock(LOCK2, lock2Timer);
    }
    else if (cmd == "3DoorOpen") {
      sendMsg("3DoorOpened");
      pulseLock(LOCK1, lock1Timer);
      pulseLock(LOCK2, lock2Timer);
    }
    else if (cmd == "1DoorClose") {
      doWrite(LOCK1, LOW);
      if (!isDoorClosed(DOOR1))
        sendMsg("1DoorClosed");
    }
    else if (cmd == "2DoorClose") {
      doWrite(LOCK2, LOW);
      if (!isDoorClosed(DOOR2)) {
        sendMsg("2DoorClosed");
      }
    }
    else if (cmd == "3DoorClose") {
      doWrite(LOCK1, LOW);
      doWrite(LOCK2, LOW);
      if (!isDoorClosed(DOOR1) && !isDoorClosed(DOOR2))
        sendMsg("3DoorClosed");
    }
    else if (cmd == "status") {
      int sta1 = readDoor(DOOR1);
      int sta2 = readDoor(DOOR2);
      if (sta1 == LOW && sta2 == LOW) {
        sendMsg("3DoorOpened");
      }
      else if (sta1 == HIGH && sta2 == LOW) {
        sendMsg("1DoorClosed");
        delay(100);
        sendMsg("2DoorOpened");
      }
      else if (sta1 == LOW && sta2 == HIGH) {
        sendMsg("1DoorOpened");
        delay(100);
        sendMsg("2DoorClosed");
      }
      else if (sta1 == HIGH && sta2 == HIGH) {
        sendMsg("3DoorClosed");
      }
    }
    // ---- OK / REJ OUTPUTS ----
    else if (cmd == "OK") {
      doWrite(OK_OUT, HIGH);          // DO4 ON
      okOutTimer = millis();
    }
    else if (cmd.indexOf("REJ") != -1) {
      doWrite(REJ_OUT, HIGH);         // DO3 ON
       pulseLock(LOCK1, lock1Timer);
      rejOutTimer = millis();
    }
  }
 
  if (handshakeComplete && !initialStateSent) {
    sendInitialDoorState();
    initialStateSent = true;
  }
 
  if (door1State != prevDoor1State) {
    prevDoor1State = door1State;
    sendDoorState(1, door1State);
  }
 
  if (door2State != prevDoor2State) {
    prevDoor2State = door2State;
    sendDoorState(2, door2State);
  }
 
  if (!client.connected()) {
    client.stop();
    aliveActive = false;
    Serial.println("Client disconnected");
  }
}
 
/* ---------------- IDLE ALIVE ---------------- */
void handleIdleAlive() {
 
  if (!client || !client.connected()) return;
 
  unsigned long now = millis();
 
  if (!aliveActive && (now - lastRxTime >= IDLE_TIME_BEFORE_ALIVE)) {
    aliveActive = true;
    aliveStartTime = now;
    lastAliveTime = 0;
    Serial.println("Alive cycle started");
  }
 
  if (aliveActive) {
    if (now - aliveStartTime >= IDLE_TIME_BEFORE_ALIVE) {
      aliveActive = false;
      Serial.println("Alive cycle stopped");
      return;
    }
 
    if (now - lastAliveTime >= ALIVE_INTERVAL) {
      client.println("Alive");
      Serial.println("TX: Alive");
      lastAliveTime = now;
    }
  }
}
 
/* ---------------- HELPERS ---------------- */
void sendMsg(const String &msg) {
  client.println(msg);
  Serial.println("TX: " + msg);
}
 
void pulseLock(int ch, unsigned long &timer) {
  doWrite(ch, HIGH);
  delay(2000);
  timer = millis();
}
 
void handleTimers() {
  if (lock1Timer && millis() - lock1Timer >= LOCK_PULSE_TIMER1) {
    doWrite(LOCK1, LOW);
    lock1Timer = 0;
  }
  if (lock2Timer && millis() - lock2Timer >= LOCK_PULSE_TIMER2) {
    doWrite(LOCK2, LOW);
    lock2Timer = 0;
  }
  if (okOutTimer && millis() - okOutTimer >= LED_ON_TIME) {
    doWrite(OK_OUT, LOW);
    okOutTimer = 0;
  }
  if (rejOutTimer && millis() - rejOutTimer >= LED_ON_TIME) {
    doWrite(REJ_OUT, LOW);
    rejOutTimer = 0;
  }
}
 
void sendDoorState(int door, bool closed) {
  sendMsg(String(door) + "Door" + (closed ? "Closed" : "Opened"));
}
 
void sendInitialDoorState() {
  if (prevDoor1State && prevDoor2State)
    sendMsg("3DoorClosed");
  else if (!prevDoor1State && !prevDoor2State)
    sendMsg("3DoorOpened");
  else if (!prevDoor1State && prevDoor2State)
    sendMsg("1DoorOpened");
  else
    sendMsg("2DoorOpened");
}
 
bool isDoorClosed(int doorPin) {
  return readDoor(doorPin) == HIGH;
}