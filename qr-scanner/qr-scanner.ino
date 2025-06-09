#include <SPI.h>
#include <Ethernet.h>
#include <ArduinoJson.h>
#include <HardwareSerial.h>

#define RX_PIN 44
#define TX_PIN 43

HardwareSerial GM66(1);
String trimmedBarcode = "";

// Konfigurasi MAC dan IP static untuk W5500
byte mac[] = { 0x32, 0xAE, 0xA4, 0x07, 0x0D, 0x66 }; // MAC custom
IPAddress ip(10, 26, 102, 197);
IPAddress gateway(10, 26, 102, 190);
IPAddress subnet(255, 255, 255, 0);
IPAddress dns(8, 8, 8, 8); 

EthernetServer server(80);

// Debug MAC
void printMacAddress() {
  Serial.print("Current MAC Address: ");
  for (int i = 0; i < 6; i++) {
    Serial.print(mac[i], HEX);
    if (i < 5) Serial.print(":");
  }
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Inisialisasi serial GM66 (pastikan baudrate sesuai)
  GM66.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);

  // Ethernet init
  Ethernet.init(5); // CS pin W5500 ke GPIO 5
  Ethernet.begin(mac, ip, dns, gateway, subnet); 
  delay(1000);

  printMacAddress();

  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("W5500 tidak terdeteksi.");
    while (true);
  }

  Serial.print("Server is at ");
  Serial.println(Ethernet.localIP());

  server.begin();
}

void loop() {
  readGM66();

  EthernetClient client = server.available();
  if (client) {
    Serial.println("Client connected");
    String req = "";

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        req += c;

        if (c == '\n') {
          if (req.indexOf("GET /qrcode") >= 0) {
            sendJsonResponse(client);
          } else {
            sendDefaultPage(client);
          }
          break;
        }
      }
    }

    delay(1);
    client.stop();
    Serial.println("Client disconnected");
  }
}

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
        timeout = millis(); // Reset timeout setiap ada input
      }
    }

    // Hilangkan karakter \r atau \n jika ada
    barcode.trim();

    if (barcode.length() > 0) {
      Serial.print("Scanned Barcode: ");
      Serial.println(barcode);
      trimmedBarcode = barcode.substring(0, barcode.length() - 3);
      Serial.print("Scanned Barcode (GM66): ");
      Serial.println(trimmedBarcode);
    }
  }
}

void sendJsonResponse(EthernetClient& client) {
  JsonDocument doc;
  doc["kode_barang"] = trimmedBarcode;
  doc["ip"] = Ethernet.localIP().toString();

  String jsonOutput;
  serializeJson(doc, jsonOutput);

  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: application/json");
  client.println("Connection: close");
  client.println();
  client.println(jsonOutput);
}

void sendDefaultPage(EthernetClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><body><h1>ESP32 Barcode Scanner (GM66)</h1><p>Use <code>/qrcode</code> to get JSON data.</p></body></html>");
}
