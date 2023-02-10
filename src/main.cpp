#include <Arduino.h>
#include <ArduinoJson.h>
#include <AsyncElegantOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <NTP.h>
#include <PubSubClient.h>
#include <U8g2lib.h>
#include <WiFiClient.h>

WiFiUDP wifiUdp;
NTP ntp(wifiUdp);

// Configure your settings here
auto wifi_ssid = ""; // Your Wi-Fi SSID
auto wifi_pass = ""; // Your Wi-Fi password

// Client prefix
// Used for connecting to the MQTT server as well as for mDNS
// For example a prefix of "esp8266-clock-" with a MAC of 01:02:03:04:05:06
// will result in the client logging in as esp8266-clock-040506 to MQTT
// and being accessible under esp8266-clock-040506.local
auto client_id_prefix = "esp8266-clock-";
const auto client_id_prefix_length = 14; // Length of the above prefix without the zero-byte.

auto mqtt_ip = ""; // Your MQTT server IP
const auto mqtt_port = 1883; // Your MQTT server port
auto mqtt_user = ""; // Your MQTT server username
auto mqtt_pass = ""; // Your MQTT server password

// If you would like to connect to a device that does not run Tasmota, see README.md
auto mqtt_topic = "tele/tasmota_000000/SENSOR"; // The MQTT topic to subscribe to
auto mqtt_sensor = ""; // Sensor name in Tasmota

const auto matrix_brightness = 1; // Brightness of the LEDs

auto ntp_set_timezone() -> void // Set your timezone here. Currently
{
    ntp.ruleDST("CEST", Last, Sun, Mar, 2, 120);
    ntp.ruleSTD("CET", Last, Sun, Oct, 3, 60);
}
// End of settings

char client_id[client_id_prefix_length + 7] = {};
char mac_char[7] = {};

int temperature;
String temperature_unit;
bool got_temperature = false;

struct Time
{
    int hour = 0;
    int minute = 0;
    int second = 0;
};

Time current_time;
Time last_update_time;

int days_running = 0;
int last_sync_day = 0;

WiFiClient client;
PubSubClient mqtt_client(mqtt_ip, mqtt_port, client);

AsyncWebServer server(80);

U8G2_MAX7219_32X8_F_4W_HW_SPI matrix(U8G2_R2, D8, U8X8_PIN_NONE);

auto draw_time() -> void; // Draws the current time on the matrix
auto draw_temperature() -> void; // Draws the current temperature on the matrix
auto connected_to_wifi() -> void; // Fires every time Wi-Fi is connected - connects to MQTT and sets up mDNS
auto mqtt_setup() -> void; // Connects to MQTT
auto mqtt_callback(char *, byte *, unsigned int) -> void; // Gets called every time a new temperature is received
auto ntp_sync_time() -> void; // Synchronize the time with NTP

auto setup() -> void
{
    uint8_t mac[6] = {0, 0, 0, 0, 0, 0}; // Prepare the client ID
    WiFi.macAddress(mac);
    snprintf(mac_char, 7, "%02x%02x%02x", mac[3], mac[4], mac[5]);
    snprintf(client_id, client_id_prefix_length + 6, "%s%s", client_id_prefix, mac_char);

    matrix.begin(); // Set up the matrix

    matrix.setFont(u8g2_font_6x12_mf);
    matrix.setFontRefHeightExtendedText();
    matrix.setContrast(matrix_brightness);
    matrix.setDrawColor(1);
    matrix.setFontDirection(0);
    matrix.setFontPosTop();

    matrix.clearBuffer();
    matrix.drawStr(2, -2, "WI-FI");
    matrix.sendBuffer();

    WiFi.begin(wifi_ssid, wifi_pass); // Connect to Wi-Fi
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        matrix.clearBuffer();
        matrix.sendBuffer();
        delay(500);
        matrix.drawStr(2, -2, "WI-FI");
        matrix.sendBuffer();
    }
    connected_to_wifi();
    WiFi.onStationModeGotIP(
            [](auto)
            {
                connected_to_wifi();
            }
    );

    ntp_set_timezone();
    ntp_sync_time();

    draw_time();

    LittleFS.begin();
    // If filesystem was flashed, send a page with basic preview of values, otherwise redirect users to /update
    if (LittleFS.exists("index.html") && LittleFS.exists("style.css.gz"))
    {
        server.on("/", [](AsyncWebServerRequest *request)
        {
            request->send(LittleFS, "/index.html", "text/html", false, [](auto &var) -> String
            {
                if (var == "client_id")
                {
                    return {client_id};
                }
                else if (var == "current_time")
                {
                    String time;
                    if (current_time.hour < 10)
                    {
                        time += "0";
                    }
                    time += current_time.hour;
                    if (current_time.minute < 10)
                    {
                        time += "0";
                    }
                    time += current_time.minute;
                    if (current_time.second < 10)
                    {
                        time += "0";
                    }
                    time += current_time.second;
                    return time;
                }
                else if (var == "days_running")
                {
                    return String(days_running);
                }
                else if (var == "temperature")
                {
                    return String(temperature);
                }
                else if (var == "last_update_time")
                {
                    if (got_temperature)
                    {
                        String time;
                        if (last_update_time.hour < 10)
                        {
                            time += "0";
                        }
                        time += last_update_time.hour;
                        if (last_update_time.minute < 10)
                        {
                            time += "0";
                        }
                        time += last_update_time.minute;
                        if (last_update_time.second < 10)
                        {
                            time += "0";
                        }
                        time += last_update_time.second;
                        return time;
                    }
                    else
                    {
                        return {"Never"};
                    }
                }
                return {};
            });
        });
        server.on("/style.css", [](AsyncWebServerRequest *request)
        {
            request->send(LittleFS, "/style.css", "text/css");
        });
    }
    else
    {
        server.on("/", [](AsyncWebServerRequest *request)
        {
            AsyncWebServerResponse *response = request->beginResponse(200, "text/plain",
                                                                      "FileSystem missing, redirecting to /update");
            response->addHeader("Refresh", "5; url=/update");
            request->send(response);
        });
    }

    AsyncElegantOTA.begin(&server);
    server.begin();
}

auto loop() -> void
{
    MDNS.update();
    mqtt_client.loop();
    static unsigned long long previous_millis = 0;

    unsigned long current_millis = millis();

    if (current_millis - previous_millis >= 1000)
    {
        previous_millis = current_millis;
        current_time.second++;
        if (current_time.second == 60)
        {
            current_time.second = 0;
            current_time.minute++;
            if (WiFi.status() != WL_CONNECTED)
            {
                WiFi.begin(wifi_ssid, wifi_pass);
            }
            if (mqtt_client.state() != MQTT_CONNECTED)
            {
                mqtt_setup();
            }
            if (current_time.minute == 60)
            {
                current_time.minute = 0;
                current_time.hour++;
                if (current_time.hour == 24)
                {
                    current_time.hour = 0;
                    days_running++;
                    if (days_running >= 30)
                    {
                        EspClass::restart();
                    }
                }
                if (current_time.hour == 4 && last_sync_day != days_running)
                {
                    last_sync_day = days_running;
                    ntp_sync_time();
                }
            }
        }
        if (current_time.second % 5 == 0)
        {
            draw_time();
        }
        if (current_time.second % 5 == 3 && got_temperature)
        {
            draw_temperature();
        }
    }
}

auto draw_time() -> void
{
    char hour_draw[3] = { // Convert hour and minute to strings
            static_cast<char>(current_time.hour / 10 + '0'),
            static_cast<char>(current_time.hour % 10 + '0'),
            0
    };
    char minute_draw[3] = {
            static_cast<char>(current_time.minute / 10 + '0'),
            static_cast<char>(current_time.minute % 10 + '0'),
            0
    };
    matrix.clearBuffer();
    matrix.drawStr(2, -2, hour_draw);
    matrix.drawStr(14, -3, ":");
    matrix.drawStr(19, -2, minute_draw);
    matrix.sendBuffer();
}

auto draw_temperature() -> void
{
    matrix.clearBuffer();
    char temperature_char[4];
    itoa(temperature, temperature_char, 10);
    char temperature_draw[6];
    snprintf(temperature_draw, 6, "%s %s", temperature_char, temperature_unit.c_str());
    switch (strlen(temperature_draw)) // Draw the string centered, position depends on the number of characters
    {
        case 3:
        {
            matrix.drawStr(8, -2, temperature_draw);
            break;
        }
        case 4:
        {
            matrix.drawStr(5, -2, temperature_draw);
            break;
        }
        default:
        {
            matrix.drawStr(2, -2, temperature_draw);
            break;
        }
    }
    matrix.sendBuffer();
}

auto connected_to_wifi() -> void
{
    MDNS.begin(client_id);
    mqtt_setup();
}

auto mqtt_setup() -> void
{
    mqtt_client.connect(client_id, mqtt_user, mqtt_pass);
    mqtt_client.setBufferSize(512);
    mqtt_client.subscribe(mqtt_topic);
    mqtt_client.setCallback(mqtt_callback);
}

auto mqtt_callback(char *, byte *payload, unsigned int) -> void
{
    String json = String(reinterpret_cast<char *>(payload));

    DynamicJsonDocument doc(128);

    StaticJsonDocument<64> filter;
    filter[mqtt_sensor]["Temperature"] = true;
    filter["TempUnit"] = true;

    deserializeJson(doc, json, DeserializationOption::Filter(filter));

    float temperature_f = doc[mqtt_sensor]["Temperature"];
    temperature = lroundf(temperature_f);

    temperature_unit = doc["TempUnit"].as<String>();

    got_temperature = true;
    last_update_time.hour = current_time.hour;
    last_update_time.minute = current_time.minute;
    last_update_time.second = current_time.second;
}

auto ntp_sync_time() -> void
{
    ntp.begin();
    ntp.update();
    current_time.hour = static_cast<unsigned char>(ntp.hours());
    current_time.minute = static_cast<unsigned char>(ntp.minutes());
    current_time.second = static_cast<unsigned char>(ntp.seconds());
    ntp.stop();
}