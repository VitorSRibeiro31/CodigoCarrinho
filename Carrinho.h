#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <MFRC522.h>

// ===== SENSORES DE LINHA =====
#define SENSOR_ESQ 2
#define SENSOR_DIR 3
#define BRANCO LOW

// ===== ULTRASSONICO =====
#define ECHO A1
#define TRIG 7
#define DISTANCIA_PARADA 10

// ===== PONTE H L298N =====
#define ENA 6
#define ENB 9

#define IN1 8
#define IN2 0
#define IN3 A2
#define IN4 A3

// ===== VELOCIDADES INDEPENDENTES =====
// Altere de 0 a 255
int velocidadeEsquerda = 80;
int velocidadeDireita = 100;

// ===== LEDS =====
#define LED_VERDE 5
#define LED_VERMELHO 4

// ===== RFID =====
#define SS_PIN 10
#define RST_PIN A0
#define BLOCO_RFID 4

MFRC522 rfid(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key chave;

// ===== LCD 16x2 =====
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===== PILHA LIFO =====
#define TAMANHO_PILHA 5

int pilha[TAMANHO_PILHA];
int topo = -1;

byte uids[TAMANHO_PILHA][10];
byte tamanhosUID[TAMANHO_PILHA];

int totalLidos = 0;

// ===== ESTADOS =====
bool aguardandoCartao = false;
bool aguardandoLiberacao = false;
bool finalizado = false;

// ===== CONTROLE DOS LEDS =====
void atualizarLeds(bool andando) {
  digitalWrite(LED_VERDE, andando ? HIGH : LOW);
  digitalWrite(LED_VERMELHO, andando ? LOW : HIGH);
}

// ===== CONTROLE DOS MOTORES =====
void controlarMotores(int velocidadeEsq, int velocidadeDir) {

  velocidadeEsq = constrain(velocidadeEsq, 0, 255);
  velocidadeDir = constrain(velocidadeDir, 0, 255);

  // Motor esquerdo
  digitalWrite(IN1, velocidadeEsq > 0 ? HIGH : LOW);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, velocidadeEsq);

  // Motor direito
  digitalWrite(IN3, velocidadeDir > 0 ? HIGH : LOW);
  digitalWrite(IN4, LOW);
  analogWrite(ENB, velocidadeDir);

  atualizarLeds(velocidadeEsq > 0 || velocidadeDir > 0);
}

void pararMotores() {
  controlarMotores(0, 0);
}

// ===== MEDIR DISTANCIA =====
float medirDistancia() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);

  unsigned long duracao = pulseIn(ECHO, HIGH, 25000UL);

  if (duracao == 0) return -1;

  return duracao * 0.0343 / 2.0;
}

// ===== PILHA PUSH =====
void push(int valor) {
  if (topo < TAMANHO_PILHA - 1) {
    pilha[++topo] = valor;
  }
}

// ===== PILHA POP =====
int pop() {
  if (topo >= 0) {
    return pilha[topo--];
  }
  return -1;
}

// ===== VERIFICAR CARTAO REPETIDO =====
bool cartaoJaLido() {

  for (int i = 0; i < totalLidos; i++) {

    if (tamanhosUID[i] != rfid.uid.size)
      continue;

    bool igual = true;

    for (byte j = 0; j < rfid.uid.size; j++) {
      if (uids[i][j] != rfid.uid.uidByte[j]) {
        igual = false;
        break;
      }
    }

    if (igual) return true;
  }

  return false;
}

// ===== LER VALOR RFID =====
int lerValorRFID() {

  MFRC522::StatusCode status;

  status = rfid.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A,
    7,
    &chave,
    &(rfid.uid)
  );

  if (status != MFRC522::STATUS_OK)
    return -1;

  byte dados[18];
  byte tamanho = sizeof(dados);

  status = rfid.MIFARE_Read(
    BLOCO_RFID,
    dados,
    &tamanho
  );

  if (status != MFRC522::STATUS_OK)
    return -1;

  if (dados[0] >= '0' &&
      dados[0] <= '5' &&
      dados[1] == 0) {

    return dados[0] - '0';
  }

  return -1;
}

// ===== LCD AGUARDANDO CARTAO =====
void mostrarAguardando() {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("OBSTACULO!");

  lcd.setCursor(0, 1);
  lcd.print("Aproxime cartao");
}

// ===== LCD MOSTRAR VALOR =====
void mostrarValor(int valor) {

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("Cartao ");
  lcd.print(totalLidos);
  lcd.print("/5");

  lcd.setCursor(0, 1);
  lcd.print("Valor: ");
  lcd.print(valor);
}

// ===== MOSTRAR PILHA LIFO =====
void mostrarPilha() {

  pararMotores();

  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, LOW);

  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("PILHA LIFO");

  lcd.setCursor(0, 1);

  int coluna = 0;

  while (topo >= 0) {

    int valor = pop();

    lcd.setCursor(coluna, 1);
    lcd.print(valor);

    coluna += 2;
  }
}

// ===== PROCESSAR CARTAO RFID =====
void processarCartao() {

  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  if (cartaoJaLido()) {

    lcd.clear();
    lcd.print("Cartao repetido!");

    rfid.PICC_HaltA();

    delay(1000);
    mostrarAguardando();
    return;
  }

  int valor = lerValorRFID();

  if (valor >= 0 && valor <= 5) {

    // Salvar UID
    tamanhosUID[totalLidos] = rfid.uid.size;

    for (byte i = 0; i < rfid.uid.size; i++) {
      uids[totalLidos][i] =
        rfid.uid.uidByte[i];
    }

    // Empilhar valor
    push(valor);
    totalLidos++;

    mostrarValor(valor);

    aguardandoCartao = false;

    if (totalLidos == TAMANHO_PILHA) {

      delay(2000);

      mostrarPilha();
      finalizado = true;

    } else {

      aguardandoLiberacao = true;
      delay(1500);
    }

  } else {

    lcd.clear();
    lcd.print("Cartao invalido!");

    delay(1000);
    mostrarAguardando();
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ===== CONFIGURACAO =====
void setup() {

  pinMode(SENSOR_ESQ, INPUT);
  pinMode(SENSOR_DIR, INPUT);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);

  digitalWrite(TRIG, LOW);

  pararMotores();

  // Inicializar LCD
  Wire.begin();
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("Carrinho RFID");

  lcd.setCursor(0, 1);
  lcd.print("Iniciando...");

  // Inicializar RFID
  SPI.begin();
  rfid.PCD_Init();

  for (byte i = 0; i < 6; i++) {
    chave.keyByte[i] = 0xFF;
  }

  delay(1500);

  lcd.clear();
  lcd.print("Seguindo linha");
}

// ===== PROGRAMA PRINCIPAL =====
void loop() {

  if (finalizado) {

    pararMotores();

    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_VERMELHO, LOW);

    return;
  }

  // Aguardando cartao RFID
  if (aguardandoCartao) {

    pararMotores();
    processarCartao();

    return;
  }

  float distancia = medirDistancia();

  // Aguardando retirar obstaculo
  if (aguardandoLiberacao) {

    pararMotores();

    if (distancia > 12) {

      aguardandoLiberacao = false;

      lcd.clear();
      lcd.print("Seguindo linha");
    }

    return;
  }

  // Obstaculo detectado
  if (distancia > 0 &&
      distancia <= DISTANCIA_PARADA) {

    pararMotores();

    aguardandoCartao = true;
    mostrarAguardando();

    return;
  }

  // Sensores de linha
  int esq = digitalRead(SENSOR_ESQ);
  int dir = digitalRead(SENSOR_DIR);

  if (esq == BRANCO && dir == BRANCO) {

    controlarMotores(
      velocidadeEsquerda,
      velocidadeDireita
    );
  }

  else if (esq == BRANCO && dir != BRANCO) {

    controlarMotores(
      0,
      velocidadeDireita
    );
  }

  else if (esq != BRANCO && dir == BRANCO) {

    controlarMotores(
      velocidadeEsquerda,
      0
    );
  }

  else {

    pararMotores();
  }
}