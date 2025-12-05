/*
 * SISTEMA DE CONTROLE DE ACESSO - VERSÃO FINAL
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Preferences.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <time.h>

// ════════════ CONFIGURAÇÕES ════════════
#define CLASS_ID      1
#define ROOM_NAME     "Sala teste"
#define WIFI_SSID     "esp"
#define WIFI_PASS     "12345678"
#define MQTT_HOST     "192.168.0.108"
#define MQTT_PORT     1883
#define NTP_SERVER    "pool.ntp.org"
#define GMT_OFFSET    -10800  
#define TIMEOUT_MS    5000    

// Pins
#define PIN_RFID_SS   15
#define PIN_RFID_RST  27
#define PIN_LED_G     2   
#define PIN_LED_R     4   
#define PIN_LED_Y     13  
#define PIN_LED_W     12  
#define PIN_BUTTON    14  

// Globais
LiquidCrystal_I2C lcd(0x27, 16, 2);
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Preferences prefs;

// Estados do Sistema
enum Mode { NORMAL, REGISTRATION };
Mode currentMode = NORMAL;

// Variáveis de Controle
bool waitingResponse = false;       
unsigned long requestStartTime = 0; 
byte lastUid[4];
unsigned long lastButtonPress = 0;
unsigned long lastRfidCheck = 0;
unsigned long lastSyncAttempt = 0; 
bool rfidWasMissing = false; 

// ─── VARIÁVEIS DE SINCRONIZAÇÃO PENDENTE ───
bool pendingExitSync = false; 
String pendingExitUid = "";   

// ════════════ CLASSE PRINCIPAL ════════════
class AccessControl {
private:
    static AccessControl* instance;

public:
    AccessControl() { instance = this; }

    // Inicializa periféricos (Serial, GPIOs, I2C, SPI), conecta WiFi/NTP e configura estado inicial da UI.
    void init() {
        Serial.begin(115200);
        
        pinMode(PIN_LED_G, OUTPUT); pinMode(PIN_LED_R, OUTPUT);
        pinMode(PIN_LED_Y, OUTPUT); pinMode(PIN_LED_W, OUTPUT);
        pinMode(PIN_BUTTON, INPUT_PULLUP);
        
        digitalWrite(PIN_LED_G, LOW); digitalWrite(PIN_LED_R, LOW);
        digitalWrite(PIN_LED_Y, LOW); digitalWrite(PIN_LED_W, LOW);

        Wire.begin(); lcd.init(); lcd.backlight();
        SPI.begin(); rfid.PCD_Init();
        
        if (!checkRfidHardware()) {
            digitalWrite(PIN_LED_Y, HIGH);
            showLcd("Erro Hardware", "Verificar RFID");
        }

        connectNetwork(false); 
        syncTime(); 
        
        updateUI();
        Serial.println(F("[SYS] Sistema Pronto - Modo Async Message Aware"));
    }

    // Loop principal: gerencia rede, verifica botão físico, timeouts, integridade do hardware RFID e leitura de cartões.
    void loop() {
        yield(); // Evita Watchdog Reset no ESP32

        handleNetworkAndSync();
        checkPhysicalButton();
        checkTimeout(); 
        checkRfidHealth();

        if (Serial.available()) handleSerial(toupper(Serial.read()));

        // Se houver uma saída pendente (offline), pisca LCD indicando sincronização e bloqueia leitura de novos cartões.
        if (pendingExitSync) {
            static unsigned long blinkTimer = 0;
            if (millis() - blinkTimer > 1000) {
                showLcd("Sala Livre", "Sincronizando...");
                blinkTimer = millis();
            }
            return; 
        }

        // Ignora leitura se estiver aguardando resposta do servidor ou se o leitor estiver desconectado.
        if (waitingResponse || rfidWasMissing) return;

        if (!rfid.PICC_IsNewCardPresent()) return;
        if (!rfid.PICC_ReadCardSerial()) return;
        
        memcpy(lastUid, rfid.uid.uidByte, 4);
        processCardLogic();
        
        rfid.PICC_HaltA();
        rfid.PCD_StopCrypto1(); 
    }

private:
    // Remove caracteres acentuados da String para garantir compatibilidade com displays LCD 16x2 comuns (ASCII).
    String sanitizeText(String text) {
        text.replace("á", "a"); text.replace("à", "a"); text.replace("ã", "a"); text.replace("â", "a");
        text.replace("é", "e"); text.replace("ê", "e");
        text.replace("í", "i");
        text.replace("ó", "o"); text.replace("õ", "o"); text.replace("ô", "o");
        text.replace("ú", "u");
        text.replace("ç", "c");
        
        text.replace("Á", "A"); text.replace("À", "A"); text.replace("Ã", "A"); text.replace("Â", "A");
        text.replace("É", "E"); text.replace("Ê", "E");
        text.replace("Í", "I");
        text.replace("Ó", "O"); text.replace("Õ", "O"); text.replace("Ô", "O");
        text.replace("Ú", "U");
        text.replace("Ç", "C");
        return text;
    }
    // ──────────────────────────────────────────

    // Mantém a conexão MQTT ativa e tenta enviar registros de saída que ocorreram enquanto o dispositivo estava offline.
    void handleNetworkAndSync() {
        if (!mqtt.connected()) {
            if (millis() - lastSyncAttempt > 2000) {
                connectNetwork(true); 
                lastSyncAttempt = millis();
            }
        } else {
            mqtt.loop();
            // Lógica de Retentativa: Se há uma saída pendente, tenta enviar ao servidor a cada 3 segundos.
            if (pendingExitSync) {
                if (millis() - lastSyncAttempt > 3000) {
                    Serial.println(F("[SYNC] Internet presente. Enviando pendência..."));
                    sendRequest("register-exit/request", pendingExitUid.c_str(), false, true);
                    lastSyncAttempt = millis();
                }
            }
        }
    }

    // Gerencia conexão WiFi e MQTT. 
    // Parâmetro 'nonBlocking': se false, trava o código até conectar (usado no boot). Se true, apenas tenta conectar se cair (usado no loop).
    void connectNetwork(bool nonBlocking) {
        if (WiFi.status() != WL_CONNECTED) {
            digitalWrite(PIN_LED_Y, HIGH); 
            
            if (!nonBlocking) {
                showLcd("Conectando WiFi", WIFI_SSID);
                WiFi.begin(WIFI_SSID, WIFI_PASS);
                unsigned long start = millis();
                while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
                    delay(500);
                    yield();
                }
            } else {
                if (WiFi.status() != WL_CONNECTED) {
                      WiFi.begin(WIFI_SSID, WIFI_PASS); 
                }
            }
        }

        if (WiFi.status() == WL_CONNECTED && !mqtt.connected()) {
            mqtt.setServer(MQTT_HOST, MQTT_PORT);
            mqtt.setCallback(mqttCallbackStatic);
            
            String id = "ESP32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
            if (mqtt.connect(id.c_str())) {
                Serial.println(F("[MQTT] Conectado!"));
                subscribeTopics();
                digitalWrite(PIN_LED_Y, LOW); 
                if (!pendingExitSync) updateUI(); 
            }
        }
    }

    // Realiza a liberação forçada da sala (reset local).
    // Se a sala estava ocupada, salva o estado para tentar sincronizar a "saída" assim que a rede voltar (pendingExitSync).
    void resetLocalState() {
        prefs.begin("room", true);
        String savedUid = prefs.getString("uid", "");
        prefs.end();

        if (savedUid == "" && !pendingExitSync) {
            Serial.println(F("[SYS] Ignorado (Sala Vazia)"));
            rfid.PCD_Init();
            rfidWasMissing = true; // Força reinicialização do leitor
            return; 
        }

        Serial.println(F("[SYS] RESET: Liberando localmente e enfileirando sync..."));

        pendingExitUid = savedUid != "" ? savedUid : "FORCE_RESET"; 
        pendingExitSync = true; 

        prefs.begin("room", false);
        prefs.clear();
        prefs.end();

        rfid.PCD_Init();
        blinkLed(PIN_LED_G); 
        setLeds(); 
        
        lastSyncAttempt = 0; // Força tentativa imediata no próximo loop
    }

    // Monitora a conexão física do módulo RFID periodicamente (1s). Se falhar, acende LED amarelo e tenta reinicializar.
    void checkRfidHealth() {
        if (millis() - lastRfidCheck > 1000) {
            bool currentStatus = checkRfidHardware();
            
            if (!currentStatus) {
                if (!rfidWasMissing) {
                    digitalWrite(PIN_LED_Y, HIGH); 
                    rfidWasMissing = true;
                    rfid.PCD_Init();
                }
            } else {
                if (rfidWasMissing) {
                    rfid.PCD_Init(); 
                    if (WiFi.status() == WL_CONNECTED && mqtt.connected()) {
                        digitalWrite(PIN_LED_Y, LOW); 
                    }
                    rfidWasMissing = false;
                }
            }
            lastRfidCheck = millis();
        }
    }

    // Lê o registrador de versão do chip MFRC522 para validar se o hardware SPI está respondendo.
    bool checkRfidHardware() {
        byte v = rfid.PCD_ReadRegister(rfid.VersionReg);
        return (v != 0x00 && v != 0xFF);
    }

    // Processa todas as mensagens MQTT recebidas.
    // Roteia comandos (force-free, activate-creation) e respostas do servidor (sucesso/erro de entrada/saída).
    void handleMqtt(char* topicStr, byte* payload, unsigned int len) {
        String topic = String(topicStr);
        
        if (topic.endsWith("activate-creation-mode")) {
            currentMode = REGISTRATION; updateUI(); return;
        }
        if (topic.endsWith("force-free")) {
            resetLocalState(); return;
        }

        // Exibe mensagens de texto enviadas pelo servidor no LCD
        if (topic.endsWith("/message")) {
            char msgBuffer[len + 1];
            memcpy(msgBuffer, payload, len); msgBuffer[len] = '\0';
            StaticJsonDocument<256> msgDoc;
            if (!deserializeJson(msgDoc, msgBuffer)) {
                showLcd(msgDoc["l1"] | "Aviso:", msgDoc["l2"] | String(msgBuffer));
            } else {
                showLcd("Mensagem:", String(msgBuffer));
            }
            return; 
        }

        StaticJsonDocument<1024> doc;
        DeserializationError err = deserializeJson(doc, payload);

        if (err) {
            Serial.print(F("[JSON] Erro de parse: "));
            Serial.println(err.c_str());
            return;
        }

        const char* status = doc["status"];
        if (!status) status = doc["data"]["status"]; 
        if (!status) status = "error"; 

        const char* serverMsg = doc["message"];
        if (!serverMsg) serverMsg = doc["data"]["message"];

        bool success = (strcmp(status, "ok") == 0);
        
        if (success) {
            if (topic.indexOf("register-exit") > 0) {
                Serial.println(F("[SYNC] Saída confirmada pelo servidor!"));
                pendingExitSync = false; 
                prefs.begin("room", false); prefs.clear(); prefs.end(); 
                blinkLed(PIN_LED_G);
                
                if (serverMsg) showLcd("Sucesso", String(serverMsg));
                else showLcd("Sucesso", "Saida OK");
                delay(1500);
            }
            else if (topic.indexOf("register-entry") > 0) {
                const char* teacher = doc["data"]["teacher_name"];
                if (!teacher) teacher = doc["data"]["data"]["teacher_name"];
                if (!teacher) teacher = "Professor";

                char uidStr[10]; formatUid(lastUid, uidStr);
                prefs.begin("room", false);
                prefs.putString("uid", uidStr);
                prefs.putString("teacher", teacher);
                prefs.putULong("time", time(NULL));
                prefs.end();
                blinkLed(PIN_LED_G);
            } 
            else if (topic.indexOf("create-card") > 0) {
                currentMode = NORMAL;
                blinkLed(PIN_LED_G);
                showLcd("Cadastro OK", "Sucesso");
                delay(1500);
            }
        } 
        // ─── LÓGICA DE ERRO COM TEXTO CORRIDO ───
        else {
            blinkLed(PIN_LED_R);
            
            String errorMsg = "Falha";
            if (serverMsg) {
                errorMsg = String(serverMsg);
            } else {
                if (topic.indexOf("create-card") > 0) errorMsg = "Falha Cadastro";
                else errorMsg = "Acesso Negado";
            }

            // Formata e quebra o texto de erro para caber nas duas linhas do LCD
            String fullText = "Erro: " + errorMsg;
            fullText = sanitizeText(fullText);

            lcd.clear();
            
            // Linha 1: Primeiros 16 caracteres
            lcd.setCursor(0, 0); 
            lcd.print(fullText.substring(0, 16));
            
            // Linha 2: Do caractere 16 até o 32 (resto da frase)
            if (fullText.length() > 16) {
                lcd.setCursor(0, 1);
                lcd.print(fullText.substring(16, 32));
            }
            
            if (topic.indexOf("create-card") > 0) {
               currentMode = NORMAL;
               delay(2000);
            } else {
               delay(2500);
            }
        }

        waitingResponse = false;
        updateUI();
    }

    // Watchdog de software para requisições: se o servidor não responder em TIMEOUT_MS, libera o sistema.
    void checkTimeout() {
        if (waitingResponse && (millis() - requestStartTime > TIMEOUT_MS)) {
            Serial.println(F("[ERRO] Timeout na resposta."));
            showLcd("Sem Resposta", "Servidor Offline");
            blinkLed(PIN_LED_Y);
            waitingResponse = false;
            rfid.PCD_Init(); 
            delay(1500);
            updateUI();
        }
    }

    // Lógica principal do cartão RFID:
    // 1. Se modo cadastro -> envia Create Request.
    // 2. Se sala vazia -> envia Entry Request.
    // 3. Se sala ocupada pelo mesmo cartão -> envia Exit Request.
    // 4. Se sala ocupada por outro -> Acesso Negado local.
    void processCardLogic() {
        char uidStr[10]; formatUid(lastUid, uidStr);

        if (currentMode == REGISTRATION) {
            sendRequest("create-card/request", uidStr, true, true);
            showLcd("Cadastrando...", "Aguarde");
            return;
        }

        prefs.begin("room", true);
        String savedUid = prefs.getString("uid", "");
        prefs.end();

        if (savedUid == "") {
            sendRequest("register-entry/request", uidStr, false, true); 
            showLcd("Verificando...", "Aguarde");
        } 
        else if (savedUid == String(uidStr)) {
            sendRequest("register-exit/request", uidStr, false, true);
            showLcd("Saindo...", "Aguarde");
        } 
        else {
            blinkLed(PIN_LED_R);
            showLcd("OCUPADO!", "Outro Cartao");
            delay(2000);
            updateUI();
        }
    }

    // Inscreve-se nos tópicos MQTT relevantes para a ID da sala configurada.
    void subscribeTopics() {
        char topic[100];
        sprintf(topic, "classroom/%d/create-card/response", CLASS_ID); mqtt.subscribe(topic);
        sprintf(topic, "classroom/%d/register-entry/response", CLASS_ID); mqtt.subscribe(topic);
        sprintf(topic, "classroom/%d/register-exit/response", CLASS_ID); mqtt.subscribe(topic);
        sprintf(topic, "classroom/%d/activate-creation-mode", CLASS_ID); mqtt.subscribe(topic);
        sprintf(topic, "classroom/%d/force-free", CLASS_ID); mqtt.subscribe(topic);
        sprintf(topic, "classroom/%d/message", CLASS_ID); mqtt.subscribe(topic);
    }

    // Constrói o JSON da requisição e publica no tópico MQTT. Define flag para aguardar resposta.
    void sendRequest(const char* actionSuffix, const char* uid, bool isCreate, bool waitForReply) {
        StaticJsonDocument<512> doc;
        char timeStr[35]; getIsoTime(timeStr);
        
        if (isCreate) {
            doc["pattern"] = "create-card/request"; doc["data"]["uid"] = uid; 
        } else {
            if (String(actionSuffix).indexOf("entry") >= 0) {
                doc["pattern"] = "entry/register/request"; doc["data"]["entry_datetime"] = timeStr;
            } else {
                doc["pattern"] = "exit/register/request"; doc["data"]["exit_datetime"] = timeStr;
            }
            doc["data"]["classroom_id"] = CLASS_ID; doc["data"]["card_uid"] = uid;
        }

        char payload[512]; serializeJson(doc, payload);
        char topic[100]; sprintf(topic, "classroom/%d/%s", CLASS_ID, actionSuffix);
        
        if (mqtt.publish(topic, payload)) {
            if (waitForReply) {
                waitingResponse = true;
                requestStartTime = millis(); 
            }
        } else {
            // Se falhar o envio e não for uma sincronização de fundo, avisa o usuário.
            if (waitForReply && !pendingExitSync) { 
                showLcd("Erro MQTT", "Falha Envio");
                blinkLed(PIN_LED_Y); delay(1000); updateUI();
            }
        }
    }

    // Wrapper estático para permitir que a biblioteca PubSubClient chame o método da classe AccessControl.
    static void mqttCallbackStatic(char* topic, byte* payload, unsigned int length) {
        if (instance) instance->handleMqtt(topic, payload, length);
    }

    // Verifica botão físico com debounce de 2 segundos. Se pressionado, força reset da sala.
    void checkPhysicalButton() {
        if (digitalRead(PIN_BUTTON) == LOW) {
            if (millis() - lastButtonPress > 2000) {
                lastButtonPress = millis(); 
                Serial.println(F("[BTN] Pressionado."));
                resetLocalState();
                rfid.PCD_Init();
                rfidWasMissing = true;
            }
        }
    }

    // Sincroniza o relógio interno do ESP32 via NTP (Network Time Protocol).
    void syncTime() {
        configTime(GMT_OFFSET, 0, NTP_SERVER);
    }

    // Atualiza o display LCD e o estado dos LEDs com base no status atual (Livre/Ocupado/Modo Cadastro).
    void updateUI() {
        if (waitingResponse) return; 
        if (pendingExitSync) return; 

        setLeds();
        prefs.begin("room", true);
        String teacher = prefs.getString("teacher", "");
        time_t entryTime = prefs.getULong("time", 0);
        prefs.end();

        if (currentMode == REGISTRATION) {
            showLcd("MODO CADASTRO", "Aproxime Cartao");
        } else if (teacher != "") {
            struct tm* ti = localtime(&entryTime);
            char timeBuf[6];
            snprintf(timeBuf, 6, "%02d:%02d", ti->tm_hour, ti->tm_min);
            showLcd("OCUPADO: " + teacher, "Desde: " + String(timeBuf));
        } else {
            showLcd(ROOM_NAME, "LIVRE");
        }
    }

    // Helper: Limpa LCD e escreve duas linhas, sanitizando caracteres especiais.
    void showLcd(String l1, String l2) {
        lcd.clear(); 
        lcd.setCursor(0, 0); lcd.print(sanitizeText(l1).substring(0, 16));
        lcd.setCursor(0, 1); lcd.print(sanitizeText(l2).substring(0, 16));
    }

    // Helper: Pisca um LED 3 vezes (função bloqueante).
    void blinkLed(int pin) {
        digitalWrite(PIN_LED_G, LOW); digitalWrite(PIN_LED_R, LOW); digitalWrite(PIN_LED_Y, LOW);
        for(int i=0; i<3; i++) { digitalWrite(pin, HIGH); delay(100); digitalWrite(pin, LOW); delay(100); }
    }

    // Define quais LEDs devem ficar acesos estaticamente ( Verde=Livre, Vermelho=Ocupado, Amarelo=Erro/Offline).
    void setLeds() {
        digitalWrite(PIN_LED_G, LOW); digitalWrite(PIN_LED_R, LOW); digitalWrite(PIN_LED_W, LOW);
        
        if (!rfidWasMissing && WiFi.status() == WL_CONNECTED && mqtt.connected()) {
             digitalWrite(PIN_LED_Y, LOW);
        } else if (!pendingExitSync) {
             digitalWrite(PIN_LED_Y, HIGH);
        }

        if (currentMode == REGISTRATION) digitalWrite(PIN_LED_W, HIGH);
        else {
            prefs.begin("room", true);
            bool occupied = prefs.isKey("uid");
            prefs.end();
            if (pendingExitSync) digitalWrite(PIN_LED_G, HIGH);
            else digitalWrite(occupied ? PIN_LED_R : PIN_LED_G, HIGH);
        }
    }

    // Formata o array de bytes do UID para uma string hexadecimal.
    void formatUid(byte* uid, char* buffer) {
        sprintf(buffer, "%02X%02X%02X%02X", uid[0], uid[1], uid[2], uid[3]);
    }

    // Obtém a hora atual do sistema em formato ISO8601 para envio no JSON.
    void getIsoTime(char* buffer) {
        struct tm timeinfo;
        if(!getLocalTime(&timeinfo)) strcpy(buffer, "1970-01-01T00:00:00Z");
        else strftime(buffer, 35, "%Y-%m-%dT%H:%M:%S-03:00", &timeinfo);
    }

    // Comandos de depuração via Serial (C = Cadastro, R = Reset).
    void handleSerial(char cmd) {
        if (cmd == 'C') { currentMode = REGISTRATION; updateUI(); }
        if (cmd == 'R') { resetLocalState(); }
    }
};

AccessControl* AccessControl::instance = nullptr;
AccessControl sys;

void setup() { sys.init(); }
void loop() { sys.loop(); }