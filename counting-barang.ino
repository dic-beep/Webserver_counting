#include <SPI.h>
#include <Ethernet.h>
#include <ArduinoJson.h>

// Konfigurasi MAC dan IP static
byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
// IPAddress ip(192, 168, 1, 176); // Ganti sesuai jaringanmu
// IPAddress myDns(192, 168, 1, 1);

// Pin photosensor
const int sensorPin = 17;
int lastSensorState = HIGH;

// Counter barang
volatile unsigned long itemCount = 0;

// Server
EthernetServer server(80);
IPAddress ip;

void resetCounter() {
  itemCount = 0;
  Serial.println("Counter reset to 0");
}

void setup() {
  Serial.begin(115200);
  pinMode(sensorPin, INPUT_PULLUP);

  // Inisialisasi Ethernet
  Ethernet.init(5); // Pin CS W5500 ke GPIO 5
  Ethernet.begin(mac);
  delay(1000);

  ip = Ethernet.localIP();

  Serial.print("Server is at ");
  Serial.println(ip);

  server.begin();
}

void loop() {
  EthernetClient client = server.available();
  if (client) {
    Serial.println("Client connected");
    String req = "";

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        req += c;

        // Permintaan selesai
        if (c == '\n') {
          if (req.indexOf("GET /count") >= 0) {
            sendJsonResponse(client);
          } else if (req.indexOf("POST /reset") >= 0) {
            resetCounter();
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

  checkSensor();
}

void checkSensor() {
  int sensorState = digitalRead(sensorPin);
  if (lastSensorState == HIGH && sensorState == LOW) {
    itemCount++;
    Serial.print("Item Detected! Count: ");
    Serial.println(itemCount);
    delay(300); // Debounce
  }
  lastSensorState = sensorState;
}

void sendJsonResponse(EthernetClient& client) {
  JsonDocument doc;
  doc["count"] = itemCount;
  doc["ip"] = ip;

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
  client.println("<html><body><h1>ESP32 Counter</h1><p>Use <code>/count</code> to get JSON data.</p></body></html>");
}