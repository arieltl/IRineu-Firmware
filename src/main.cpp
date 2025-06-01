// IR Receiver Example - Cleaned Version
// Receives and decodes IR signals using IRremoteESP8266 library

#include <Arduino.h>
#include <IRac.h>
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRtext.h>
#include <IRutils.h>
#include <assert.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Utils.h>

#include "env.h"

// WiFi and MQTT configuration
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
const char *mqtt_server = MQTT_SERVER;
const char *pairing_code = PAIRING_CODE;
const char *mqtt_ac_command = "ac/" PAIRING_CODE "/command";
const char *mqtt_ac_report = "ac/" PAIRING_CODE "/state";

const char *mqtt_raw_command = "raw/" PAIRING_CODE "/command";
const char *mqtt_raw_report = "raw/" PAIRING_CODE "/report";


#define MAX_ELEMS    400
#define MAX_STRLEN  (MAX_ELEMS*5 + 8)  // worst-case input length
char sharedBuf[MAX_STRLEN]; //needed to save memory

// Make these variables accessible from other files
extern const size_t max_str_len = MAX_STRLEN;
extern const size_t max_elems = MAX_ELEMS;

WiFiClient espClient;
PubSubClient client(espClient);
ESP8266WebServer server(80);

// Increase MQTT buffer size
const int mqtt_buffer_size = MAX_STRLEN + 100;  // Increased from default 128 bytes

// === BEGIN CONFIGURATION ===
const uint16_t kRecvPin = 5; // Use GPIO 5 (D1 on NodeMCU)
#define kSendPin  4 // Use GPIO 4 (D2 on NodeMCU)
const uint32_t kBaudRate = 115200; // Serial baud rate
const uint16_t kCaptureBufferSize = 1024; // Reduced from 1024 to 512
#if DECODE_AC
const uint8_t kTimeout = 50; // Timeout for A/C remotes
#else
const uint8_t kTimeout = 15; // Timeout for most remotes
#endif
const uint16_t kMinUnknownSize = 12; // Minimum size for unknown messages
const uint8_t kTolerancePercentage = kTolerance; // Signal tolerance
#define LEGACY_TIMING_INFO false
#define REBOOT_BUTTON_PIN 0
// === END CONFIGURATION ===

IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, true);
decode_results results;

void connectToWiFi() {
    Serial.print("Connecting to WiFi");
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("Connected to WiFi");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
}

void connectToMQTT() {
    while (!client.connected()) {
        Serial.print("Connecting to MQTT...");
        if (client.connect("ESP8266Client", "test","test")) {
            Serial.println("connected");
            client.subscribe(mqtt_ac_command);
            client.subscribe(mqtt_raw_command);
        } else {
            Serial.print("failed with state ");
            Serial.println(client.state());
            delay(2000);
        }
    }
}

void callback(char *topic, byte *payload, unsigned int length) {
    // Check if payload is too long for our buffer
    if (length >= MAX_STRLEN) {
        Serial.println("Message too long, ignoring");
        return;
    }

    // Copy payload directly to shared buffer
    memcpy(sharedBuf, payload, length);
    sharedBuf[length] = '\0';  // Null terminate

    Serial.print("Message received: ");
    Serial.println(sharedBuf);
    irrecv.disableIRIn(); // avoid echo signal

    if (strcmp(topic, mqtt_ac_command) == 0) {
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(doc, sharedBuf);

        if (error) {
            Serial.print("Failed to parse JSON: ");
            Serial.println(error.c_str());
            return;
        }

        // Extract AC state from JSON
        bool power = doc["power"] | false;
        int temperature = doc["temperature"] | 24;
        const char* mode = doc["mode"] | "cool";
        const char* fan = doc["fan"] | "auto";
        const char* protocol = doc["protocol"] | "GREE";
        int model = doc["model"] | 0;

        // Configure IR command
        IRac ac(kSendPin);
        ac.next.protocol = strToDecodeType(protocol);
        ac.next.model = model;
        ac.next.power = power;
        ac.next.degrees = temperature;
        ac.next.mode = IRac::strToOpmode(mode, stdAc::opmode_t::kAuto);
        ac.next.fanspeed = IRac::strToFanspeed(fan, stdAc::fanspeed_t::kAuto);

        // Send IR command
        ac.sendAc();
        Serial.println("IR command sent.");
    }
    else if (strcmp(topic, mqtt_raw_command) == 0) {
        size_t outLen;
        uint16_t* data = parseHexMessage(outLen); // outLen is passed by reference
        
        if (data == nullptr || outLen == 0) {
            Serial.println("Invalid raw command format");
            return;
        }
        
        IRsend irsend(kSendPin);
        irsend.begin(); // Required before sending
        irsend.sendRaw(data, outLen, 38); // 38 kHz typical IR carrier
        
        delete[] data;  // Clean up allocated memory
    }
    irrecv.enableIRIn(); // enable IR receiver
}

// Interrupt service routine for reboot button
ICACHE_RAM_ATTR void rebootISR() {
    Serial.println("Reboot button pressed. Rebooting...");
    ESP.restart();
}

void handleRoot() {
    String html = R"html(
<!DOCTYPE html>
<html>
<head>
    <title>IR Remote Control</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <style>
        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            margin: 0;
            padding: 20px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
        }
        .container {
            background: white;
            padding: 40px;
            border-radius: 15px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.2);
            text-align: center;
            max-width: 400px;
            width: 100%;
        }
        h1 {
            color: #333;
            margin-bottom: 30px;
            font-size: 2em;
        }
        .pairing-code {
            background: #f8f9fa;
            border: 2px solid #e9ecef;
            border-radius: 10px;
            padding: 20px;
            margin: 20px 0;
            font-size: 2.5em;
            font-weight: bold;
            color: #495057;
            letter-spacing: 3px;
            font-family: 'Courier New', monospace;
        }
        .info {
            color: #666;
            font-size: 0.9em;
            margin-top: 20px;
            line-height: 1.6;
        }
        .status {
            background: #d4edda;
            color: #155724;
            padding: 10px;
            border-radius: 5px;
            margin: 15px 0;
            border: 1px solid #c3e6cb;
        }
        .ip-address {
            font-family: 'Courier New', monospace;
            font-weight: bold;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔌 IR Remote Control</h1>
        <div class="status">
            ✅ Device Connected
        </div>
        <div class="pairing-code">)html";
    
    html += pairing_code;
    
    html += R"html(</div>
        <div class="info">
            <p><strong>Pairing Code</strong></p>
            <p>Use this code to pair your device with the IR controller.</p>
            <p>Device IP: <span class="ip-address">)html";
    
    html += WiFi.localIP().toString();
    
    html += R"html(</span></p>
            <p>MAC Address: <span class="ip-address">)html";
    
    html += WiFi.macAddress();
    
    html += R"html(</span></p>
        </div>
    </div>
</body>
</html>)html";
    
    server.send(200, "text/html", html);
}

void handleNotFound() {
    server.send(404, "text/plain", "Page not found");
}

void setup()
{
    Serial.begin(kBaudRate, SERIAL_8N1, SERIAL_TX_ONLY);
    while (!Serial)
        delay(50); // Wait for serial connection
    assert(irutils::lowLevelSanityCheck() == 0);

    Serial.printf("\n" D_STR_IRRECVDUMP_STARTUP "\n", kRecvPin);
#if DECODE_HASH
    irrecv.setUnknownThreshold(kMinUnknownSize);
#endif
    irrecv.setTolerance(kTolerancePercentage);
    irrecv.enableIRIn();

    Serial.println("Example topic: " );
    Serial.println(mqtt_ac_command);

    // Setup reboot button interrupt
    pinMode(REBOOT_BUTTON_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(REBOOT_BUTTON_PIN), rebootISR, FALLING);
    
    connectToWiFi();
    
    // Setup web server
    server.on("/", handleRoot);
    server.onNotFound(handleNotFound);
    server.begin();
    Serial.println("Web server started");
    Serial.print("Access the pairing page at: http://");
    Serial.println(WiFi.localIP());
    
    client.setServer(mqtt_server, 1883);
    client.setBufferSize(mqtt_buffer_size);  // Set the larger buffer size
    client.setCallback(callback);
}

void loop()
{
    server.handleClient(); // Handle web server requests
    
    if (!client.connected()) {
        connectToMQTT();
    }
    client.loop();

    if (irrecv.decode(&results)) {
        if (results.overflow)
            Serial.printf(D_WARN_BUFFERFULL "\n", kCaptureBufferSize);
        Serial.println(D_STR_LIBRARY "   : v" _IRREMOTEESP8266_VERSION_STR "\n");
        if (kTolerancePercentage != kTolerance)
            Serial.printf(D_STR_TOLERANCE " : %d%%\n", kTolerancePercentage);
        Serial.print(resultToHumanReadableBasic(&results));
        String description = IRAcUtils::resultAcToString(&results);
        if (description.length()) {
            Serial.println(D_STR_MESGDESC ": " + description);
            Serial.println("AC command received");
            StaticJsonDocument<128> jsonDoc; // Reduced from 256 to 128
            stdAc::state_t state;
            IRAcUtils::decodeToState(&results, &state);
            jsonDoc["protocol"] = typeToString(state.protocol);
            jsonDoc["model"] = state.model;
            jsonDoc["power"] = state.power;
            jsonDoc["temperature"] = state.degrees;
            jsonDoc["mode"] = IRac::opmodeToString(state.mode);
            jsonDoc["fan"] = IRac::fanspeedToString(state.fanspeed);

            serializeJson(jsonDoc, sharedBuf);
            Serial.println(sharedBuf);
            Serial.println();
            client.publish(mqtt_ac_report, sharedBuf);
        }
        else {
            Serial.println("Non AC command received");
            uint16_t *raw_array = resultToRawArray(&results);
            // Find out how many elements are in the array.
            uint16_t size = getCorrectedRawLength(&results);
            uint16ArrayToHexString(raw_array, size, sharedBuf, sizeof(sharedBuf));
            
            Serial.print("Publishing to topic: ");
            Serial.println(mqtt_raw_report);
            Serial.print("MQTT connected: ");
            Serial.println(client.connected() ? "yes" : "no");
            
            bool published = client.publish(mqtt_raw_report, sharedBuf);
            Serial.print("Publish result: ");
            Serial.println(published ? "success" : "failed");
            
            delete[] raw_array;
            Serial.print("Size: ");
            Serial.println(size);
            Serial.println(sharedBuf);
            Serial.println();
        } 
        yield();
#if LEGACY_TIMING_INFO
        Serial.println(resultToTimingInfo(&results));
        yield();
#endif
        //Serial.println(resultToSourceCode(&results));
        //Serial.println();
        yield();
    }
}
