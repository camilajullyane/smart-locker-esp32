#include <Arduino.h>

// Os botoes devem ser ligados entre o GPIO correspondente e o GND.
// INPUT_PULLUP mantem o pino em HIGH enquanto o botao estiver solto.
#define BLUE_OPEN_BUTTON_PIN 22
#define RED_CLOSE_BUTTON_PIN 21
#define GREEN_LED_PIN 26
#define RED_LED_PIN 27
#define BUZZER_PIN 14

const unsigned long BUTTON_DEBOUNCE_MS = 50;

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
  Serial.println("Comandos seriais: A=abrir, F=fechar, T=teste dos sinais, S=status dos botoes");
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
  Serial.println("Sistema pronto.");
}

void loop() {
  printButtonChanges();

  if (wasButtonPressed(openButton)) {
    openLocker();
  }

  if (wasButtonPressed(closeButton)) {
    closeLocker();
  }

  handleSerialCommands();
}
