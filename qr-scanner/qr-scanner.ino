#include <SPI.h>
#include <Ethernet.h>
#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>

#define RX_PIN 44
#define TX_PIN 43

HardwareSerial GM66(1);
String trimmedBarcode = "";

Preferences preferences;

byte mac[] = { 0x32, 0xAE, 0xA4, 0x07, 0x0D, 0x66 };

IPAddress staticIP(10, 26, 102, 197);
IPAddress gateway(10, 26, 102, 190);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8);

EthernetServer server(80);

bool isAPMode = false;

// ================== Load Configuration ===================
void loadStaticNetwork() {
  preferences.begin("network", true);
  byte ip[4], gw[4], sn[4];
  preferences.getBytes("ip", ip, 4);
  preferences.getBytes("gateway", gw, 4);
  preferences.getBytes("subnet", sn, 4);
  staticIP = IPAddress(ip[0], ip[1], ip[2], ip[3]);
  gateway = IPAddress(gw[0], gw[1], gw[2], gw[3]);
  subnet = IPAddress(sn[0], sn[1], sn[2], sn[3]);
  preferences.end();
}

void saveStaticNetwork(IPAddress newIP, IPAddress newGateway, IPAddress newSubnet) {
  preferences.begin("network", false);
  byte ip[4] = { newIP[0], newIP[1], newIP[2], newIP[3] };
  byte gateway[4] = { newGateway[0], newGateway[1], newGateway[2], newGateway[3] };
  byte subnet[4] = { newSubnet[0], newSubnet[1], newSubnet[2], newSubnet[3] };
  preferences.putBytes("ip", ip, 4);
  preferences.putBytes("gateway", gateway, 4);
  preferences.putBytes("subnet", subnet, 4);
  // preferences.putBool("hasIP", true);
  preferences.end();
}

void loadWiFi() {
  preferences.begin("wifi", true);
  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  preferences.end();

  if (ssid.length() > 0) {
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);

    // Set IP static sebelum WiFi.begin()
    if (!WiFi.config(staticIP, gateway, subnet, dns)) {
      Serial.println("STA Failed to configure");
    }

    WiFi.begin(ssid.c_str(), pass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("WiFi Connected: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("WiFi failed. Starting AP mode...");
      startAPMode();
    }
  } else {
    startAPMode();
  }
}

void startAPMode() {
  WiFi.softAP("ESP32-Setup", "12345678");
  isAPMode = true;
  Serial.println("Access Point started. IP: 192.168.4.1");
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
  } else if (req.indexOf("GET /wifi") >= 0) {
    sendWiFiConfigPage(client);
  } else if (req.indexOf("POST /setip") >= 0) {
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
        return;
      }
    }
    client.println("HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\nInvalid IP");
  } else if (req.indexOf("POST /setwifi") >= 0) {
    String body = "";
    while (client.available()) body += (char)client.read();

    String ssid = "", pass = "";
    int ssidIndex = body.indexOf("ssid=");
    int passIndex = body.indexOf("pass=");
    if (ssidIndex >= 0 && passIndex >= 0) {
      ssid = body.substring(ssidIndex + 5, body.indexOf("&", ssidIndex));
      pass = body.substring(passIndex + 5);
      ssid.replace("+", " ");
      pass.replace("+", " ");
      preferences.begin("wifi", false);
      preferences.putString("ssid", ssid);
      preferences.putString("pass", pass);
      preferences.end();
      client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\nWiFi saved. Restart ESP.");
      return;
    }
    client.println("HTTP/1.1 400 Bad Request\r\nContent-Type: text/html\r\n\r\nInvalid WiFi data");
  } else {
    sendHomePage(client);
  }
}

void sendJsonResponse(EthernetClient& client) {
  JsonDocument doc;
  doc["kode_barang"] = trimmedBarcode;
  doc["ip_ethernet"] = Ethernet.localIP().toString();
  doc["ip_wifi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "Not connected";

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

void sendWiFiConfigPage(EthernetClient& client) {
  client.println("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
  client.println("<form method='POST' action='/setwifi'>");
  client.println("SSID: <input name='ssid'><br>Password: <input name='pass' type='password'><br>");
  client.println("<input type='submit' value='Simpan WiFi'></form>");
}

// ================== Setup & Loop ===================
void setup() {
  Serial.begin(115200);
  GM66.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);

  loadStaticNetwork();
  Ethernet.init(5);
  Ethernet.begin(mac, staticIP, dns, gateway, subnet);
  Serial.print("Ethernet IP: ");
  Serial.println(Ethernet.localIP());

  loadWiFi();
  server.begin();
}

void loop() {
  readGM66();

  EthernetClient client = server.available();
  if (client) {
    String req = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        req += c;
        if (req.endsWith("\r\n\r\n")) break;
      }
    }
    handleClient(client, req);
    client.stop();
  }
}
