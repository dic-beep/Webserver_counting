#include <SPI.h>
#include <Ethernet.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>
#include <SmoothThermistor.h>

#define RX_PIN 44
#define TX_PIN 43

HardwareSerial GM66(1);
String trimmedBarcode = "";
Preferences preferences;

SmoothThermistor smoothThermistor(4,              // the analog pin to read from
                                  ADC_SIZE_12_BIT, // the ADC size
                                  10000,           // the nominal resistance
                                  10000,           // the series resistance
                                  3950,            // the beta coefficient of the thermistor
                                  25,              // the temperature for nominal resistance
                                  10);             // the number of samples to take for each measurement
#define FAN_PIN 3

byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

IPAddress staticIP(10, 26, 101, 197);
IPAddress gateway(10, 26, 101, 190);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);

EthernetServer server(80);
float temp = 0.0;

// ================== Load Configuration ===================
bool loadStaticNetwork() {
  preferences.begin("network", true);
  bool hasIP = preferences.getBool("hasIP", false);
  if (!hasIP) {
    preferences.end();
    Serial.println("No saved IP config. Using default static IP.");
    return false;
  }

  byte ip[4], gw[4], sn[4];
  preferences.getBytes("ip", ip, 4);
  preferences.getBytes("gateway", gw, 4);
  preferences.getBytes("subnet", sn, 4);

  staticIP = IPAddress(ip[0], ip[1], ip[2], ip[3]);
  gateway = IPAddress(gw[0], gw[1], gw[2], gw[3]);
  subnet = IPAddress(sn[0], sn[1], sn[2], sn[3]);

  preferences.end();
  Serial.print("Loaded IP from preferences: ");
  Serial.println(staticIP);
  return true;
}

void saveStaticNetwork(IPAddress newIP, IPAddress newGateway, IPAddress newSubnet) {
  preferences.begin("network", false);
  byte ip[4] = { newIP[0], newIP[1], newIP[2], newIP[3] };
  byte gateway[4] = { newGateway[0], newGateway[1], newGateway[2], newGateway[3] };
  byte subnet[4] = { newSubnet[0], newSubnet[1], newSubnet[2], newSubnet[3] };
  preferences.putBytes("ip", ip, 4);
  preferences.putBytes("gateway", gateway, 4);
  preferences.putBytes("subnet", subnet, 4);
  preferences.putBool("hasIP", true);  // Simpan flag
  preferences.end();
  Serial.println("Success Change Network");
}

// ================== Barcode Reader ===================
void readGM66() {
  if (GM66.available()) {
    String barcode = "";
    unsigned long timeout = millis();
    while (millis() - timeout < 200) {
      if (GM66.available()) {
        char c = GM66.read();
        if (c >= 32 && c <= 126) {
          barcode += c;
        }
        timeout = millis();
      }
    }
    barcode.trim();
    if (barcode.length() > 0) {
      trimmedBarcode = barcode.substring(0, barcode.length() - 3);
      Serial.print("Barcode: ");
      Serial.println(trimmedBarcode);
    }
  }
}

// ================== Web Handling ===================
void handleClient(EthernetClient& client, const String& req) {
  if (req.indexOf("GET /qrcode") >= 0) {
    sendJsonResponse(client);
  } else if (req.indexOf("GET /config") >= 0) {
    sendConfigPage(client);
  } 

    else if (req.indexOf("POST /setip") >= 0) {
    String body = "";
    while (client.available()) body += (char)client.read();
    int ipIndex = body.indexOf("ip=");
    int gatewayIndex = body.indexOf("gateway=");
    int subnetIndex = body.indexOf("subnet=");
    if (ipIndex >= 0 && gatewayIndex >= 0 && subnetIndex >= 0) {
      String ipStr = body.substring((ipIndex + 3), body.indexOf("&", ipIndex));
      String gwStr = body.substring((gatewayIndex + 8), body.indexOf("&", gatewayIndex));
      String snStr = body.substring(subnetIndex + 7);

      ipStr.replace("%2E", ".");
      gwStr.replace("%2E", ".");
      snStr.replace("%2E", ".");

      IPAddress newIP, newGW, newSN;
      if (newIP.fromString(ipStr) && newGW.fromString(gwStr) && newSN.fromString(snStr)) {
        saveStaticNetwork(newIP, newGW, newSN);
        client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\nNetwork saved. Please Restart ESP!!.");
        ESP.restart();
        return;
      }
    }
    client.println("HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\nInvalid IP");
  } 
  else {
    sendHomePage(client);
  }
}

void sendJsonResponse(EthernetClient& client) {
  JsonDocument doc;
  doc["kode_barang"] = trimmedBarcode;
  doc["ip_ethernet"] = Ethernet.localIP().toString();
  doc["temperature"] = smoothThermistor.temperature();

  String output;
  serializeJson(doc, output);

  client.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n" + output);
}

void sendHomePage(EthernetClient& client) {
  client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
  client.println("<html><body><h1>ESP32 Barcode</h1><ul>");
  client.println("<li><a href='/qrcode'>QR Code</a></li>");
  client.println("<li><a href='/config'>Set IP</a></li>");
  client.println("<li><a href='/wifi'>WiFi Config</a></li>");
  client.println("</ul></body></html>");
}

void sendConfigPage(EthernetClient& client) {
  client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
  client.println("<form method='POST' action='/setip'> IP Baru: <input name='ip'> <br>");
  client.println("Gateway Baru: <input name='gateway'> <br>");
  client.println("Subnet Baru: <input name='subnet'> <br>");
  client.println("<input type='submit'> </form>");
}

// ================== Setup & Loop ===================
void setup() {
  Serial.begin(115200);
  GM66.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  analogWrite(FAN_PIN, 0);
  delay(500);
  Ethernet.init(5);
  smoothThermistor.useAREF(true);

  if (!loadStaticNetwork()) {
    // Jika tidak ada IP tersimpan, pakai IP default
    staticIP = IPAddress(10, 26, 101, 197);
    gateway = IPAddress(10, 26, 101, 190);
    subnet = IPAddress(255, 255, 255, 0);
  }

  Ethernet.begin(mac, staticIP, dns, gateway, subnet);
  Serial.print("Ethernet IP: ");
  Serial.println(Ethernet.localIP());

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("Ethernet shield was not found.");
  } else if (Ethernet.linkStatus() == LinkOFF) {
    Serial.println("Ethernet cable is not connected.");
  }

  server.begin();
}

void loop() {
  temp = smoothThermistor.temperature();
  Serial.print("Suhu: ");
  Serial.println(temp);
  delay(100);
  if (temp >= 50 ){
    analogWrite(FAN_PIN, 255);
    Serial.println("FAN ON");
    delay(1000);
  }else if(temp <= 30){
    analogWrite(FAN_PIN, 0);
    Serial.println("FAN OFF");
    delay(1000);
  }
  readGM66();

  EthernetClient client = server.available();
  if (client) {
    String req = "";
    while (client.connected()) {
      Serial.println("Berhasil Terhubung..");
      if (client.available()) {
        char c = client.read();
        req += c;
        if (req.endsWith("\r\n\r\n")) break;
      }
    }
    Serial.println("REQ: " + req);
    handleClient(client, req);
    client.stop();
  }
}