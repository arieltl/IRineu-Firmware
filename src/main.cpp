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
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "env.h"

// WiFi and MQTT configuration
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
const char *mqtt_server = MQTT_SERVER;
const char *mqtt_ac_command = "ac/state";
const char *mqtt_ac_report = "ac/command";

const char *mqtt_raw_command = "raw/state";
const char *mqtt_raw_report = "raw/command";

WiFiClient espClient;
PubSubClient client(espClient);

// === BEGIN CONFIGURATION ===
const uint16_t kRecvPin = 5; // Use GPIO 5 (D1 on NodeMCU)
#define kSendPin  4 // Use GPIO 4 (D2 on NodeMCU)
const uint32_t kBaudRate = 115200; // Serial baud rate
const uint16_t kCaptureBufferSize = 1024; // Buffer for IR data
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
}

void connectToMQTT() {
    while (!client.connected()) {
        Serial.print("Connecting to MQTT...");
        if (client.connect("ESP8266Client")) {
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
    String message;
    for (int i = 0; i < length; i++) {
        message += (char)payload[i];
    }
    Serial.print("Message received: ");
    Serial.println(message);

    if (String(topic) == mqtt_ac_command) {
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, message);

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
    else if (String(topic) == mqtt_raw_command) {
        uint16_t* data = (uint16_t*) payload;
        uint16_t len = data[0];
        
        if (len == 0) {
            return;
        }
        
        data++;
        IRsend irsend(kSendPin);
        irsend.begin(); // Required before sending
        irsend.sendRaw(data, len, 38); // 38 kHz typical IR carrier
    }
}

void checkRebootButton() {
    pinMode(REBOOT_BUTTON_PIN, INPUT_PULLUP);
    if (digitalRead(REBOOT_BUTTON_PIN) == LOW) {
        Serial.println("Reboot button pressed. Rebooting...");
        ESP.restart();
    }
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

    connectToWiFi();
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);
}

void loop()
{
    if (!client.connected()) {
        connectToMQTT();
    }
    client.loop();

    checkRebootButton();

    if (irrecv.decode(&results)) {
        uint32_t now = millis();
        Serial.printf(D_STR_TIMESTAMP " : %06u.%03u\n", now / 1000, now % 1000);
        if (results.overflow)
            Serial.printf(D_WARN_BUFFERFULL "\n", kCaptureBufferSize);
        Serial.println(D_STR_LIBRARY "   : v" _IRREMOTEESP8266_VERSION_STR "\n");
        if (kTolerancePercentage != kTolerance)
            Serial.printf(D_STR_TOLERANCE " : %d%%\n", kTolerancePercentage);
        Serial.print(resultToHumanReadableBasic(&results));
        String description = IRAcUtils::resultAcToString(&results);
        if (description.length()) {
            Serial.println(D_STR_MESGDESC ": " + description);

            // Create a custom JSON object to publish IR command details
            StaticJsonDocument<256> jsonDoc;
            stdAc::state_t state;
            IRAcUtils::decodeToState(&results, &state);
            jsonDoc["protocol"] = typeToString(state.protocol);
            jsonDoc["model"] = state.model;
            jsonDoc["power"] = state.power;
            jsonDoc["temperature"] = state.degrees;
            jsonDoc["mode"] = IRac::opmodeToString(state.mode);
            jsonDoc["fan"] = IRac::fanspeedToString(state.fanspeed);

            char jsonBuffer[256];
            serializeJson(jsonDoc, jsonBuffer);
            Serial.println(jsonBuffer);
            Serial.println();
            client.publish(mqtt_ac_report, jsonBuffer);
        }
        else {
            StaticJsonDocument<512> jsonDoc;
            JsonArray rawArray = jsonDoc.createNestedArray("rawData");

            for (uint16_t i = 1; i < results.rawlen; i++) {
                rawArray.add((int)results.rawbuf[i] * kRawTick);
            }
            
            char jsonBuffer[512];
            serializeJson(jsonDoc, jsonBuffer);
            Serial.println(jsonBuffer);
            Serial.println();
            client.publish(mqtt_raw_report, jsonBuffer);
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
