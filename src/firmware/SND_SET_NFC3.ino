
using ::byte;
#include <MFRC522.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <EEPROM.h>
#include "SPIFFS.h"
#include "Audio.h"
#include <ArduinoJson.h> // JSON解析用

// Web関連関数
void handleRoot();
void handleSave();
void handleQRConfig();
void handleStatus();
void setupWebServer();
void setupDNSServer();

// システム関連関数
void startConfigMode();
bool connectToWiFi();
void setup();
void loop();

String getConfigPageHTML();

// sendToMySQL関数内のJSON処理を修正
void sendToMySQL(String uid) {
  // WiFi接続チェック
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi未接続");
    return;
  }
  
  HTTPClient http;
  
// ユーティリティ関数
String getConfigPageHTML();
String urlEncode(String str);
void blinkLed(int pin, int times, int delayMs);
void playMP3(const char* filename);

// ピン定義
#define SS_PIN 5         // SDA接続ピン (RFID)
#define RST_PIN 22       // RST接続ピン (RFID)
#define STATUS_LED 2
#define REGISTER_BTN 15  // 登録モード切替用ボタン
#define IO0_BTN 0        // ESP32のIO0（BOOT）ボタン

// MAX98357 I2S ピン定義
#define I2S_BCLK 26      // I2S BCLK
#define I2S_LRC 25       // I2S WCLK/LRCK
#define I2S_DOUT 27      // I2S DOUT

// 定数
#define EEPROM_SIZE 512
#define AP_SSID "ESP32_RFID_Setup"
#define AP_PASS "12345678"
const char* MYSQL_URL = "https://bencougar4119-limts.com/earth8/mysql2.php";
const char* USER_REGISTER_URL = "https://bencougar4119-limts.com/earth8/user_register.php";
const char* SQL_KEY = "jh4zqs";
const char* HELLO_MP3_FILENAME = "/hello.mp3";
const char* BYE_MP3_FILENAME = "/bye.mp3";
const char* HELLO_MP3_URL = "https://bencougar4119-limts.com/earth8/mp3/hello.mp3"; // 入場MP3のダウンロードURL
const char* BYE_MP3_URL = "https://bencougar4119-limts.com/earth8/mp3/bye.mp3"; // 退場MP3のダウンロードURL

// リーダーモード定義
#define MODE_ATTENDANCE 0  // 園の入退室用
#define MODE_PLAY 1        // 遊び場所用

// リセットボタン関連
const unsigned long RESET_BUTTON_TIME = 5000; // リセットボタンを押す時間（ミリ秒）
unsigned long io0ButtonPressTime = 0;         // IO0ボタンが押された時間を記録
unsigned long registerButtonPressTime = 0;    // 登録ボタンが押された時間を記録

// DNSサーバー（キャプティブポータル用）
const uint8_t DNS_PORT = 53;
DNSServer dnsServer;

// Audio オブジェクト
Audio audio;

// グローバル変数
MFRC522 rfid(SS_PIN, RST_PIN);
WebServer server(80);
String chipId = "";
String wifiSsid = "";
String wifiPassword = "";
String locationName = ""; // 場所名
uint8_t readerMode = MODE_ATTENDANCE; // リーダーモード（デフォルトは入退室）
bool isConfigMode = false;
bool isRegisterMode = false; // 園児登録モード
bool spiffsInitialized = false; // SPIFFSファイルシステム初期化状態

// 前回のカードUID
uint8_t lastCardUID[4] = {0, 0, 0, 0};
unsigned long lastCardTime = 0;
const unsigned long cardTimeout = 3000; // 同じカードの再読み取り防止タイムアウト

//----- ユーティリティ関数群 -----

// 前回と同じカードかチェック
bool isSameCard(uint8_t *currentUID) {
  for (uint8_t i = 0; i < 4; i++) {
    if (currentUID[i] != lastCardUID[i]) {
      return false;
    }
  }
  return true;
}

// LEDを点滅
void blinkLed(int pin, int times, int delayMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(delayMs);
    digitalWrite(pin, LOW);
    delay(delayMs);
  }
}

// URL エンコード関数
String urlEncode(String str) {
  String encodedString = "";
  char c;
  char code0;
  char code1;
  
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encodedString += '+';
    } else if (isAlphaNumeric(c)) {
      encodedString += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) {
        code0 = c - 10 + 'A';
      }
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
  }
  return encodedString;
}

//----- EEPROM設定管理関数群 -----

// EEPROMに文字列を書き込む
void writeStringToEEPROM(int addrOffset, const String &strToWrite) {
  uint8_t len = strToWrite.length();
  EEPROM.write(addrOffset, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(addrOffset + 1 + i, strToWrite[i]);
  }
}

// EEPROMから文字列を読み込む
String readStringFromEEPROM(int addrOffset) {
  int newStrLen = EEPROM.read(addrOffset);
  char data[newStrLen + 1];
  for (int i = 0; i < newStrLen; i++) {
    data[i] = EEPROM.read(addrOffset + 1 + i);
  }
  data[newStrLen] = '\0';
  return String(data);
}

// 設定を保存
void saveConfig() {
  // 設定フラグを設定
  EEPROM.write(0, 0xAA);
  
  // 設定をEEPROMに書き込み
  writeStringToEEPROM(1, wifiSsid);
  writeStringToEEPROM(50, wifiPassword);
  writeStringToEEPROM(100, locationName);
  EEPROM.write(150, readerMode); // リーダーモードを保存
  
  EEPROM.commit();
  Serial.println("設定を保存しました");
}

// 設定を読み込み
void loadConfig() {
  wifiSsid = readStringFromEEPROM(1);
  wifiPassword = readStringFromEEPROM(50);
  locationName = readStringFromEEPROM(100);
  readerMode = EEPROM.read(150); // リーダーモードを読み込み
  
  // 値が範囲外の場合はデフォルト値に設定
  if (readerMode > MODE_PLAY) {
    readerMode = MODE_ATTENDANCE;
  }
  
  Serial.println("設定を読み込みました");
  Serial.println("SSID: " + wifiSsid);
  Serial.println("場所: " + locationName);
  Serial.println("モード: " + String(readerMode == MODE_ATTENDANCE ? "入退室" : "遊び場所"));
}

// ESP32の設定をクリア
void clearConfig() {
  Serial.println("設定をクリアしています...");
  
  // EEPROMをクリア（設定済みフラグとすべての設定）
  EEPROM.write(0, 0); // 設定済みフラグをクリア
  
  // WiFi情報を明示的にクリア
  for (int i = 1; i < 150; i++) {
    EEPROM.write(i, 0);
  }
  
  // 設定をリセット
  wifiSsid = "";
  wifiPassword = "";
  locationName = "";
  
  // 変更を保存
  EEPROM.commit();
  
  // LED点滅でクリア完了を通知
  for (int i = 0; i < 10; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(100);
    digitalWrite(STATUS_LED, LOW);
    delay(100);
  }
  
  Serial.println("設定が完全にクリアされました。再起動します...");
  delay(1000);
  ESP.restart();  // 再起動
}

//----- SPIFFS/MP3管理関数群 -----

// MP3をサーバーからダウンロードしてSPIFFSに保存する関数
bool downloadMP3File(const char* url, const char* filename) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi未接続のためMP3をダウンロードできません");
    return false;
  }
  
  Serial.println("MP3ファイルをダウンロード中...");
  Serial.println(url);
  
  HTTPClient http;
  http.begin(url);
  
  // GETリクエスト送信
  int httpResponseCode = http.GET();
  
  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
    
    // ファイルサイズを取得
    int fileSize = http.getSize();
    Serial.print("ファイルサイズ: ");
    Serial.print(fileSize);
    Serial.println(" バイト");
    
    if (fileSize <= 0) {
      Serial.println("ファイルサイズが不正です");
      http.end();
      return false;
    }
    
    // SPIFFSにファイルを作成
    File file = SPIFFS.open(filename, FILE_WRITE);
    if (!file) {
      Serial.println("ファイルを作成できませんでした");
      http.end();
      return false;
    }
    
    // WiFiClientからストリームを取得
    WiFiClient* stream = http.getStreamPtr();
    
    // バッファサイズを設定（ESP32のメモリに応じて調整）
    const size_t bufferSize = 1024;
    uint8_t buffer[bufferSize];
    
    // ダウンロードの進捗を示すLED点滅
    digitalWrite(STATUS_LED, HIGH);
    
    // データを読み込んでファイルに書き込む
    int totalBytes = 0;
    int bytesWritten = 0;
    
    while (http.connected() && (totalBytes < fileSize)) {
      // データが利用可能かチェック
      size_t size = stream->available();
      if (size > 0) {
        // バッファサイズを超えないようにする
        size_t readBytes = min(size, bufferSize);
        
        // データを読み込む
        int c = stream->readBytes(buffer, readBytes);
        
        // ファイルに書き込む
        bytesWritten = file.write(buffer, c);
        totalBytes += c;
        
        // LEDを点滅させて進捗を示す
        if (totalBytes % (bufferSize * 5) == 0) {
          digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
        }
        
        // 進捗をシリアルに出力
        if (totalBytes % (bufferSize * 10) == 0) {
          Serial.print("ダウンロード進捗: ");
          Serial.print(totalBytes);
          Serial.print("/");
          Serial.print(fileSize);
          Serial.print(" (");
          Serial.print((totalBytes * 100) / fileSize);
          Serial.println("%)");
        }
      }
      delay(1);
    }
    
    // ファイルを閉じる
    file.close();
    
    Serial.print("合計 ");
    Serial.print(totalBytes);
    Serial.println(" バイトをダウンロードしました");
    
    digitalWrite(STATUS_LED, LOW);
    
    // ダウンロードが完了したことを示すLED点滅
    blinkLed(STATUS_LED, 5, 100);
    
    http.end();
    
    // ダウンロードの検証
    if (totalBytes == fileSize) {
      Serial.println("MP3ファイルのダウンロードに成功しました");
      return true;
    } else {
      Serial.println("ダウンロードしたファイルのサイズが一致しません");
      // 不完全なファイルを削除
      SPIFFS.remove(filename);
      return false;
    }
  } else {
    Serial.print("MP3ダウンロードエラー: HTTP ");
    Serial.println(httpResponseCode);
    http.end();
    return false;
  }
}

// SPIFFSのファイル一覧を表示（デバッグ用）
void showSPIFFSFiles() {
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  Serial.println("SPIFFS内のファイル一覧:");
  while(file) {
    Serial.print("  ");
    Serial.print(file.name());
    Serial.print(" (");
    Serial.print(file.size());
    Serial.println("バイト)");
    file = root.openNextFile();
  }
}

// SPIFFSの初期化と必要に応じてMP3ファイルのダウンロード
bool initSPIFFS() {
  Serial.println("SPIFFSを初期化中...");
  
  if (!SPIFFS.begin(true)) { // trueを指定するとフォーマットも行う
    Serial.println("SPIFFSのマウントに失敗しました");
    return false;
  }
  
  Serial.println("SPIFFS初期化成功");
  
  // MP3ファイルの存在確認
  bool helloExists = SPIFFS.exists(HELLO_MP3_FILENAME);
  bool byeExists = SPIFFS.exists(BYE_MP3_FILENAME);
  
  if (!helloExists || !byeExists) {
    Serial.println("MP3ファイルが見つかりません");
    
    // WiFi接続チェック
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("WiFi接続中のため、MP3ファイルをダウンロードします");
      
      // MP3ファイルをダウンロード
      bool helloSuccess = true;
      bool byeSuccess = true;
      
      if (!helloExists) {
        helloSuccess = downloadMP3File(HELLO_MP3_URL, HELLO_MP3_FILENAME);
      }
      
      if (!byeExists) {
        byeSuccess = downloadMP3File(BYE_MP3_URL, BYE_MP3_FILENAME);
      }
      
      if (helloSuccess && byeSuccess) {
        Serial.println("MP3ファイルのダウンロードに成功しました");
      } else {
        Serial.println("MP3ファイルのダウンロードに失敗しました");
        
        // SPIFFSの内容をリスト表示（デバッグ用）
        showSPIFFSFiles();
        
        return false;
      }
    } else {
      Serial.println("WiFi未接続のため、MP3ファイルをダウンロードできません");
      
      // SPIFFSの内容をリスト表示（デバッグ用）
      showSPIFFSFiles();
      
      return false;
    }
  } else {
    Serial.println("MP3ファイルを確認しました");
  }
  
  return true;
}

// MP3ファイルの存在確認と必要に応じたダウンロード
bool checkAndDownloadMP3() {
  // SPIFFSが初期化されているか確認
  if (!spiffsInitialized) {
    Serial.println("SPIFFSが初期化されていないためMP3の確認ができません");
    return false;
  }
  
  // MP3ファイルの存在を確認
  bool helloExists = SPIFFS.exists(HELLO_MP3_FILENAME);
  bool byeExists = SPIFFS.exists(BYE_MP3_FILENAME);
  
  Serial.print("入場MP3ファイルの存在確認: ");
  Serial.println(helloExists ? "存在します" : "存在しません");
  Serial.print("退場MP3ファイルの存在確認: ");
  Serial.println(byeExists ? "存在します" : "存在しません");
  
  // ファイルが存在しない場合、またはアップデートが必要な場合にダウンロード
  bool success = true;
  
  if (WiFi.status() == WL_CONNECTED) {
    if (!helloExists) {
      Serial.println("入場MP3ファイルをダウンロードします");
      success = downloadMP3File(HELLO_MP3_URL, HELLO_MP3_FILENAME) && success;
    }
    
    if (!byeExists) {
      Serial.println("退場MP3ファイルをダウンロードします");
      success = downloadMP3File(BYE_MP3_URL, BYE_MP3_FILENAME) && success;
    }
  } else {
    Serial.println("WiFi未接続のためMP3をダウンロードできません");
    return false;
  }
  
  return success;
}

// MP3ファイルを強制的にアップデート
bool forceUpdateMP3() {
  // SPIFFSが初期化されているか確認
  if (!spiffsInitialized) {
    Serial.println("SPIFFSが初期化されていないためMP3の更新ができません");
    return false;
  }
  
  // 既存のMP3ファイルを削除
  if (SPIFFS.exists(HELLO_MP3_FILENAME)) {
    Serial.println("既存の入場MP3ファイルを削除します");
    SPIFFS.remove(HELLO_MP3_FILENAME);
  }
  
  if (SPIFFS.exists(BYE_MP3_FILENAME)) {
    Serial.println("既存の退場MP3ファイルを削除します");
    SPIFFS.remove(BYE_MP3_FILENAME);
  }
  
  // 新しいMP3ファイルをダウンロード
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("新しいMP3ファイルをダウンロードします");
    bool helloSuccess = downloadMP3File(HELLO_MP3_URL, HELLO_MP3_FILENAME);
    bool byeSuccess = downloadMP3File(BYE_MP3_URL, BYE_MP3_FILENAME);
    return helloSuccess && byeSuccess;
  } else {
    Serial.println("WiFi未接続のためMP3をダウンロードできません");
    return false;
  }
}

//----- オーディオ関連関数群 -----

// オーディオの初期化
bool initAudio() {
  Serial.println("オーディオを初期化中...");
  
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT);
  audio.setVolume(21); // 0~21の間で音量を設定
  
  Serial.println("オーディオ初期化成功");
  return true;
}

// MP3ファイルの再生
void playMP3(const char* filename) {
  if (!spiffsInitialized) {
    Serial.println("SPIFFSが初期化されていないため再生できません");
    return;
  }
  
  if (!SPIFFS.exists(filename)) {
    Serial.print("MP3ファイルが存在しません: ");
    Serial.println(filename);
    return;
  }
  
  Serial.println("MP3ファイル再生中: " + String(filename));
  audio.connecttoFS(SPIFFS, filename); // SPIFFSからファイルを読み込み
  
  // 再生が終わるのを待つためのループ
  // 実際のアプリケーションでは、非ブロッキング方式で実装することを検討してください
  unsigned long startTime = millis();
  while (audio.isRunning() && (millis() - startTime < 10000)) { // 最大10秒間待機
    audio.loop();
    delay(1);
  }
  
  Serial.println("MP3ファイル再生完了");
}

//----- サーバー通信関連関数群 -----

// MySQLサーバーにデータを送信し、応答に基づいてMP3を再生
void sendToMySQL(String uid) {
  // WiFi接続チェック
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi未接続");
    return;
  }
  
  HTTPClient http;
  
  // サーバーURL
  http.begin(MYSQL_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  // モードに応じたテーブル名を設定
  String tableName = (readerMode == MODE_ATTENDANCE) ? "attendance" : "play";
  
  // SQLクエリの作成 - flgフィールドはサーバーが決定するため、ここでは設定しない
  String sqlQuery = "INSERT INTO " + tableName + " (uid, 名前, 時間, 場所, アクション, 予備1, 予備2) VALUES ('" + 
                    uid + "', '', NOW(), '" + locationName + "', '', '', '')";
  
  // エンコードされたクエリとキー
  // モード情報も送信
  String postData = "query=" + urlEncode(sqlQuery) + "&key=" + SQL_KEY + "&uid=" + uid + 
                   "&location=" + urlEncode(locationName) + "&mode=" + String(readerMode) + 
                   "&table=" + tableName;
  
  Serial.println("SQLクエリ送信: " + sqlQuery);
  Serial.println("モード: " + String(readerMode == MODE_ATTENDANCE ? "入退室" : "遊び場所"));
  Serial.println("POST データ: " + postData);
  
  // POSTリクエスト送信
  int httpResponseCode = http.POST(postData);
  
  // レスポンス処理
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("HTTP Response: " + String(httpResponseCode));
    Serial.println(response);
    
    // JSON応答を解析して入場/退場を判断
    DynamicJsonDocument doc(1024);
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
      Serial.print("JSON解析エラー: ");
      Serial.println(error.c_str());
      // エラーを示すLED点滅
      blinkLed(STATUS_LED, 3, 200);
    } else {
      String status = doc["status"].as<String>();
      String action = doc["action"].as<String>();
      Serial.print("サーバーからのステータス: ");
      Serial.println(status);
      Serial.print("サーバーからのアクション: ");
      Serial.println(action);
      
      if (status == "success") {
        // 入場/退場に応じたMP3を再生
        if (action == "entry") {
          // 入場の場合
          playMP3(HELLO_MP3_FILENAME);
        } else if (action == "exit") {
          // 退場の場合
          playMP3(BYE_MP3_FILENAME);
        }
        
        // 成功を示すLED点灯
        digitalWrite(STATUS_LED, HIGH);
        delay(500);
        digitalWrite(STATUS_LED, LOW);
      } else {
        // エラーを示すLED点滅
        blinkLed(STATUS_LED, 2, 300);
      }
    }
  } else {
    Serial.println("エラー: HTTP " + String(httpResponseCode));
    // エラーを示すLED点滅
    blinkLed(STATUS_LED, 5, 50);
  }
  
  http.end();
}

// ユーザー登録サーバーにカードUIDを送信
void sendToUserRegistration(String uid) {
  // WiFi接続チェック
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi未接続");
    return;
  }
  
  HTTPClient http;
  
  // サーバーURL
  http.begin(MYSQL_URL);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  // モードに応じたテーブル名を設定
  String tableName = (readerMode == MODE_ATTENDANCE) ? "attendance" : "play";
  
  // SQLクエリの作成 - flgフィールドはサーバーが決定するため、ここでは設定しない
  String sqlQuery = "INSERT INTO " + tableName + " (uid, 名前, 時間, 場所, アクション, 予備1, 予備2) VALUES ('" + 
                    uid + "', '', NOW(), '" + locationName + "', '', '', '')";
  
  // エンコードされたクエリとキー
  // モード情報も送信
  String postData = "query=" + urlEncode(sqlQuery) + "&key=" + SQL_KEY + "&uid=" + uid + 
                   "&location=" + urlEncode(locationName) + "&mode=" + String(readerMode) + 
                   "&table=" + tableName;
  
  Serial.println("SQLクエリ送信: " + sqlQuery);
  Serial.println("モード: " + String(readerMode == MODE_ATTENDANCE ? "入退室" : "遊び場所"));
  Serial.println("POST データ: " + postData);
  
  // POSTリクエスト送信
  int httpResponseCode = http.POST(postData);
  
  // レスポンス処理
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("HTTP Response: " + String(httpResponseCode));
    Serial.println(response);
    
    // JSON応答を解析して入場/退場を判断
    JsonDocument doc; // DynamicJsonDocumentの代わりにJsonDocumentを使用
    DeserializationError error = deserializeJson(doc, response);
    
    if (error) {
      Serial.print("JSON解析エラー: ");
      Serial.println(error.c_str());
      // エラーを示すLED点滅
      blinkLed(STATUS_LED, 3, 200);
    } else {
      String status = doc["status"].as<String>();
      String action = doc["action"].as<String>();
      Serial.print("サーバーからのステータス: ");
      Serial.println(status);
      Serial.print("サーバーからのアクション: ");
      Serial.println(action);
      
      if (status == "success") {
        // 入場/退場に応じたMP3を再生
        if (action == "entry") {
          // 入場の場合
          playMP3(HELLO_MP3_FILENAME);
        } else if (action == "exit") {
          // 退場の場合
          playMP3(BYE_MP3_FILENAME);
        }
        
        // 成功を示すLED点灯
        digitalWrite(STATUS_LED, HIGH);
        delay(500);
        digitalWrite(STATUS_LED, LOW);
      } else {
        // エラーを示すLED点滅
        blinkLed(STATUS_LED, 2, 300);
      }
    }
  } else {
    Serial.println("エラー: HTTP " + String(httpResponseCode));
    // エラーを示すLED点滅
    blinkLed(STATUS_LED, 5, 50);
  }
  
  http.end();
}

//----- Webサーバー関連関数群 -----

// MP3更新用のWebハンドラ
void handleMP3Update() {
  // 簡単な認証（オプション）- セキュリティを向上させるためパスワード保護することも検討
  if (server.hasArg("key") && server.arg("key") == SQL_KEY) {
    Serial.println("MP3更新リクエストを受信しました");
    
    // 強制アップデートフラグをチェック
    bool forceUpdate = false;
    if (server.hasArg("force")) {
      forceUpdate = (server.arg("force") == "1" || server.arg("force").equalsIgnoreCase("true"));
    }
    
    bool success = false;
    if (forceUpdate) {
      success = forceUpdateMP3();
    } else {
      success = checkAndDownloadMP3();
    }
    
    if (success) {
      server.send(200, "text/plain", "MP3ファイルの更新に成功しました");
    } else {
      server.send(500, "text/plain", "MP3ファイルの更新に失敗しました");
    }
  } else {
    server.send(403, "text/plain", "アクセス拒否: 無効なキー");
  }
}

// MP3情報の取得用Webハンドラ
void handleMP3Info() {
  String info = "";
  
  // SPIFFSの状態確認
  if (!spiffsInitialized) {
    info = "SPIFFSが初期化されていません";
    server.send(500, "text/plain", info);
    return;
  }
  
  // MP3ファイルの存在確認
  bool helloExists = SPIFFS.exists(HELLO_MP3_FILENAME);
  bool byeExists = SPIFFS.exists(BYE_MP3_FILENAME);
  
  info = "SPIFFS MP3ファイル情報:\n\n";
  
  if (helloExists) {
    // ファイル情報を取得
    File mp3File = SPIFFS.open(HELLO_MP3_FILENAME, FILE_READ);
    if (mp3File) {
      int fileSize = mp3File.size();
      mp3File.close();
      
      info += "入場MP3: " + String(HELLO_MP3_FILENAME) + "\n";
      info += "ステータス: 存在します\n";
      info += "サイズ: " + String(fileSize) + " バイト\n\n";
    } else {
      info += "入場MP3: ファイルを開けませんでした\n\n";
    }
  } else {
    info += "入場MP3: 存在しません\n\n";
  }
  
  if (byeExists) {
    // ファイル情報を取得
    File mp3File = SPIFFS.open(BYE_MP3_FILENAME, FILE_READ);
    if (mp3File) {
      int fileSize = mp3File.size();
      mp3File.close();
      
      info += "退場MP3: " + String(BYE_MP3_FILENAME) + "\n";
      info += "ステータス: 存在します\n";
      info += "サイズ: " + String(fileSize) + " バイト";
    } else {
      info += "退場MP3: ファイルを開けませんでした";
    }
  } else {
    info += "退場MP3: 存在しません";
  }
  
  server.send(200, "text/plain", info);
}

// QRコードからのパラメータ設定を処理するハンドラ
void handleQRConfig() {
  Serial.println("QRコード設定リクエスト受信");
  
  // URLパラメータを取得
  String ssid = server.arg("id");
  String password = server.arg("pw");
  String location = server.arg("pl");
  String mode = server.arg("mode");
  
  // デバッグ出力
  Serial.println("受信パラメータ:");
  Serial.println("SSID: " + ssid);
  Serial.println("パスワード: ***");
  Serial.println("場所: " + location);
  Serial.println("モード: " + mode);
  
  // パラメータが存在するか確認
  bool hasParams = false;
  
  // SSIDパラメータ処理
  if (ssid.length() > 0) {
    wifiSsid = ssid;
    hasParams = true;
  }
  
  // パスワードパラメータ処理
  if (password.length() > 0) {
    wifiPassword = password;
    hasParams = true;
  }
  
  // 場所パラメータ処理
  if (location.length() > 0) {
    locationName = location;
    hasParams = true;
  }
  
  // モードパラメータ処理（文字列または数値に対応）
  if (mode.length() > 0) {
    uint8_t modeValue;
    if (mode == "MODE_ATTENDANCE" || mode == "0") {
      modeValue = MODE_ATTENDANCE;
    } else if (mode == "MODE_PLAY" || mode == "1") {
      modeValue = MODE_PLAY;
    } else {
      modeValue = mode.toInt(); // デフォルトの変換
    }
    
    // モードが有効な値の場合のみ設定
    if (modeValue <= MODE_PLAY) {
      readerMode = modeValue;
      hasParams = true;
    }
  }
  
  // パラメータがあれば設定を保存
  if (hasParams) {
    // 設定を保存
    saveConfig();
    
    // レスポンスHTMLを生成（自動登録と再起動を促すページ）
    String html = "<!DOCTYPE html><html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<meta charset='UTF-8'>";
    html += "<style>";
    html += "body{font-family:Arial,sans-serif;margin:0;padding:20px;line-height:1.6;background:#f5f5f5;text-align:center;}";
    html += ".container{max-width:500px;margin:0 auto;background:white;padding:20px;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1);}";
    html += "h1{color:#333;margin-top:0;}";
    html += ".success{color:#4CAF50;font-weight:bold;}";
    html += "</style>";
    html += "<title>設定保存</title>";
    html += "</head><body>";
    html += "<div class='container'>";
    html += "<h1>設定が保存されました</h1>";
    html += "<p class='success'>設定を自動的に適用しています...</p>";
    html += "<script>";
    html += "window.onload = function() {";
    html += "  setTimeout(function() {";
    html += "    window.location.href = 'about:blank';"; // ページを閉じる
    html += "    window.close();"; // 可能であればウィンドウを閉じる
    html += "  }, 2000);";
    html += "};";
    html += "</script>";
    html += "</div>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
    
    // 少し待ってから再起動
    delay(3000);
    ESP.restart();
  } else {
    // パラメータがない場合はエラーメッセージ
    server.send(400, "text/plain", "無効なパラメータです");
  }

// 設定の保存を処理
void handleSave() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  wifiSsid = server.arg("ssid");
  wifiPassword = server.arg("password");
  locationName = server.arg("location");
  
  // モード設定を取得
  if (server.hasArg("mode")) {
    uint8_t modeValue = server.arg("mode").toInt();
    if (modeValue <= MODE_PLAY) {
      readerMode = modeValue;
    }
  }
  
  // 設定を保存
  saveConfig();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<meta charset='UTF-8'>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:0;padding:20px;line-height:1.6;background:#f5f5f5;text-align:center;}";
  html += ".container{max-width:500px;margin:0 auto;background:white;padding:20px;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1);}";
  html += "h1{color:#333;margin-top:0;}";
  html += ".success{color:#4CAF50;font-weight:bold;}";
  html += "</style>";
  html += "<meta http-equiv='refresh' content='5;url=/'>";
  html += "<title>設定保存</title></head><body>";
  html += "<div class='container'>";
  html += "<h1>設定が保存されました</h1>";
  html += "<p class='success'>設定を保存しました。デバイスは数秒後に再起動します。</p>";
  if (wifiSsid.length() > 0) {
    html += "<p>WiFi SSID: " + wifiSsid + "</p>";
  }
  if (locationName.length() > 0) {
    html += "<p>場所名: " + locationName + "</p>";
  }
  html += "<p>モード: " + String(readerMode == MODE_ATTENDANCE ? "入退室" : "遊び場所") + "</p>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
  
  // 設定を保存したら少し待ってから再起動
  delay(3000);
  ESP.restart();
}

// Webサーバーをセットアップ
void setupWebServer() {
  // ルートとステータスページのハンドラを設定
  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/qr", handleQRConfig);
  server.on("/status", handleStatus);
  server.on("/mp3/update", handleMP3Update);
  server.on("/mp3/info", handleMP3Info);
  
  // 404 Not Foundハンドラ
  server.onNotFound([]() {
    server.sendHeader("Location", "/", true);
    server.send(302, "text/plain", "");
  });
  
  // Webサーバーを開始
  server.begin();
  Serial.println("Webサーバーを開始しました");
}

// キャプティブポータル用のDNSサーバーをセットアップ
void setupDNSServer() {
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  Serial.println("DNSサーバーを開始しました");
}

//----- メイン処理関数群 -----

// 設定モードを開始する関数
void startConfigMode() {
  Serial.println("設定モードを開始します");
  isConfigMode = true;
  
  // AP（アクセスポイント）モードで起動
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  
  Serial.print("AP IPアドレス: ");
  Serial.println(WiFi.softAPIP());
  
  // DNSサーバー（キャプティブポータル）をセットアップ
  setupDNSServer();
  
  // Webサーバーをセットアップ
  setupWebServer();
  
  // 設定モード開始を示すLED点滅
  blinkLed(STATUS_LED, 3, 200);
}

// WiFiに接続する関数
bool connectToWiFi() {
  if (wifiSsid.length() == 0) {
    Serial.println("WiFi SSIDが設定されていません");
    return false;
  }
  
  Serial.print("WiFiに接続中: ");
  Serial.println(wifiSsid);
  
  // WiFiを接続モードに設定
  WiFi.mode(WIFI_STA);
  
  // WiFiに接続
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  
  // 接続を最大20秒間待機
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 20000) {
    delay(500);
    Serial.print(".");
    // 接続中はLEDを点滅
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
  }
  
  // 接続状態をチェック
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("WiFi接続完了");
    Serial.print("IPアドレス: ");
    Serial.println(WiFi.localIP());
    
    // 接続成功を示すLED点滅
    blinkLed(STATUS_LED, 2, 100);
    return true;
  } else {
    Serial.println("");
    Serial.println("WiFi接続に失敗しました");
    
    // 接続失敗を示すLED点滅
    blinkLed(STATUS_LED, 5, 50);
    return false;
  }
}

// 初期化処理
void setup() {
  // シリアル通信の初期化
  Serial.begin(115200);
  Serial.println("\n\nESP32 RFID Systemを起動中...");
  
  // GPIO設定
  pinMode(STATUS_LED, OUTPUT);
  pinMode(REGISTER_BTN, INPUT_PULLUP);
  pinMode(IO0_BTN, INPUT_PULLUP);
  
  // 起動時にLEDを点灯
  digitalWrite(STATUS_LED, HIGH);
  
  // チップIDを取得（デバイス識別用）
  uint64_t chipid = ESP.getEfuseMac();
  chipId = String((uint16_t)(chipid >> 32), HEX) + String((uint32_t)chipid, HEX);
  chipId.toUpperCase();
  Serial.print("チップID: ");
  Serial.println(chipId);
  
  // EEPROMの初期化
  if (!EEPROM.begin(EEPROM_SIZE)) {
    Serial.println("EEPROMの初期化に失敗しました");
    // エラーを示すLED点滅
    while (true) {
      blinkLed(STATUS_LED, 3, 100);
      delay(1000);
    }
  }
  
  // IO0ボタンが押されているかチェック（リセット用）
  if (digitalRead(IO0_BTN) == LOW) {
    Serial.println("IO0ボタンが押されています。5秒間押し続けると設定がリセットされます。");
    unsigned long startTime = millis();
    
    // ボタンが離されるまでまたは5秒経過するまで待機
    while (digitalRead(IO0_BTN) == LOW) {
      if (millis() - startTime > RESET_BUTTON_TIME) {
        Serial.println("5秒経過しました。設定をリセットします。");
        clearConfig();
        break;
      }
      delay(100);
      // 待機中はLEDを点滅
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
    }
  }
  
  // 設定が保存されているかチェック
  if (EEPROM.read(0) == 0xAA) {
    Serial.println("保存された設定を読み込みます");
    loadConfig();
    
    // WiFiに接続
    if (connectToWiFi()) {
      // WiFi接続成功
    } else {
      // WiFi接続失敗 - 設定モードを開始
      startConfigMode();
    }
  } else {
    Serial.println("保存された設定がありません。設定モードを開始します");
    startConfigMode();
  }
  
  // WiFiに接続できたら、通常モードを初期化
  if (!isConfigMode) {
    // RFIDリーダーを初期化
    SPI.begin();
    rfid.PCD_Init();
    Serial.println("RFIDリーダーを初期化しました");
    
    // SPIFFSを初期化（MP3ファイル用）
    spiffsInitialized = initSPIFFS();
    
    // オーディオを初期化
    if (spiffsInitialized) {
      initAudio();
      
      // MP3ファイルの存在を確認し、ダウンロード
      checkAndDownloadMP3();
    }
    
    // 通常モードでもWebサーバーを起動（リモート設定用）
    setupWebServer();
    
    Serial.println("システム初期化完了");
    // 初期化完了を示すLED点滅
    blinkLed(STATUS_LED, 3, 100);
  }
}

// メインループ処理
void loop() {
  // 設定モードの場合
  if (isConfigMode) {
    // DNSサーバーとWebサーバーを処理
    dnsServer.processNextRequest();
    server.handleClient();
  } else {
    // 通常モード
    
    // Webサーバーリクエストを処理
    server.handleClient();
    
    // オーディオ処理
    audio.loop();
    
    // 登録ボタンのチェック
    if (digitalRead(REGISTER_BTN) == LOW) {
      if (registerButtonPressTime == 0) {
        registerButtonPressTime = millis();
      } else if (millis() - registerButtonPressTime > 3000) {
        // 3秒以上ボタンが押されていたらモード切替
        isRegisterMode = !isRegisterMode;
        Serial.print("登録モードを ");
        Serial.println(isRegisterMode ? "オン" : "オフ");
        
        // モード切替を示すLED点滅
        if (isRegisterMode) {
          blinkLed(STATUS_LED, 5, 100);
          digitalWrite(STATUS_LED, HIGH);  // 登録モードではLEDを点灯したままにする
        } else {
          blinkLed(STATUS_LED, 2, 100);
          digitalWrite(STATUS_LED, LOW);   // 通常モードではLEDを消灯
        }
        
        // 次のボタン押下まで待機
        registerButtonPressTime = 0;
        
        // ボタンが離されるまで待機
        while (digitalRead(REGISTER_BTN) == LOW) {
          delay(10);
        }
      }
    } else {
      registerButtonPressTime = 0;
    }
    
    // IO0ボタンのチェック（リセット用）
    if (digitalRead(IO0_BTN) == LOW) {
      if (io0ButtonPressTime == 0) {
        io0ButtonPressTime = millis();
      } else if (millis() - io0ButtonPressTime > RESET_BUTTON_TIME) {
        // 5秒以上ボタンが押されていたら設定リセット
        Serial.println("IO0ボタンが5秒以上押されました。設定をリセットします。");
        clearConfig();
      }
    } else {
      io0ButtonPressTime = 0;
    }
    
    // RFIDカードをチェック
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      // カードのUIDを取得
      String uid = "";
      for (uint8_t i = 0; i < rfid.uid.size; i++) {
        uid += (rfid.uid.uidByte[i] < 0x10 ? "0" : "") + String(rfid.uid.uidByte[i], HEX);
      }
      uid.toUpperCase();
      
      // 前回と同じカードで、タイムアウト内ならスキップ
      bool sameCard = isSameCard(rfid.uid.uidByte);
      unsigned long currentTime = millis();
      
      if (sameCard && (currentTime - lastCardTime < cardTimeout)) {
        Serial.println("同じカードが短時間で読み取られました（スキップ）");
      } else {
        // 現在のカードUIDを保存
        memcpy(lastCardUID, rfid.uid.uidByte, 4);
        lastCardTime = currentTime;
        
        Serial.print("カードUID: ");
        Serial.println(uid);
        
        // 登録モードとアクション
        if (isRegisterMode) {
          // 登録モードの場合はユーザー登録サーバーに送信
          sendToUserRegistration(uid);
        } else {
          // 通常モードの場合はMySQLに送信
          sendToMySQL(uid);
        }
      }
      
      // カード読み取り完了
      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }
    
    // 少し待機
    delay(50);
  }
}

// 設定画面のHTMLを生成
String getConfigPageHTML() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<meta charset='UTF-8'>";
  html += "<title>ESP32 RFID設定</title>";
  html += "<style>";
  html += "body{font-family:Arial,sans-serif;margin:0;padding:20px;line-height:1.6;background:#f5f5f5;}";
  html += ".container{max-width:500px;margin:0 auto;background:white;padding:20px;border-radius:5px;box-shadow:0 2px 5px rgba(0,0,0,0.1);}";
  html += "h1{color:#333;margin-top:0;}";
  html += ".form-group{margin-bottom:15px;}";
  html += "label{display:block;margin-bottom:5px;font-weight:bold;}";
  html += "input[type='text'],input[type='password'],select{width:100%;padding:8px;box-sizing:border-box;border:1px solid #ddd;border-radius:4px;}";
  html += "button{background:#4CAF50;color:white;border:none;padding:10px 15px;border-radius:4px;cursor:pointer;}";
  html += "button:hover{background:#45a049;}";
  html += ".note{font-size:0.9em;color:#666;margin-top:20px;}";
  html += ".status{margin-top:20px;padding:10px;background:#f8f8f8;border-left:4px solid #4CAF50;}";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='container'>";
  html += "<h1>ESP32 RFID設定</h1>";
  
  // チップIDと接続状態を表示
  html += "<div class='status'>";
  html += "<p><strong>チップID:</strong> " + chipId + "</p>";
  html += "<p><strong>WiFi状態:</strong> ";
  if (WiFi.status() == WL_CONNECTED) {
    html += "接続済み (SSID: " + WiFi.SSID() + ", IP: " + WiFi.localIP().toString() + ")";
  } else {
    html += "未接続";
  }
  html += "</p>";
  
  // 現在の設定がある場合は表示
  if (locationName.length() > 0) {
    html += "<p><strong>現在の設定:</strong> ";
    if (locationName.length() > 0) {
      html += "場所: " + locationName + " ";
    }
    html += "モード: " + String(readerMode == MODE_ATTENDANCE ? "入退室" : "遊び場所");
    html += "</p>";
  }
  html += "</div>";
  
  // 設定フォーム
  html += "<form action='/save' method='post'>";
  html += "<div class='form-group'>";
  html += "<label for='ssid'>WiFi SSID:</label>";
  html += "<input type='text' id='ssid' name='ssid' value='" + wifiSsid + "' required>";
  html += "</div>";
  html += "<div class='form-group'>";
  html += "<label for='password'>WiFiパスワード:</label>";
  html += "<input type='password' id='password' name='password' value='" + wifiPassword + "'>";
  html += "</div>";
  html += "<div class='form-group'>";
  html += "<label for='location'>場所名:</label>";
  html += "<input type='text' id='location' name='location' value='" + locationName + "' required>";
  html += "</div>";
  html += "<div class='form-group'>";
  html += "<label for='mode'>リーダーモード:</label>";
  html += "<select id='mode' name='mode'>";
  html += String("<option value='0'") + (readerMode == MODE_ATTENDANCE ? " selected" : "") + ">入退室</option>";
  html += String("<option value='1'") + (readerMode == MODE_PLAY ? " selected" : "") + ">遊び場所</option>";
  html += "</select>";
  html += "</div>";
  html += "<button type='submit'>保存</button>";
  html += "</form>";
  
  html += "<div class='note'>";
  html += "<p><strong>注:</strong> 設定を保存すると、デバイスは自動的に再起動します。</p>";
  html += "<p>QRコードで設定する場合は、以下のフォーマットを使用してください:</p>";
  html += "<p><code>http://" + WiFi.localIP().toString() + "/qr?id=SSID&pw=PASSWORD&pl=LOCATION&mode=0or1</code></p>";
  html += "</div>";
  
  html += "</div></body></html>";
  return html;
}

// ルートページのハンドラ
void handleRoot() {
  server.send(200, "text/html", getConfigPageHTML());
}

// ステータスのみを取得するAPIエンドポイント


// ステータスのみを取得するAPIエンドポイント
void handleStatus() {
  String status = "{";
  status += "\"chip_id\":\"" + chipId + "\",";
  status += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  if (WiFi.status() == WL_CONNECTED) {
    status += "\"ssid\":\"" + WiFi.SSID() + "\",";
    status += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  }
  status += "\"location\":\"" + locationName + "\",";
  status += "\"mode\":\"" + String(readerMode == MODE_ATTENDANCE ? "入退室" : "遊び場所") + "\",";
  status += "\"mode_value\":" + String(readerMode) + ",";
  status += "\"register_mode\":" + String(isRegisterMode ? "true" : "false") + ",";
  status += "\"spiffs_initialized\":" + String(spiffsInitialized ? "true" : "false");
  status += "}";
  
  server.send(200, "application/json", status);
  

}
