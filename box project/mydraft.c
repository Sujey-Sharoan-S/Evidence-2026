#include<SPI.h>
#include<Ethernet.h>
#include<Wire.h>


#define SENSOR1 4
#define SENSOR2 5

#define ETH_CS    16
#define ETH_SCLK  15
#define ETH_MISO  14
#define ETH_MOSI  13
#define ETH_RST   39

#define TCA_ADDR  0x20
#define TCA_OUTPUT_REG 0x01
#define TCA_CONFIG_REG 0x03

#define LOCK1 0x01
#define LOCK2 0x02
#define RED_LED   0x04
#define GREEN_LED 0x08

uint8_t outputState = 0;

// ---------------- NETWORK ----------------
 
// Change the IP and MAC for the second device.
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress ip(192, 168, 1, 77);
 
EthernetServer server(5111);
EthernetClient client;

// PULSE TIMERS
const unsigned long LOCK1_PULSE_TIMER = 2000;
const unsigned long LOCK2_PULSE_TIMER = 5000;

// FOR TCP ALIVE CHECKING
const unsigned long IDLE_TIME_BEFORE_ALIVE = 60000;
const unsigned long ALIVE_INTERVAL = 30000;

// LED ACTIVE
const unsigned long GREEN_INT = 3500;
const unsigned long RED_INT = 5000;

unsigned long GreenTime = 0;
unsigned long RedTime = 0;

bool GreenActive = false;
bool redactive = false;

// ---------------- ALIVE STATE ----------------
unsigned long lastRxTime = 0;
unsigned long aliveStartTime = 0;
unsigned long lastAliveTime = 0;

bool aliveActive = false;
// ---------------- DOOR STATE ----------------
 
bool door1State = false;
bool door2State = false;
 
bool prevDoor1State = false;
bool prevDoor2State = false;
 
bool handshakeComplete = false;
bool initialStateSent = false;

// ---------------- LOCK TIMERS ----------------
 
unsigned long lock1Timer = 0;
unsigned long lock2Timer = 0;
 
bool lock1Active = false;
bool lock2Active = false;

// ---------------- FUNCTION DECLARATIONS ----------------
 
void writeTCA(uint8_t reg, uint8_t value);
void setOutput(uint8_t mask, bool on);
void pulseLock1();
void pulseLock2();
void handleTimers();
void handleClient();
void sendMsg(const String &msg);
void sendDoorState(int door, bool closed);
void sendInitialDoorState();
bool isDoorClosed(int doorPin);
void setStatusLED(bool red, bool green);

// ---------------- TCA9554 OUTPUT CONTROL ----------------
 
void writeTCA(uint8_t reg, uint8_t value)
{
    Wire.beginTransmission(TCA_ADDR);
    Wire.write(reg);
    Wire.write(value);
    uint8_t err = Wire.endTransmission(); 
    
    // Send the data to the TCA9554 and check for errors
    if (err != 0) // If there was an error, print a warning message to the serial monitor
    {
        Serial.print("[WARN] TCA9554 write error: ");
        Serial.println(err);
    }
}

void setOutput(uint8_t mask, bool on)
{
    if (on)
        outputState |= mask;
    else
        outputState &= ~mask;
 
    writeTCA(TCA_OUTPUT_REG, outputState);
}

// LOCK1 IS ON 
void pulseLock1()
{
    setOutput(LOCK1, true);
    lock1Timer = millis();
    lock1Active = true;

}
// LOCK2 IS ON
void pulseLock2()
{
    setOutput(LOCK2, true);
    lock2Timer = millis();
    lock2Active = true;

}

// LED STATUS 
void setStatusLED(bool red, bool green)
{
    setOutput(RED_LED, red);
    setOutput(GREEN_LED, green);
}

// SETUP 

void setup()
{
    Serial.begin(115200);
 
    // Give Native USB CDC time to connect with the PC host
    unsigned long startWait = millis();
    while (!Serial && (millis() - startWait < 3000))
    {
        delay(10);
    }
    delay(500);
 
    Serial.println("  ESP32-S3 PoE System Starting Up...     ");

 
    // Door inputs
    pinMode(SENSOR1, INPUT_PULLUP);
    pinMode(SENSOR2, INPUT_PULLUP);
 
    // I2C for TCA9554
    Serial.println("[INIT] Starting I2C (SDA=42, SCL=41)...");
    Wire.begin(42, 41);

    
    writeTCA(TCA_CONFIG_REG, 0xF0);     // Configure TCA9554 pins as outputs.
    
    outputState = 0;                    // Initialize all outputs OFF
    writeTCA(TCA_OUTPUT_REG, outputState);

    // Reset W5500
    Serial.println("[INIT] Resetting W5500 Ethernet...");
    pinMode(ETH_RST, OUTPUT);
    digitalWrite(ETH_RST, LOW);
    delay(100);
    digitalWrite(ETH_RST, HIGH);
    delay(200);
 
    // Initialize Ethernet SPI
    Serial.println("[INIT] Initializing Ethernet SPI...");
    SPI.begin(ETH_SCLK, ETH_MISO, ETH_MOSI, ETH_CS);
    Ethernet.init(ETH_CS);
    delay(1000);
    // Static IP
    Serial.print("[INIT] Configuring static IP: ");
    Serial.println(ip);
    Ethernet.begin(mac, ip);
 
    delay(500);
 
    if (Ethernet.hardwareStatus() == EthernetNoHardware)
    {
        Serial.println("[ERROR] W5500 Ethernet controller not found! Check SPI connections.");
    }
    else if (Ethernet.linkStatus() == LinkOFF)
    {
        Serial.println("[WARN] Ethernet cable is not connected (Link OFF).");
        while(Ethernet.linkStatus() == LinkOFF)
        {
            Serial.println("Connect Ethernet");
            delay(500);
        }
        Serial.println("Ethernet Connected Successfully");
    }
    else
    {
        Serial.println("[OK] Ethernet cable connected (Link ON).");
    }
 
    server.begin();
    Serial.print("[OK] Server listening on port 5111, IP: ");
    Serial.println(Ethernet.localIP());

    prevDoor1State = digitalRead(SENSOR1);
    prevDoor2State = digitalRead(SENSOR2);
 
    door1State = prevDoor1State;
    door2State = prevDoor2State;

    Serial.println("  System Ready! ");
}

// ---------------- MAIN LOOP ----------------
 
void loop()
{
    static unsigned long lastHeartbeat = 0;
    if (millis() - lastHeartbeat >= 10000)
    {
        lastHeartbeat = millis();
        if (!client || !client.connected())
        {
            Serial.println("[Heartbeat] Waiting for client connection on port 5111...");
        }
    }
    if (!client || !client.connected())
    {
        EthernetClient newClient = server.available();
 
        if (newClient)
        {
            client = newClient;
 
            handshakeComplete = false;
            initialStateSent = false;
            aliveActive = false;
            lastRxTime = millis();
 
            Serial.println("Client connected");
        }
    }
 
    if (client && client.connected())
    {
        handleClient();
    }
 
    handleTimers();
}
// ---------------- CLIENT HANDLING ----------------
 
void handleClient()
{
    door1State = digitalRead(SENSOR1);
    door2State = digitalRead(SENSOR2);
 
    if (client.available())
    {
        String cmd = client.readStringUntil('\n');
        cmd.trim();
 
        Serial.println("RX: " + cmd);
 
        lastRxTime = millis();
        aliveActive = false;
 
        // HANDSHAKE
        if (cmd == "MAE" && !handshakeComplete)
        {
            sendMsg("MAE WELCOME");
            handshakeComplete = true;
            initialStateSent = false;
            Serial.println("MAE WELCOME");
        }
        else if (cmd == "1DoorOpen")
        {
            sendMsg("1DoorOpened");
            pulseLock1();
        }
        else if (cmd == "2DoorOpen")
        {
            sendMsg("2DoorOpened");
            pulseLock2();
        }
        else if (cmd == "3DoorOpen")
        {
            sendMsg("3DoorOpened");
            pulseLock1();
            pulseLock2();
        }
        // CLOSE DOORS
        else if (cmd == "1DoorClose")
        {
            setOutput(LOCK1, false);
            lock1Active = false;
            lock1Timer = 0;
 
            if (!isDoorClosed(SENSOR1))
                sendMsg("1DoorClosed");
        }
        else if (cmd == "2DoorClose")
        {
            setOutput(LOCK2, false);
            lock2Active = false;
            lock2Timer = 0;
 
            if (!isDoorClosed(SENSOR2))
                sendMsg("2DoorClosed");
        }
        else if (cmd == "3DoorClose")
        {
            setOutput(LOCK1, false);
            setOutput(LOCK2, false);
 
            lock1Active = false;
            lock2Active = false;
            lock1Timer = 0;
            lock2Timer = 0;
 
            if (!isDoorClosed(SENSOR1) &&
                !isDoorClosed(SENSOR2))
            {
                sendMsg("3DoorClosed");
            }
        }
        // STATUS
        else if (cmd == "status")
        {
            int sta1 = digitalRead(SENSOR1);
            int sta2 = digitalRead(SENSOR2);
 
            if (sta1 == LOW && sta2 == LOW)
            {
                sendMsg("3DoorOpened");
            }
            else if (sta1 == HIGH && sta2 == LOW)
            {
                sendMsg("1DoorClosed");
                sendMsg("2DoorOpened");
            }
            else if (sta1 == LOW && sta2 == HIGH)
            {
                sendMsg("1DoorOpened");
                sendMsg("2DoorClosed");
            }
            else
            {
                sendMsg("3DoorClosed");
            }
        }
              // STATUS LED
        else if (cmd == "OK")
        {
            setStatusLED(false, true);
            pulseLock2();

            GreenTime = millis();
            GreenActive = true;    
        }   
        else if (cmd == "REJ")
        {
             setStatusLED(true, false);
             pulseLock1();

            RedTime = millis();
            redactive = true;
        }
          
    }
    if (handshakeComplete && !initialStateSent)
    {
        sendInitialDoorState();
        initialStateSent = true;
    }
 
    // Refresh input readings
    door1State = digitalRead(SENSOR1);
    door2State = digitalRead(SENSOR2);
 
    if (door1State != prevDoor1State)
    {
        prevDoor1State = door1State;
        sendDoorState(1, door1State);
    }
 
    if (door2State != prevDoor2State)
    {
        prevDoor2State = door2State;
        sendDoorState(2, door2State);
    }
 
    if (!client.connected())
    {
        client.stop();
        aliveActive = false;
        Serial.println("Client disconnected");
    }
}
void handleTimers()
{
    if ((GreenActive && millis() - GreenTime >= GREEN_INT))// && digitalRead(SENSOR2) == LOW)
    {
        setOutput(GREEN_LED, false);
        GreenActive = false;
    } 
    if ((redactive && millis() - RedTime >= RED_INT))// && digitalRead(SENSOR1) == LOW)
    {
        setOutput(RED_LED, false);
        redactive = false;
    }
    unsigned long now = millis();
 
    if (lock1Active &&
        now - lock1Timer >= LOCK1_PULSE_TIMER)
    {
        setOutput(LOCK1, false);
        lock1Active = false;
        lock1Timer = 0;
    }
 
    if (lock2Active &&
        now - lock2Timer >= LOCK2_PULSE_TIMER)
    {
        setOutput(LOCK2, false);
        lock2Active = false;
        lock2Timer = 0;
    }
}
void sendMsg(const String &msg)
{
    if (client && client.connected())
    {
        client.println(msg);
        Serial.println("TX: " + msg);
    }
}
 
void sendDoorState(int door, bool closed)
{
    sendMsg(
        String(door) + "Door" +
        (closed ? "Closed" : "Opened")
    );
}
 
void sendInitialDoorState()
{
    if (prevDoor1State && prevDoor2State)
        sendMsg("3DoorClosed");
    else if (!prevDoor1State && !prevDoor2State)
        sendMsg("3DoorOpened");
    else if (!prevDoor1State && prevDoor2State)
        sendMsg("1DoorOpened");
    else
        sendMsg("2DoorOpened");
}
 
bool isDoorClosed(int doorPin)
{
    return digitalRead(doorPin) == HIGH;
}

