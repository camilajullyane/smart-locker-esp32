#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ESP32Servo.h>


#define SS_PIN 5
#define RST_PIN 21

#define SERVO_PIN 13
#define GREEN_LED_PIN 26
#define RED_LED_PIN 27
#define BUZZER_PIN 14

const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// Troque pela URL do seu Firebase Realtime Database
const char* FIREBASE_HOST = "https://projeto-top-redes-default-rtdb.firebaseio.com";

const String LOCKER_ID = "locker-01";

MFRC522 rfid(SS_PIN, RST_PIN);
Servo lockerServo;

void connectWiFi() {
  Serial.print("Conectando ao Wi-Fi");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("Wi-Fi conectado!");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

int firebaseRequest(
  const String& method,
  const String& path,
  const String& payload,
  String& response
) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  WiFiClientSecure client;

  // Para protótipo no Wokwi.
  // Em projeto real, o correto é usar certificado.
  client.setInsecure();

  HTTPClient http;

  String url = String(FIREBASE_HOST) + path + ".json";

  Serial.println();
  Serial.print("URL Firebase: ");
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("Erro ao iniciar HTTP");
    return -1;
  }

  http.addHeader("Content-Type", "application/json");

  int httpCode;

  if (method == "GET") {
    httpCode = http.GET();
  } else if (method == "POST") {
    httpCode = http.POST(payload);
  } else if (method == "PUT") {
    httpCode = http.PUT(payload);
  } else {
    Serial.println("Metodo HTTP invalido");
    http.end();
    return -1;
  }

  response = http.getString();

  Serial.print("HTTP Code: ");
  Serial.println(httpCode);

  Serial.print("Resposta Firebase: ");
  Serial.println(response);

  http.end();

  return httpCode;
}

String getCardUID() {
  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {
    if (i > 0) {
      uid += ":";
    }

    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }

    uid += String(rfid.uid.uidByte[i], HEX);
  }

  uid.toUpperCase();
  return uid;
}

String uidToFirebaseKey(String uid) {
  uid.replace(":", "_");
  return uid;
}

bool isAuthorized(String uid) {
  String key = uidToFirebaseKey(uid);
  String response;

  String path = "/authorizedUsers/" + key + "/active";

  int code = firebaseRequest("GET", path, "", response);

  if (code == 200 && response == "true") {
    return true;
  }

  return false;
}

void updateLockerStatus(String state, String uid, String eventType) {
  String response;

  String payload = "{";
  payload += "\"state\":\"" + state + "\",";
  payload += "\"lastUid\":\"" + uid + "\",";
  payload += "\"lastEvent\":\"" + eventType + "\",";
  payload += "\"updatedAtMs\":" + String(millis());
  payload += "}";

  firebaseRequest("PUT", "/lockers/" + LOCKER_ID, payload, response);
}

void sendAccessLog(String uid, bool authorized) {
  String response;

  String status = authorized ? "AUTHORIZED" : "DENIED";
  String lockerState = authorized ? "OPEN" : "BLOCKED";

  String payload = "{";
  payload += "\"lockerId\":\"" + LOCKER_ID + "\",";
  payload += "\"uid\":\"" + uid + "\",";
  payload += "\"status\":\"" + status + "\",";
  payload += "\"lockerState\":\"" + lockerState + "\",";
  payload += "\"source\":\"ESP32-WOKWI\",";
  payload += "\"timestampMs\":" + String(millis());
  payload += "}";

  firebaseRequest("POST", "/logs", payload, response);
}

void unlockLocker() {
  Serial.println("Acesso autorizado. Abrindo locker...");

  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, LOW);

  tone(BUZZER_PIN, 1200, 150);

  lockerServo.write(90);
  delay(3000);

  lockerServo.write(0);
  digitalWrite(GREEN_LED_PIN, LOW);

  Serial.println("Locker fechado novamente.");
}

void denyAccess() {
  Serial.println("Acesso negado!");

  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, HIGH);

  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 500, 150);
    delay(250);
  }

  digitalWrite(RED_LED_PIN, LOW);
}

void setup() {
  Serial.begin(115200);

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);

  lockerServo.attach(SERVO_PIN);
  lockerServo.write(0);

  SPI.begin(18, 19, 23, 5);

  rfid.PCD_Init();

  connectWiFi();

  Serial.println("Sistema de locker inteligente iniciado.");
  Serial.println("Aproxime uma tag RFID no leitor.");
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  String uid = getCardUID();

  Serial.println();
  Serial.print("UID lido: ");
  Serial.println(uid);

  bool authorized = isAuthorized(uid);

  if (authorized) {
    updateLockerStatus("open", uid, "authorized_access");
    sendAccessLog(uid, true);
    unlockLocker();
    updateLockerStatus("closed", uid, "auto_closed");
  } else {
    updateLockerStatus("blocked", uid, "denied_access");
    sendAccessLog(uid, false);
    denyAccess();
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(1500);
}