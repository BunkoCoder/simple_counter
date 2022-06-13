#include <Arduino.h>
#include <EEPROM.h>
#include <HTTPClient.h>
#include <CayenneMQTTESP32.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>     //Wifi library
#include <esp_wpa2.h> //wpa2 library for connections to Enterprise networks
#include <esp_wifi.h>

#define DEBUG true

#define CILINDER_PIN 4
#define DEBUG_LIGHT 2
#define ONE_WIRE_BUS_PIN 27
#define TEMP_SENS_VCC 13

#define CILINDER_TIMEOUT 180000
#define CILINDER_DEBOUNCE 2000
#define INFO_TIME 7200000
#define CAYENNE_UPDATE_TIME 30000
#define DOLZINA_POTI 0.7
#define JAVI_DOSEZENO_POT 100000

#define VIRTUAL_CHANNEL 5

#define EAP_ANONYMOUS_IDENTITY "anonymous@tuke.sk" // anonymous@example.com, or you can use also nickname@example.com
#define EAP_IDENTITY "username@tuke.sk"            // nickname@example.com, at some organizations should work nickname only without realm, but it is not recommended
#define EAP_PASSWORD "password"                    // password for eduroam account

const char *ssid = "eduroam"; // eduroam SSID

char username[] = "da4caf40-e4f2-11ec-a681-73c9540e1265";
char password[] = "ac2330f16dab84423205e47e24e2b676ba49b1b7";
char clientID[] = "ed222820-e4f2-11ec-9f5b-45181495093e";

String PHONE_NUM = "+38630238798";
String APIKEY = "978395";

OneWire oneWire(ONE_WIRE_BUS_PIN);
DallasTemperature sensors(&oneWire);

volatile uint32_t pot;
volatile unsigned long lastKlick;
volatile bool dead = false;

float temperature;
bool done = false;
int valuePrej;
unsigned long info, cayinfo;

String url;

void IRAM_ATTR ISR_CILINDER();

uint32_t getPOT(int startA);
void setPOT(int startA);
float getTemp();
void message_to_whatsapp(String message);
bool postData();
String urlencode(String str);
void EnduroamWiFi();
void I_WILL_CONNECT();

void setup()
{
  if (DEBUG)
    Serial.begin(9600);

  pinMode(TEMP_SENS_VCC, OUTPUT);
  digitalWrite(TEMP_SENS_VCC, HIGH);

  EEPROM.begin((sizeof(uint32_t) / sizeof(uint8_t)));
  pot = getPOT(0);
  attachInterrupt(CILINDER_PIN, ISR_CILINDER, RISING);
  sensors.begin();
  temperature = getTemp();
  EnduroamWiFi();
  if (DEBUG)
    Serial.println(WiFi.localIP());

  Cayenne.begin(username, password, clientID);
}

void loop()
{
  unsigned long temptime = millis();
  if (!dead && temptime - lastKlick >= CILINDER_TIMEOUT)
  {
    message_to_whatsapp("! USTAVLJEN ! \n DOLŽINA POTI: " + (String)(pot * DOLZINA_POTI) + " m \n TEMPERATURA: " + (String)temperature + " °C");
    dead = true;
  }
  if (!dead && temptime - info >= INFO_TIME)
  {
    message_to_whatsapp("DELAM \n DOLŽINA POTI: " + (String)(pot * DOLZINA_POTI) + " m \n TEMPERATURA: " + (String)temperature + " °C");
    info += INFO_TIME;
  }
  else if (!done && pot * DOLZINA_POTI >= JAVI_DOSEZENO_POT)
  {
    message_to_whatsapp("ZASTAVLJENA POT OPRAVLJENA \n DOLŽINA POTI:" + (String)(pot * DOLZINA_POTI) + " m \n TEMPERATURA: " + (String)temperature + " °C");
    done = true;
  }
  if (temptime - cayinfo >= CAYENNE_UPDATE_TIME)
  {
    temperature = getTemp();
    if (DEBUG)
      Serial.println(temperature);
    if (!WiFi.isConnected())
      I_WILL_CONNECT();
    Cayenne.loop();
    cayinfo += CAYENNE_UPDATE_TIME;
  }
}

void I_WILL_CONNECT()
{
  EnduroamWiFi();
  unsigned int stevec = 0;
  while (!WiFi.isConnected())
  {
    if (stevec >= 40)
    {
      if (DEBUG)
        Serial.println("RESTARTING...");
      setPOT(0);
      esp_restart();
    }
    stevec++;
    delay(500);
  }
  Cayenne.connect();
}

CAYENNE_OUT_DEFAULT()
{
  Cayenne.virtualWrite(0, pot * DOLZINA_POTI);
  Cayenne.virtualWrite(1, temperature);
  Cayenne.virtualWrite(2, !dead, TYPE_DIGITAL_SENSOR, UNIT_DIGITAL);
}

CAYENNE_IN(VIRTUAL_CHANNEL)
{
  int value = getValue.asInt();
  if (valuePrej != value)
  {
    pot = 0;
    message_to_whatsapp("asdasdasdas" + (String)(pot * DOLZINA_POTI) + " m \n IN TI ME ZRESETIRAŠ :|");
  }
}

void IRAM_ATTR ISR_CILINDER()
{
  if (millis() - lastKlick >= CILINDER_DEBOUNCE)
  {
    pot++;
    lastKlick = millis();
    dead = false;
  }
}

uint32_t getPOT(int startA)
{
  uint32_t value = 0;
  for (int i = startA; i < startA + (sizeof(uint32_t) / sizeof(uint8_t)); i++)
  {
    uint8_t val = EEPROM.read(startA + i);
    value = value | (val << 8 * (i - startA));
  }
  return value;
}

void setPOT(int startA)
{
  for (int i = 0; i < sizeof(uint32_t) / sizeof(uint8_t); i++)
  {
    uint8_t val = pot << (8 * i);
    EEPROM.write(startA + i, val);
  }
  EEPROM.commit();
}

float getTemp()
{
  sensors.requestTemperatures();
  float temp = sensors.getTempCByIndex(0);
  return temp;
}

void message_to_whatsapp(String message) // user define function to send meassage to WhatsApp app
{
  if (DEBUG)
    Serial.println(message);
  setPOT(0);
  url = "https://api.callmebot.com/whatsapp.php?phone=" + PHONE_NUM + "&apikey=" + APIKEY + "&text=" + urlencode(message);
  for (int i = 0; i < 3; i++)
  {
    if (postData())
    {
      if (DEBUG)
        Serial.println("JE POSLALU");
      break;
    }
    else
    {
      if (DEBUG)
        Serial.println("NI POSLALU");
      if (!WiFi.isConnected())
        I_WILL_CONNECT();
    } // calling postData to run the above-generated url once so that you will receive a message.
  }
}

bool postData() // userDefine function used to call api(POST data)
{
  int httpCode;    // variable used to get the responce http code after calling api
  HTTPClient http; // Declare object of class HTTPClient
  http.begin(url); // begin the HTTPClient object with generated url
  httpCode = http.POST(url);
  http.end(); // After calling API end the HTTP client object.

  if (httpCode == 200)
    return true;
  else
    return false;
}

String urlencode(String str) // Function used for encoding the url
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++)
  {
    c = str.charAt(i);
    if (c == ' ')
    {
      encodedString += '+';
    }
    else if (isalnum(c))
    {
      encodedString += c;
    }
    else
    {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9)
      {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9)
      {
        code0 = c - 10 + 'A';
      }
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
      // encodedString+=code2;
    }
    yield();
  }
  return encodedString;
}

void EnduroamWiFi()
{
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  esp_wifi_sta_wpa2_ent_set_identity((uint8_t *)EAP_ANONYMOUS_IDENTITY, strlen(EAP_ANONYMOUS_IDENTITY));
  esp_wifi_sta_wpa2_ent_set_username((uint8_t *)EAP_IDENTITY, strlen(EAP_IDENTITY));
  esp_wifi_sta_wpa2_ent_set_password((uint8_t *)EAP_PASSWORD, strlen(EAP_PASSWORD));
  esp_wpa2_config_t config = WPA2_CONFIG_INIT_DEFAULT();
  esp_wifi_sta_wpa2_ent_enable(&config);
  WiFi.begin(ssid);
}