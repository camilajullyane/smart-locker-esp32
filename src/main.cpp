#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

// Os botoes devem ser ligados entre o GPIO correspondente e o GND.
// INPUT_PULLUP mantem o pino em HIGH enquanto o botao estiver solto.
#define BLUE_OPEN_BUTTON_PIN 22
#define RED_CLOSE_BUTTON_PIN 21
#define GREEN_LED_PIN 26
#define RED_LED_PIN 27
#define BUZZER_PIN 14

#ifndef WIFI_SSID
#define WIFI_SSID "Wokwi-GUEST"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#ifndef WIFI_ENABLED
#define WIFI_ENABLED 1
#endif

#ifndef FIREBASE_DATABASE_URL
#define FIREBASE_DATABASE_URL "https://projeto-top-redes-default-rtdb.firebaseio.com"
#endif

#ifndef LOCKER_ID
#define LOCKER_ID "locker-a-12"
#endif

const unsigned long BUTTON_DEBOUNCE_MS = 50;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
const unsigned long WIFI_RECONNECT_INTERVAL_MS = 10000;
const unsigned long FIREBASE_POLL_INTERVAL_MS = 3000;

struct Button {
  int pin;
  bool stableState;
  bool lastReading;
  unsigned long lastChangeMs;
};

Button openButton = {BLUE_OPEN_BUTTON_PIN, HIGH, HIGH, 0};
Button closeButton = {RED_CLOSE_BUTTON_PIN, HIGH, HIGH, 0};

bool lockerIsOpen = false;
bool lastDebugOpenReading = HIGH;
bool lastDebugCloseReading = HIGH;
bool wifiWasConnected = false;
unsigned long lastWifiReconnectAttemptMs = 0;
unsigned long lastFirebasePollMs = 0;
String lastProcessedCommandId = "";

void beep(int frequency, int durationMs) {
  tone(BUZZER_PIN, frequency);
  delay(durationMs);
  noTone(BUZZER_PIN);
  delay(30);
}

void showOpenState(bool playSound = true) {
  lockerIsOpen = true;
  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, LOW);

  if (playSound) {
    beep(1400, 100);
    beep(1800, 140);
  }

  Serial.println("ARMARIO ABERTO | LED verde aceso");
}

void showClosedState(bool playSound = true) {
  lockerIsOpen = false;
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, HIGH);

  if (playSound) {
    beep(700, 250);
  }

  Serial.println("ARMARIO FECHADO | LED vermelho aceso");
}

void openLocker() {
  if (lockerIsOpen) {
    Serial.println("Comando ignorado: o armario ja esta aberto.");
    return;
  }

  Serial.println("Botao azul pressionado.");
  showOpenState();
}

void closeLocker() {
  if (!lockerIsOpen) {
    Serial.println("Comando ignorado: o armario ja esta fechado.");
    return;
  }

  Serial.println("Botao vermelho pressionado.");
  showClosedState();
}

void printWifiStatus() {
#if !WIFI_ENABLED
  Serial.println("WiFi desativado por configuracao (WIFI_ENABLED=0).");
  return;
#endif

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi conectado.");
    Serial.printf("SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Sinal: %d dBm\n", WiFi.RSSI());
    return;
  }

  Serial.println("WiFi desconectado.");
}

bool connectToWifi() {
#if !WIFI_ENABLED
  Serial.println("WiFi desativado por configuracao (WIFI_ENABLED=0).");
  return false;
#endif

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Conectando ao WiFi \"%s\"", WIFI_SSID);

  unsigned long startedAt = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    Serial.print(".");
    delay(500);
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiWasConnected = true;
    printWifiStatus();
    return true;
  }

  wifiWasConnected = false;
  Serial.println("Nao foi possivel conectar ao WiFi agora. O sistema continuara funcionando localmente.");
  Serial.println("Confira SSID/senha ou tente novamente pelo comando serial C.");
  return false;
}

void maintainWifiConnection() {
#if !WIFI_ENABLED
  return;
#endif

  bool wifiConnected = WiFi.status() == WL_CONNECTED;

  if (wifiConnected) {
    if (!wifiWasConnected) {
      wifiWasConnected = true;
      Serial.println("WiFi reconectado.");
      printWifiStatus();
    }

    return;
  }

  if (wifiWasConnected) {
    wifiWasConnected = false;
    Serial.println("WiFi desconectou.");
  }

  if (millis() - lastWifiReconnectAttemptMs >= WIFI_RECONNECT_INTERVAL_MS) {
    lastWifiReconnectAttemptMs = millis();
    Serial.println("Tentando reconectar ao WiFi...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

String firebaseCommandUrl() {
  return String(FIREBASE_DATABASE_URL) + "/lockerCommands/" + LOCKER_ID + ".json";
}

String extractJsonString(const String &payload, const String &key) {
  String pattern = "\"" + key + "\":\"";
  int startIndex = payload.indexOf(pattern);

  if (startIndex < 0) {
    return "";
  }

  startIndex += pattern.length();
  int endIndex = payload.indexOf("\"", startIndex);

  if (endIndex < 0) {
    return "";
  }

  return payload.substring(startIndex, endIndex);
}

bool extractJsonBool(const String &payload, const String &key) {
  String pattern = "\"" + key + "\":true";
  return payload.indexOf(pattern) >= 0;
}

void updateFirebaseCommandStatus(const String &status) {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  http.begin(firebaseCommandUrl());
  http.addHeader("Content-Type", "application/json");

  String body = "{\"status\":\"" + status +
                "\",\"processedAtDeviceMs\":" + String(millis()) + "}";
  int httpCode = http.PATCH(body);

  if (httpCode > 0) {
    Serial.printf("Status do comando atualizado no Firebase: %s (%d)\n",
                  status.c_str(), httpCode);
  } else {
    Serial.printf("Falha ao atualizar comando no Firebase: %s\n",
                  http.errorToString(httpCode).c_str());
  }

  http.end();
}

void handleFirebaseCommand(const String &payload) {
  String commandPayload = payload;
  commandPayload.trim();

  if (commandPayload.length() == 0 || commandPayload == "null") {
    return;
  }

  String commandId = extractJsonString(commandPayload, "commandId");
  String action = extractJsonString(commandPayload, "action");
  String status = extractJsonString(commandPayload, "status");
  bool allowed = extractJsonBool(commandPayload, "allowed");

  if (status != "pending" || !allowed || commandId.length() == 0) {
    return;
  }

  if (commandId == lastProcessedCommandId) {
    return;
  }

  lastProcessedCommandId = commandId;

  Serial.printf("Comando Firebase recebido: %s | locker %s\n",
                action.c_str(), LOCKER_ID);

  if (action == "open") {
    openLocker();
    updateFirebaseCommandStatus("done");
  } else if (action == "close") {
    closeLocker();
    updateFirebaseCommandStatus("done");
  } else {
    Serial.println("Comando Firebase ignorado: action desconhecida.");
    updateFirebaseCommandStatus("ignored");
  }
}

void pollFirebaseCommands() {
#if !WIFI_ENABLED
  return;
#endif

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (millis() - lastFirebasePollMs < FIREBASE_POLL_INTERVAL_MS) {
    return;
  }

  lastFirebasePollMs = millis();

  HTTPClient http;
  http.begin(firebaseCommandUrl());
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    handleFirebaseCommand(http.getString());
  } else if (httpCode > 0) {
    Serial.printf("Firebase respondeu HTTP %d ao buscar comandos.\n", httpCode);
  } else {
    Serial.printf("Falha ao buscar comandos no Firebase: %s\n",
                  http.errorToString(httpCode).c_str());
  }

  http.end();
}

// Retorna true uma unica vez quando um botao passa de solto para pressionado.
bool wasButtonPressed(Button &button) {
  bool reading = digitalRead(button.pin);

  if (reading != button.lastReading) {
    button.lastChangeMs = millis();
    button.lastReading = reading;
  }

  if (millis() - button.lastChangeMs >= BUTTON_DEBOUNCE_MS &&
      reading != button.stableState) {
    button.stableState = reading;
    return button.stableState == LOW;
  }

  return false;
}

void quickHardwareTest() {
  Serial.println("Iniciando teste dos LEDs e do buzzer...");

  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, HIGH);
  beep(1400, 100);
  beep(1800, 140);
  delay(300);

  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, HIGH);
  beep(700, 250);
  delay(300);

  if (lockerIsOpen) {
    showOpenState(false);
  } else {
    showClosedState(false);
  }

  Serial.println("Teste finalizado.");
}

void handleSerialCommands() {
  if (!Serial.available()) {
    return;
  }

  char command = Serial.read();

  if (command == 'a' || command == 'A') {
    openLocker();
  } else if (command == 'f' || command == 'F') {
    closeLocker();
  } else if (command == 't' || command == 'T') {
    quickHardwareTest();
  } else if (command == 's' || command == 'S') {
    Serial.printf("Botao azul GPIO %d: %s\n", BLUE_OPEN_BUTTON_PIN,
                  digitalRead(BLUE_OPEN_BUTTON_PIN) == LOW ? "PRESSIONADO" : "SOLTO");
    Serial.printf("Botao vermelho GPIO %d: %s\n", RED_CLOSE_BUTTON_PIN,
                  digitalRead(RED_CLOSE_BUTTON_PIN) == LOW ? "PRESSIONADO" : "SOLTO");
  } else if (command == 'w' || command == 'W') {
    printWifiStatus();
  } else if (command == 'c' || command == 'C') {
    connectToWifi();
  }
}

void printButtonChanges() {
  bool openReading = digitalRead(BLUE_OPEN_BUTTON_PIN);
  bool closeReading = digitalRead(RED_CLOSE_BUTTON_PIN);

  if (openReading != lastDebugOpenReading) {
    lastDebugOpenReading = openReading;
    Serial.printf("Leitura azul GPIO %d mudou para %s\n", BLUE_OPEN_BUTTON_PIN,
                  openReading == LOW ? "LOW/PRESSIONADO" : "HIGH/SOLTO");
  }

  if (closeReading != lastDebugCloseReading) {
    lastDebugCloseReading = closeReading;
    Serial.printf("Leitura vermelha GPIO %d mudou para %s\n", RED_CLOSE_BUTTON_PIN,
                  closeReading == LOW ? "LOW/PRESSIONADO" : "HIGH/SOLTO");
  }
}

void printInstructions() {
  Serial.println();
  Serial.println("=== SMART LOCKER - CONTROLE POR BOTOES ===");
  Serial.printf("Botao azul (abrir)     -> GPIO %d e GND\n", BLUE_OPEN_BUTTON_PIN);
  Serial.printf("Botao vermelho (fechar)-> GPIO %d e GND\n", RED_CLOSE_BUTTON_PIN);
  Serial.printf("LED verde              -> GPIO %d\n", GREEN_LED_PIN);
  Serial.printf("LED vermelho           -> GPIO %d\n", RED_LED_PIN);
  Serial.printf("Buzzer                  -> GPIO %d\n", BUZZER_PIN);
  Serial.printf("WiFi SSID configurado  -> %s\n", WIFI_SSID);
  Serial.printf("Locker Firebase        -> %s\n", LOCKER_ID);
  Serial.println("Comandos seriais: A=abrir, F=fechar, T=teste dos sinais, S=status dos botoes, W=status WiFi, C=reconectar WiFi");
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BLUE_OPEN_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RED_CLOSE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  // Sincroniza o debounce com o estado real dos botoes na inicializacao.
  openButton.stableState = openButton.lastReading = digitalRead(openButton.pin);
  closeButton.stableState = closeButton.lastReading = digitalRead(closeButton.pin);
  lastDebugOpenReading = openButton.lastReading;
  lastDebugCloseReading = closeButton.lastReading;

  printInstructions();
  showClosedState(false);
  // connectToWifi();
  Serial.println("Sistema pronto.");
}

void loop() {
  maintainWifiConnection();
  pollFirebaseCommands();
  printButtonChanges();

  if (wasButtonPressed(openButton)) {
    openLocker();
  }

  if (wasButtonPressed(closeButton)) {
    closeLocker();
  }

  handleSerialCommands();
}
