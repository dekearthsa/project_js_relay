/*
 Board Relay 4 Channel MCU ESP8266-12F Box Kit
 Create Date : 15-12-2023
 by Adthaphon Chaiwchan
*/

// include library to read and write from flash memory
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// define the number of bytes you want to access
#define EEPROM_SIZE 8 // Allocate The Memory Size Needed, I can read add. 0-7 = [FF] (new esp32) and add. 8 = (0)

#define INTERVAL_MESSAGE_1_2 1200 // 1.2 Sec.
#define INTERVAL_MESSAGE_10 10000 // 10 Sec.

#define swpb 5
#define LED_blu 4
#define LED_red 2
#define LED_grn 0
#define relay_1 16
#define relay_2 14
#define relay_3 12
#define relay_4 13

// Raspberry Pi4 [AP] ssid & password
const char *ssid = "Earth";
const char *password = "12345678Ab";
String mac_address;

// Add your MQTT Broker IP address
const char *mqtt_server = "192.168.155.160";
WiFiClient espClient;
PubSubClient client(espClient);
StaticJsonDocument<256> docL;

bool nowifi_mode;
bool wifi_mode;
bool mqttConnected;
bool eep_clear_first;
bool regis_pass = false;

int swpb_State = 0;

unsigned long time_1_2 = 0;
unsigned long time_10 = 0;

unsigned int timeOutwifi; // counter for wifi connection timeout
unsigned int timeOutmqtt; // counter for mqtt connection timeout
unsigned int timeOutReg;  // counter for no register respond from pi timeout

byte deviceTKclear[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
byte deviceTK[8];
char su_deviceTKchar[9]; // array length more than byte array /// char array length 1 1address higher than byte array (\r or 0x0d yes or on?)
String deviceTKstring;
byte blankCHK;

String callbackStr;

void setup()
{

    pinMode(relay_4, OUTPUT); // active high
    digitalWrite(relay_4, LOW);
    pinMode(relay_1, OUTPUT);
    digitalWrite(relay_1, LOW);
    pinMode(relay_2, OUTPUT);
    digitalWrite(relay_2, LOW);
    pinMode(relay_3, OUTPUT);
    digitalWrite(relay_3, LOW);

    pinMode(LED_blu, OUTPUT); // active high
    digitalWrite(LED_blu, LOW);
    pinMode(LED_red, OUTPUT);
    digitalWrite(LED_red, LOW);
    pinMode(LED_grn, OUTPUT);
    digitalWrite(LED_grn, LOW);

    pinMode(swpb, INPUT); // active low

    Serial.begin(115200);
    delay(100);

    // initialize EEPROM with predefined size
    EEPROM.begin(EEPROM_SIZE);

    // (0) Press switch SW.1 for clear 8 Bytes EEPROM <= FFh
    swpb_State = digitalRead(swpb);
    if (swpb_State == LOW && eep_clear_first == false)
    {
        eep_clear_first = true;
        digitalWrite(LED_grn, HIGH); // LED ON
        delay(100);
        // Write byte deviceTKclear[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}; to EEPROM add.0-7
        for (int i = 0; i < 8; i++)
        {
            EEPROM.write(i, deviceTKclear[i]);
            EEPROM.commit();
            delay(100);
        }
        delay(1000);
        digitalWrite(LED_grn, LOW); // LED OFF
    }

    // (1) check EEPROM blank ? (check byte[0-7] = FF it mean bank address)
    Serial.print("Device Token Key ID : ");
    for (int i = 0; i < 8; i++)
    {
        deviceTK[i] = EEPROM.read(i);
        su_deviceTKchar[i] = EEPROM.read(i);
        Serial.print(su_deviceTKchar[i]);
    }
    Serial.println();
    delay(1000);

    blankCHK = (deviceTK[7] & deviceTK[6] & deviceTK[5] & deviceTK[4] & deviceTK[3] & deviceTK[2] & deviceTK[1] & deviceTK[0]); // = FF for blank address
    Serial.print("EEPROM Check Blank Value : ");
    Serial.println(blankCHK, 16);
    delay(1000);

    if (blankCHK == 0xff)
    {
        regis_pass = false;
    }
    else
    {
        regis_pass = true;
        deviceTKstring = String(su_deviceTKchar);
    }

    // (2) check wifi (if ok check token(3) then goto no-wifi mode)
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(200);
        digitalWrite(LED_blu, HIGH); // LED ON
        delay(200);
        digitalWrite(LED_blu, LOW); // LED OFF
        timeOutwifi++;

        if (timeOutwifi > 40)
        {
            nowifi_mode = true;
            wifi_mode = false;
            timeOutwifi = 0;
            digitalWrite(LED_blu, LOW); // LED OFF
            ESP.restart();
            goto endsetup;
        }
    }

    // (3) wifi OK next step -> connect mqtt broker
    delay(100);
    Serial.println("Wifi Connection OK..");
    nowifi_mode = false;
    wifi_mode = true;
    mac_address = WiFi.macAddress();
    client.setServer(mqtt_server, 1883);
    client.setCallback(callback);

    // (4)

endsetup:
    Serial.println("End Setup Routine");
}

void reconnect()
{

    while (!client.connected())
    {
        Serial.print("Attempting MQTT connection...");

        // Create a random client ID
        String clientId = "ESP32Client-";
        clientId += String(random(0xffff), HEX);

        // Attempt to connect
        // if (client.connect(clientId.c_str(),mqttUser,mqttPassword)) {

        if (client.connect(clientId.c_str()))
        {
            Serial.println("MQTT is connected >>> Then client.subscribe register or command..");
            mqttConnected = true;

            if (regis_pass == true)
            {
                // Subscribe
                client.subscribe("v1/events/data/command/json");
            }

            if (regis_pass == false)
            {
                // Length (with one extra character for the null terminator)
                int str_len = mac_address.length() + 1;
                // Prepare the character array (the buffer)
                char char_maxadd_array[str_len];
                // Copy it over
                mac_address.toCharArray(char_maxadd_array, str_len);

                // "v1/events/device/register/{deviceID}/result"
                char regTopic[64];
                char regTopic1[] = "v1/events/device/register/";
                char regTopic2[] = "/result";

                strcpy(regTopic, regTopic1);
                strcat(regTopic, char_maxadd_array);
                strcat(regTopic, regTopic2);
                client.subscribe(regTopic); // "v1/events/device/register/{deviceID}/result"
                Serial.println(regTopic);
            }
        }
        else
        { // mqtt connection not OK!
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(1000);
            digitalWrite(LED_blu, HIGH); // LED ON
            delay(1000);
            digitalWrite(LED_blu, LOW); // LED OFF
            timeOutmqtt++;

            if (timeOutmqtt > 6)
            {
                timeOutmqtt = 0;
                nowifi_mode = true;
                wifi_mode = false;
                Serial.print("MQTT Connection not OK!..");
                Serial.println("Then Goto No-Wifi Mode");
                digitalWrite(LED_blu, LOW); // LED OFF
                break;
            }
        }
    }
}

void callback(char *topic, byte *message, unsigned int length)
{

    callbackStr = String(topic);
    Serial.print("Message arrived on topic: ");
    Serial.println(topic);
    Serial.print("Message: ");
    String messageTemp;

    for (int j = 0; j < length; j++)
    {
        messageTemp += (char)message[j];
    }

    if (regis_pass == true)
    {
        // ----------------------------------------------------------------------------------------------------------------------------
        if (String(topic) == "v1/events/data/command/json")
        {
            String json = messageTemp;
            // char json[] = "{\"deviceID\":\"34:85:18:05:99:78\",\"deviceType\":\"LFL\",\"token\":\"null\",\"data\":{\"ledStatus\":1}}";
            // char json[] = "{\"sensor\":\"gps\",\"time\":1351824120,\"data\":[48.756080,2.302038]}";
            // Deserialize the JSON document
            DeserializationError error = deserializeJson(docL, json);

            if (error)
            {
                Serial.print(F("deserializeJson() failed: "));
                Serial.println(error.f_str());
                return;
            }

            String deviceID = docL["deviceID"];
            String deviceType = docL["deviceType"];
            String token = docL["token"];
            int channel1 = docL["data"]["channel1"];
            int channel2 = docL["data"]["channel2"];
            int channel3 = docL["data"]["channel3"];
            int channel4 = docL["data"]["channel4"];

            // dimmer off lamp
            if (deviceID == mac_address && deviceType == "relay" && token == deviceTKstring)
            {
                if (channel1 == 1)
                {
                    digitalWrite(relay_1, HIGH); // relay_1 ON
                }
                else
                {
                    digitalWrite(relay_1, LOW); // relay_1 OFF
                }

                if (channel2 == 1)
                {
                    digitalWrite(relay_2, HIGH); // relay_2 ON
                }
                else
                {
                    digitalWrite(relay_2, LOW); // relay_2 OFF
                }

                if (channel3 == 1)
                {
                    digitalWrite(relay_3, HIGH); // relay_3 ON
                }
                else
                {
                    digitalWrite(relay_3, LOW); // relay_3 OFF
                }

                if (channel4 == 1)
                {
                    digitalWrite(relay_4, HIGH); // relay_4 ON
                }
                else
                {
                    digitalWrite(relay_4, LOW); // relay_4 OFF
                }
            }
        }
        // ----------------------------------------------------------------------------------------------------------------------------
    }

    if (regis_pass == false)
    {

        // Convert mac_address (String) TO char Array
        // Length (with one extra character for the null terminator)
        int str_len = mac_address.length() + 1;
        // Prepare the character array (the buffer)
        char char_maxadd_array[str_len];
        // Copy it over
        mac_address.toCharArray(char_maxadd_array, str_len);

        // "v1/events/device/register/{deviceID}/result"
        char regTopic[64];
        char regTopic1[] = "v1/events/device/register/";
        char regTopic2[] = "/result";

        strcpy(regTopic, regTopic1); // Array connect method
        strcat(regTopic, char_maxadd_array);
        strcat(regTopic, regTopic2);
        client.subscribe(regTopic); // "v1/events/device/register/{deviceID}/result"

        Serial.println(messageTemp);
        Serial.print("regTopic Compare : ");
        Serial.println(regTopic);

        // Serial.println(strlen(regTopic));

        if (callbackStr == String(regTopic))
        { // "v1/events/device/register/{deviceID}/result"
            String json = messageTemp;
            Serial.println("regTopic OK");
            DeserializationError error = deserializeJson(docL, json);

            if (error)
            {
                Serial.print(F("deserializeJson() failed: "));
                Serial.println(error.f_str());
                return;
            }
            /* Payload :
              {
              "deviceID" : Device UUID <string>,
              "deviceType" : Device type <string>,
              "result" : "success" || "failure" <string>,
              "token" : Token <string>
              }
             */

            String deviceID = docL["deviceID"];
            String deviceType = docL["deviceType"];
            String result = docL["result"];
            String token = docL["token"];

            // check result before
            if (result == "success")
            {
                if (deviceID == mac_address && deviceType == "relay")
                {
                    // ***** write token to EEPROM *****
                    Serial.println("Write Tokey Key TO EEPROM");
                    // Length (with one extra character for the null terminator)
                    int str_len = token.length() + 1;
                    // Prepare the character array (the buffer)
                    char token_char_array[str_len];
                    // Copy it over
                    token.toCharArray(token_char_array, str_len);

                    // Write char array (char token_char_array[str_len];) to EEPROM add.0-7
                    for (int i = 0; i < str_len; i++)
                    {
                        EEPROM.write(i, token_char_array[i]);
                        EEPROM.commit();
                        delay(100);
                    }

                    // read EEPROM check Token Key before
                    char deviceTKchar[str_len];
                    for (int i = 0; i < str_len; i++)
                    {
                        deviceTKchar[i] = EEPROM.read(i);
                    }

                    deviceTKstring = String(deviceTKchar);

                    for (int i = 0; i < str_len; i++)
                    {
                        Serial.print(deviceTKchar[i]);
                    }
                    Serial.println();

                    // regis_pass = true;                   // ***** register token key OK *****

                    for (int i = 0; i < 10; i++)
                    {
                        analogWrite(LED_grn, 0); // G LED
                        delay(100);
                        analogWrite(LED_grn, 255);
                        delay(100);
                    }
                    analogWrite(LED_grn, 0);
                    delay(3000);
                    analogWrite(LED_grn, 255);

                    Serial.println("Restart ESP after write Token Key TO EEPROM Finish bye...");
                    delay(3000);
                    ESP.restart(); // ==> Restart ESP
                }
            }
            else if (result == "failure")
            {
                // *** must be *** alarm blink red led and goto no-wifi mode
                Serial.println("Pi Public Send Register result = failure..");

                nowifi_mode = true;
                wifi_mode = false;
            }
        }
    }

} // end void callback()

void pub_reg_request()
{   // [PUB] MQTT Registration request
    /*
      Topic :
      v1/events/device/register
    {
     "deviceID" : Device UUID <string>,
     "deviceType" : Device type <string>,
     "batteryLevel" : Battery level <int>
    }
    */

    char buffer[256];

    // Initializing the JsonDocument (initializing the memory).
    StaticJsonDocument<256> doc;
    // Creating the JsonObject (coverting the JsonDocument).
    JsonObject Object = doc.to<JsonObject>();

    // Inserting the Key value pair in JsonObejct.
    Object["deviceID"] = mac_address;
    Object["deviceType"] = "relay";

    // Serializing the JsonObject on std::cout stream. // For what?
    /*
    serializeJsonPretty(Object, Serial);
    serializeJson(Object, Serial);
    Serial.println();
    */

    serializeJson(Object, buffer);
    client.publish("v1/events/device/register", buffer);

    Serial.print("Send Public Register Request >> ");
    Serial.println("Try to Register...");
}

void loop()
{

    // ================================================= mcu scan time loop ================================================

    if (wifi_mode == true && nowifi_mode == false)
    { // ******************************* wifi mode

        if (!client.connected())
        {
            reconnect();
        }
        client.loop();
        Serial.println("MQTT Client.loop (in void loop()) => OK");

        if (mqttConnected == true)
        { // check token key in eeprom
            if (regis_pass == false)
            {
                timeOutReg++;
                delay(6000);
                digitalWrite(LED_grn, HIGH); // G LED ON
                delay(2000);
                digitalWrite(LED_grn, LOW); // G LED OFF
                // [PUB] MQTT Registration request
                pub_reg_request(); // MQTT Pub for Token Registry

                // if no respond from pi then goto no-wifi mode
                if (timeOutReg > 10)
                {
                    timeOutReg = 0;
                    nowifi_mode = true;
                    wifi_mode = false;
                    Serial.println("No Register Respond From Pi..");
                    Serial.println("Goto No-Wifi Mode");
                    ESP.restart();
                }
            }
            else if (regis_pass == true)
            {

                // regis_pass (It can happen from 2 cases.)
                // 1. The token key was detected in the first place.
                // 2. Successfully registered token key.
                // have token key, read token to char => string
                delay(100); // ########### for test check loop in void loop() {regis_pass == true}
                Serial.println("in void loop regis_pass == true");
            }
        }
    }

    // ================================================= 1.2 sec. time program service ======================================
    if (millis() - time_1_2 > INTERVAL_MESSAGE_1_2)
    {
        time_1_2 = millis();
        // Serial.println("print every 1 sec.");

        if (wifi_mode == true && nowifi_mode == false)
        {
            Serial.println("wifi mode");
        }
        else if (wifi_mode == false && nowifi_mode == true)
        {
            Serial.println("no wifi mode");
        }
    }

    /*
        swpb_State = digitalRead(swpb);
        if (swpb_State == LOW){
          Serial.println("swpb is press");
        } else {
          Serial.println("swpb is relese");
        }
    */
}
