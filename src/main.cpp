#include <Arduino.h>
#include <ESP32Servo.h>

#define BLUE_REQUEST_BUTTON_PIN 4
#define RED_SENSOR_BUTTON_PIN 5
#define SERVO_PIN 13
#define GREEN_LED_PIN 26
#define RED_LED_PIN 27
#define BUZZER_PIN 14

const int SERVO_CLOSED_ANGLE = 0;
const int SERVO_OPEN_ANGLE = 90;
const int SERVO_STEP_DELAY_MS = 30;
const unsigned long SERVO_OPEN_TIME_MS = 3000;
const unsigned long BUTTON_DEBOUNCE_MS = 120;
const unsigned long BUTTON_COOLDOWN_MS = 1500;
const unsigned long DOOR_CHECK_INTERVAL_MS = 2000;

const int DOOR_OPEN_VALUE = 0;
const int DOOR_CLOSED_VALUE = 1;

enum PanelStatus {
  STATUS_READY,
  STATUS_RELEASED,
  STATUS_DOOR_OPEN,
  STATUS_DOOR_CLOSED,
  STATUS_LOCKING,
  STATUS_ERROR
};

Servo lockerServo;

bool lastRequestButtonPressed = false;
bool lastSensorButtonPressed = false;
bool servoIsOpen = false;
int currentServoAngle = SERVO_CLOSED_ANGLE;
int simulatedDoorState = DOOR_CLOSED_VALUE;
PanelStatus currentPanelStatus = STATUS_READY;

unsigned long lastRequestActionMs = 0;
unsigned long lastSensorActionMs = 0;
unsigned long lastDoorCheckMs = 0;
unsigned long lastHeartbeatMs = 0;

void beep(int frequency, int durationMs) {
  tone(BUZZER_PIN, frequency, durationMs);
  delay(durationMs + 30);
  noTone(BUZZER_PIN);
}

bool isButtonPressed(int pin) {
  return digitalRead(pin) == LOW;
}

bool isButtonPressedStable(int pin) {
  if (!isButtonPressed(pin)) {
    return false;
  }

  unsigned long pressedSinceMs = millis();

  while (millis() - pressedSinceMs < BUTTON_DEBOUNCE_MS) {
    if (!isButtonPressed(pin)) {
      return false;
    }

    delay(5);
  }

  return true;
}

const char* doorStateText() {
  return simulatedDoorState == DOOR_CLOSED_VALUE ? "FECHADA" : "ABERTA";
}

const char* panelStatusText(PanelStatus status) {
  switch (status) {
    case STATUS_READY:
      return "PRONTO";
    case STATUS_RELEASED:
      return "LIBERADO";
    case STATUS_DOOR_OPEN:
      return "PORTA ABERTA";
    case STATUS_DOOR_CLOSED:
      return "PORTA FECHADA";
    case STATUS_LOCKING:
      return "FECHANDO";
    case STATUS_ERROR:
      return "ERRO";
    default:
      return "DESCONHECIDO";
  }
}

void showPanelStatus(PanelStatus status) {
  currentPanelStatus = status;

  if (status == STATUS_RELEASED || status == STATUS_DOOR_CLOSED || status == STATUS_READY) {
    digitalWrite(GREEN_LED_PIN, HIGH);
    digitalWrite(RED_LED_PIN, LOW);
  } else {
    digitalWrite(GREEN_LED_PIN, LOW);
    digitalWrite(RED_LED_PIN, HIGH);
  }

  Serial.print("PAINEL: ");
  Serial.print(panelStatusText(status));
  Serial.print(" | sensor=");
  Serial.print(simulatedDoorState);
  Serial.print(" | porta=");
  Serial.println(doorStateText());
}

void moveServoSlowly(int targetAngle) {
  if (currentServoAngle < targetAngle) {
    for (int angle = currentServoAngle; angle <= targetAngle; angle++) {
      lockerServo.write(angle);
      delay(SERVO_STEP_DELAY_MS);
    }
  } else {
    for (int angle = currentServoAngle; angle >= targetAngle; angle--) {
      lockerServo.write(angle);
      delay(SERVO_STEP_DELAY_MS);
    }
  }

  currentServoAngle = targetAngle;
}

void printInstructions() {
  Serial.println();
  Serial.println("=== SMART LOCKER - TESTE COM BOTOES E SENSOR SIMULADO ===");
  Serial.println("Monitor serial: 115200 baud");
  Serial.println();
  Serial.println("Conexoes esperadas:");
  Serial.printf("  Botao azul pedido       -> GPIO %d e GND\n", BLUE_REQUEST_BUTTON_PIN);
  Serial.printf("  Botao vermelho sensor   -> GPIO %d e GND\n", RED_SENSOR_BUTTON_PIN);
  Serial.printf("  Servo sinal             -> GPIO %d\n", SERVO_PIN);
  Serial.printf("  LED verde/status        -> GPIO %d\n", GREEN_LED_PIN);
  Serial.printf("  LED vermelho/status     -> GPIO %d\n", RED_LED_PIN);
  Serial.printf("  Buzzer                  -> GPIO %d\n", BUZZER_PIN);
  Serial.println();
  Serial.println("Sensor simulado: 0=porta aberta, 1=porta fechada.");
  Serial.println("Comandos seriais: A=processo, S=alterna sensor, F=fechar servo, T=teste rapido.");
  Serial.println();
}

void setIdleState() {
  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);
  noTone(BUZZER_PIN);

  lockerServo.write(SERVO_CLOSED_ANGLE);
  currentServoAngle = SERVO_CLOSED_ANGLE;
  servoIsOpen = false;
  simulatedDoorState = DOOR_CLOSED_VALUE;
}

void closeServo() {
  if (!servoIsOpen) {
    Serial.println("Servo ja esta fechado.");
    showPanelStatus(simulatedDoorState == DOOR_CLOSED_VALUE ? STATUS_DOOR_CLOSED : STATUS_DOOR_OPEN);
    return;
  }

  Serial.println("Fechando servo lentamente...");
  showPanelStatus(STATUS_LOCKING);

  moveServoSlowly(SERVO_CLOSED_ANGLE);
  servoIsOpen = false;

  showPanelStatus(simulatedDoorState == DOOR_CLOSED_VALUE ? STATUS_DOOR_CLOSED : STATUS_DOOR_OPEN);
}

void runAccessProcess() {
  Serial.println();
  Serial.println("Pedido recebido pelo botao azul.");

  if (servoIsOpen) {
    Serial.println("Processo ignorado: servo ja esta aberto.");
    showPanelStatus(STATUS_RELEASED);
    return;
  }

  showPanelStatus(STATUS_RELEASED);
  beep(1400, 120);

  Serial.println("Abrindo servo lentamente...");
  moveServoSlowly(SERVO_OPEN_ANGLE);
  servoIsOpen = true;

  Serial.printf("Servo aberto. Aguardando %lu ms antes de fechar...\n", SERVO_OPEN_TIME_MS);
  delay(SERVO_OPEN_TIME_MS);

  closeServo();
}

void toggleSimulatedDoorSensor() {
  simulatedDoorState = simulatedDoorState == DOOR_CLOSED_VALUE ? DOOR_OPEN_VALUE : DOOR_CLOSED_VALUE;

  Serial.println();
  Serial.println("Botao vermelho acionado: sensor simulado alterado.");
  Serial.print("Valor enviado pelo sensor: ");
  Serial.println(simulatedDoorState);

  showPanelStatus(simulatedDoorState == DOOR_CLOSED_VALUE ? STATUS_DOOR_CLOSED : STATUS_DOOR_OPEN);
}

void checkDoorState() {
  Serial.print("Verificacao periodica da porta: sensor=");
  Serial.print(simulatedDoorState);
  Serial.print(" -> ");
  Serial.println(doorStateText());

  showPanelStatus(simulatedDoorState == DOOR_CLOSED_VALUE ? STATUS_DOOR_CLOSED : STATUS_DOOR_OPEN);
}

void quickHardwareTest() {
  Serial.println();
  Serial.println("Teste rapido manual dos componentes...");

  Serial.println("LED verde");
  digitalWrite(GREEN_LED_PIN, HIGH);
  delay(500);
  digitalWrite(GREEN_LED_PIN, LOW);

  Serial.println("LED vermelho");
  digitalWrite(RED_LED_PIN, HIGH);
  delay(500);
  digitalWrite(RED_LED_PIN, LOW);

  Serial.println("Buzzer");
  beep(1000, 150);
  beep(1600, 150);

  Serial.println("Servo: fechado -> aberto -> fechado");
  moveServoSlowly(SERVO_OPEN_ANGLE);
  delay(1000);
  moveServoSlowly(SERVO_CLOSED_ANGLE);
  servoIsOpen = false;

  showPanelStatus(STATUS_READY);
  Serial.println("Teste rapido finalizado.");
}

void handleRequestButton() {
  bool buttonPressed = isButtonPressed(BLUE_REQUEST_BUTTON_PIN);

  if (millis() - lastRequestActionMs < BUTTON_COOLDOWN_MS) {
    lastRequestButtonPressed = buttonPressed;
    return;
  }

  if (!lastRequestButtonPressed && buttonPressed) {
    if (isButtonPressedStable(BLUE_REQUEST_BUTTON_PIN)) {
      lastRequestActionMs = millis();
      runAccessProcess();

      while (isButtonPressed(BLUE_REQUEST_BUTTON_PIN)) {
        delay(10);
      }
    }
  }

  lastRequestButtonPressed = isButtonPressed(BLUE_REQUEST_BUTTON_PIN);
}

void handleSensorButton() {
  bool buttonPressed = isButtonPressed(RED_SENSOR_BUTTON_PIN);

  if (millis() - lastSensorActionMs < BUTTON_COOLDOWN_MS) {
    lastSensorButtonPressed = buttonPressed;
    return;
  }

  if (!lastSensorButtonPressed && buttonPressed) {
    if (isButtonPressedStable(RED_SENSOR_BUTTON_PIN)) {
      lastSensorActionMs = millis();
      toggleSimulatedDoorSensor();

      while (isButtonPressed(RED_SENSOR_BUTTON_PIN)) {
        delay(10);
      }
    }
  }

  lastSensorButtonPressed = isButtonPressed(RED_SENSOR_BUTTON_PIN);
}

void handleSerialCommands() {
  if (!Serial.available()) {
    return;
  }

  char command = Serial.read();

  if (command == 'a' || command == 'A') {
    runAccessProcess();
  } else if (command == 's' || command == 'S') {
    toggleSimulatedDoorSensor();
  } else if (command == 'f' || command == 'F') {
    closeServo();
  } else if (command == 't' || command == 'T') {
    quickHardwareTest();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  pinMode(BLUE_REQUEST_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RED_SENSOR_BUTTON_PIN, INPUT_PULLUP);
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  lockerServo.attach(SERVO_PIN);
  setIdleState();

  lastRequestButtonPressed = isButtonPressed(BLUE_REQUEST_BUTTON_PIN);
  lastSensorButtonPressed = isButtonPressed(RED_SENSOR_BUTTON_PIN);

  printInstructions();
  showPanelStatus(STATUS_READY);
  Serial.println("Pronto. Azul=pede abertura. Vermelho=simula sensor da porta.");
}

void loop() {
  handleRequestButton();
  handleSensorButton();
  handleSerialCommands();

  if (millis() - lastDoorCheckMs >= DOOR_CHECK_INTERVAL_MS) {
    lastDoorCheckMs = millis();
    checkDoorState();
  }

  if (millis() - lastHeartbeatMs >= 5000) {
    lastHeartbeatMs = millis();
    Serial.print("Sistema ativo | azul=");
    Serial.print(isButtonPressed(BLUE_REQUEST_BUTTON_PIN) ? "PRESSIONADO" : "SOLTO");
    Serial.print(" | vermelho=");
    Serial.print(isButtonPressed(RED_SENSOR_BUTTON_PIN) ? "PRESSIONADO" : "SOLTO");
    Serial.print(" | porta=");
    Serial.println(doorStateText());
  }
}
