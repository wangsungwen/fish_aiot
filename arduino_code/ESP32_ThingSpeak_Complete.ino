/*
 * 專案：魚菜共生 AIoT - ESP32 上傳至 ThingSpeak 整合版
 * 說明：讀取 5 種感測器並上傳至 ThingSpeak 對應的 Field 1~5
 */

#include "ThingSpeak.h" // 請務必安裝 ThingSpeak 函式庫
#include <DallasTemperature.h>
#include <OneWire.h>
#include <WiFi.h>

// =================== 1. 網路與 ThingSpeak 設定 (請修改這裡)
// ===================
const char *ssid = "YOUR_WIFI_SSID";         // 請輸入 WiFi 名稱
const char *password = "YOUR_WIFI_PASSWORD"; // 請輸入 WiFi 密碼

unsigned long myChannelNumber = 3146597; // 您的 Channel ID
const char *myWriteAPIKey =
    "YX7R6GEYXQDDMWEZP"; // ⚠️ 請填入您的 Write API Key (不是 Read Key)

// =================== 2. 硬體腳位定義 ===================
const int PIN_TEMP = 4;         // DS18B20 水溫
const int PIN_TDS = 34;         // TDS (類比輸入)
const int PIN_TURBIDITY = 35;   // 濁度 (類比輸入)
const int PIN_PH = 32;          // PH (類比輸入)
const int PIN_WATER_LEVEL = 33; // Adafruit 4965 水位偵測
const int PIN_LED = 2;          // 狀態燈

// =================== 3. 物件初始化 ===================
WiFiClient client;
OneWire oneWire(PIN_TEMP);
DallasTemperature sensors(&oneWire);

// 變數儲存
float val_temp, val_tds, val_ph, val_turb, val_level;

void setup() {
  Serial.begin(115200);

  // 設定腳位
  pinMode(PIN_WATER_LEVEL, INPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_TDS, INPUT);
  pinMode(PIN_TURBIDITY, INPUT);
  pinMode(PIN_PH, INPUT);

  sensors.begin(); // 啟動溫度計

  // 連接 WiFi
  WiFi.mode(WIFI_STA);
  ThingSpeak.begin(client); // 初始化 ThingSpeak
}

void loop() {
  // 檢查網路連線
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("連線 WiFi 中: ");
    Serial.println(ssid);
    while (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid, password);
      delay(5000);
      Serial.print(".");
    }
    Serial.println("\nWiFi 已連線");
  }

  // 1. 讀取所有感測器數據
  readSensors();

  // 2. 設定 ThingSpeak 欄位 (Field)
  // 請確保您的 ThingSpeak Channel 已經開啟了對應的 Field 1-5
  ThingSpeak.setField(1, val_temp);  // Field 1: 溫度
  ThingSpeak.setField(2, val_tds);   // Field 2: TDS
  ThingSpeak.setField(3, val_ph);    // Field 3: PH
  ThingSpeak.setField(4, val_turb);  // Field 4: 濁度
  ThingSpeak.setField(5, val_level); // Field 5: 水位

  // 3. 上傳數據
  Serial.println("正在上傳數據至 ThingSpeak...");

  // writeFields 會一次把上面設定好的所有欄位送出
  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);

  if (x == 200) {
    Serial.println("✅ 上傳成功！");
    digitalWrite(PIN_LED, HIGH); // 亮燈表示成功
    delay(500);
    digitalWrite(PIN_LED, LOW);
  } else {
    Serial.print("❌ 上傳失敗，錯誤代碼: "); // 200 以外都是失敗
    Serial.println(x);
  }

  // ⚠️ 重要：ThingSpeak 免費版限制兩次上傳間隔至少 15 秒
  // 這裡設定 20 秒 (20000 毫秒) 以確保穩定
  Serial.println("等待 20 秒後進行下一次測量...");
  delay(20000);
}

// --- 讀取感測器副程式 ---
void readSensors() {
  // A. 溫度
  sensors.requestTemperatures();
  val_temp = sensors.getTempCByIndex(0);
  if (val_temp == -127.00)
    val_temp = 0.0;

  // B. 水位 (Adafruit 4965)
  // HIGH = 有水 (安全) -> 模擬為 20.0 cm
  // LOW  = 無水 (危險) -> 模擬為 0.0 cm
  val_level = digitalRead(PIN_WATER_LEVEL) == HIGH ? 20.0 : 0.0;

  // C. 類比讀取 (模擬轉換，請依實際校正參數修改)
  // TDS
  float raw_tds = analogRead(PIN_TDS);
  val_tds = (raw_tds * 3.3 / 4095.0) * 100; // 範例轉換

  // PH
  float raw_ph = analogRead(PIN_PH);
  val_ph = 7.0 + ((2.5 - (raw_ph * 3.3 / 4095.0)) / 0.18); // 範例轉換

  // 濁度
  float raw_turb = analogRead(PIN_TURBIDITY);
  val_turb = raw_turb * (3.3 / 4095.0); // 傳送電壓值

  // 監控顯示
  Serial.printf("T:%.1f, TDS:%.0f, PH:%.1f, Turb:%.1f, Lv:%.1f\n", val_temp,
                val_tds, val_ph, val_turb, val_level);
}