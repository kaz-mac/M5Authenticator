/*
  M5Authenticator
  M5Stack DinMeterで二段階認証のパスワードを表示するデバイス ver.1.0

  Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT

  対応ハードウェア
    コントローラー M5 DinMeter 
    RFIDリーダー: M5Stack RFID 2 Unit (WS1850S)

  対応カード
    MIFARE Classic 1K, NTAG213/215/216

  コンパイル時は以下のように設定する
    Flash Sizeを8MBに変更する
    Partition Schemeをcustomに変更する

  TODO
    なし
  既知の問題
    一度WiFi接続すると一部の文字が表示されなくなる（canvas絡み）
    バッテリー駆動だと動作が重くなる。エンコーダーやボタンの誤作動が起こりやすい。
    ESP32-BLE-KeyboardでNimBLEを使う場合は、NimBLE-Arduinoのバージョンを1.43にする（今回はNimBLE未使用）
*/
//#include <M5Unified.h>  // M5DinMeter.hの中でM5UnifiedをincludeしてるのでM5Unified.hは書かなくてよい
#include <M5DinMeter.h>   // https://github.com/m5stack/M5DinMeter/
#include <Arduino.h>
#include <FFat.h>
#include <WiFi.h>
#include "common.h"
#include "secret.h"

// 定数
//#define BUZZ_PIN 3
#define POWER_HOLD_PIN 46   // GPIOピン H=稼働時、L=スリープ時
#define GPIO_BTN_A     42   // GPIOピン ボタンA (wake)
#define USE_RFID       true // RFIDユニットを使用する
#define USE_QRCODE     true // QR-CODEユニットを使用する
bool debug = true;

// デバッグに便利なマクロ定義 --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)
#define spp(k,v) Serial.println(String(k)+"="+String(v))
#define spf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)

// ユーザーインターフェースCLASS
#include "DinMeterUI.h"
DinMeterUI ui;

// 設定情報CLASS
#include "Configure.h"
Configure cf;  // CLASS

// アイコン画像
#include "icon.h"

// NTP関連
#include <esp_sntp.h>
const char NTP_TIMEZONE[] = "JST-9";
const char NTP_SERVER1[]  = "ntp.nict.jp";
const char NTP_SERVER2[]  = "ntp.jst.mfeed.ad.jp";
// const int8_t TIME_ZONE = 9;

// BLE関連
// #define USE_NIMBLE     // NimBLE-Arduinoを使う場合はNimBLE 2.xだとエラーになる
#include <BleKeyboard.h>  // ZIPからインストール https://github.com/T-vK/ESP32-BLE-Keyboard
const char* BLE_DEVICE_NAME = "M5Authenticator";
BleKeyboard bleKeyboard(BLE_DEVICE_NAME, "M5DinMeter", 100);

// M5Unit-QR関連 
#include <M5UnitQRCode.h>   // https://github.com/m5stack/M5Unit-QRCode
M5UnitQRCodeI2C qr;  // I2Sモード
//M5UnitQRCodeUART qr;  // UARTモード

// NFC関連
// #include <MFRC522_I2C.h>   // ライブラリマネージャーから要インストール（NfcEasyWriterで使用）
#include "NfcEasyWriter.h"
#define M5UNIT_RFID2_ADDR 0x28
MFRC522_I2C_Extend mfrc522(M5UNIT_RFID2_ADDR, -1, &Wire); // デバイスアドレス, dummy, TwoWireインスタンス（&Wire省略可）
NfcEasyWriter nfc(mfrc522);  // nfcwriter オブジェクトのインスタンス化

// TOTP関連
#include <TOTP.h>

// Ticker関連
#include <Ticker.h>
Ticker tickerBleConn;
Ticker tickerAutooff;
Ticker tickerUpdateUI;
bool tickerUpdateUISkip = false;

// グローバル変数
StatusInfo status;  // ステータス情報
ConfigInfo conf;    // 設定情報
AuthKey passwdNfc;  // NFCのパスワード
String nextOtpFilename; // OTP追加後に表示するファイル名
int8_t pinSda, pinScl;  // GPIOポート SDA SCL
uint32_t wctPastTime = 0;  // 無操作カウンター(ms)
bool autoSleepEnable = true;  // 無操作自動スリープの有効化
String webAuthUser;    // 
String webAuthPasswd;  // 

// サブルーチン
#include "function.h"
#include "totputil.h"
#include "utility.h"
#include "webserver.h"


// =================================================================================
//  Ticker / Callback 処理
// =================================================================================

// BLEの状態変化を捉える  Ticker 250ms
void tickerBleConnectionMonitor() {
  static bool prev = !bleKeyboard.isConnected();
  bool cur = bleKeyboard.isConnected();
  if (cur != prev){
    status.ble = cur;
    if (cur) {
      if (debug) sp("BLE connected!");
    } else {
      if (debug) sp("BLE disconnected!");
    }
  }
  prev = cur;
}

// 無操作カウンター　更新
void wctInterrupt() {
  wctPastTime = 0;
}

// 無操作カウンター　定期処理  Ticker 1000ms
void wctTicker() {
  // getBatteryLevel(true);  // デバッグ バッテリーレベルチェック
  static uint32_t lastTime = millis();
  static uint8_t longPress = 0;
  // 非常シャットダウン 10秒長押し
  longPress = (M5.BtnA.isPressed()) ? longPress + 1 : 0;
  if (longPress >= 10) {
    if (debug) sp("Emargency Power Off");
    funcPoweroff();
  }
  // 無操作で指定時間が経過したら電源オフ
  if (!autoSleepEnable || !conf.loaded || conf.autoSleep < 10) {
    wctPastTime = 0;
    return;
  }
  wctPastTime += millis() - lastTime;
  lastTime = millis();
  if ((wctPastTime / 1000) > conf.autoSleep) {
    if (debug) sp("Auto Power Off");
    funcPoweroff();
  }
}

// ボタン押下割り込み
bool m5BtnAwasReleased() {
  bool res = M5.BtnA.wasReleased();
  if (res) wctInterrupt();
  return res;
}

// UIステータスの更新　変化があったときだけ描画更新  Ticker 500ms
void tickerRefreshStatusInfo() {
  if (tickerUpdateUISkip) return;
  static bool lastUnlock = status.unlock;
  static bool lastBle = status.ble;
  static bool lastUnitQRready = status.unitQRready;
  static bool lastUnitRFIDready = status.unitRFIDready;
  static uint8_t lastBattery = status.battery;
  static uint32_t tm = 0;
  // 定期的にバッテリー残量をチェック　30秒に1回
  if (tm < millis()) {
    status.battery = getBatteryLevel(false);
    tm = millis() + 30000;
    if (debug) spp("getBatteryLevel", status.battery);
  }
  // 比較
  bool refresh = false;
  if (status.unlock != lastUnlock) refresh = true;
  if (status.ble != lastBle) refresh = true;
  if (status.unitQRready != lastUnitQRready) refresh = true;
  if (status.unitRFIDready != lastUnitRFIDready) refresh = true;
  if (status.battery != lastBattery) refresh = true;
  lastUnlock = status.unlock;
  lastBle = status.ble;
  lastUnitQRready = status.unitQRready;
  lastUnitRFIDready = status.unitRFIDready;
  lastBattery = status.battery;
  // UI更新
  if (refresh) {
    ui.drawStatusPanel(&status);  // UI左描画
  }
}

// =================================================================================
//  初期化
// =================================================================================

void setup() {
  bool res;
  // M5DinMeterの初期化
  auto cfg = M5.config();
  DinMeter.begin(cfg, true);  // true=Encoder enable

  // M5DinMeterでバッテリー駆動時に電源ONを継続するための設定
  gpio_reset_pin((gpio_num_t)POWER_HOLD_PIN);
  pinMode(POWER_HOLD_PIN, OUTPUT);
  digitalWrite(POWER_HOLD_PIN, HIGH);

  // シリアル
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  sp("\n\nSystem Start!");
  debug_free_memory("Setup-start");

  // I2Cの初期化
  pinSda = M5.getPin(m5::pin_name_t::port_a_sda);  // Port A
  pinScl = M5.getPin(m5::pin_name_t::port_a_scl);
  spf("I2C Pin Setting SDA=%d SCL=%d\n", pinSda, pinScl);
  Wire.begin(pinSda, pinScl);   // この後のqrcode.begin()でも初期化される

  // ディスプレイの設定
  DinMeter.Display.init();
  DinMeter.Display.setRotation(1);
  DinMeter.Display.setColorDepth(16);
  DinMeter.Display.setTextSize(1);
  DinMeter.Display.setBrightness(127);
  DinMeter.Display.fillScreen(TFT_BLACK);

  // UIの設定
  ui._debug = debug;
  ui.setDrawDisplay(&DinMeter.Display, 0x000001);   // 出力先、透過色(未使用)
  tickerUpdateUI.attach_ms(500, tickerRefreshStatusInfo);
  console("Now Loading...\n");

  // エンコーダーの設定
  DinMeter.Encoder.readAndReset();

  // FatFSの有効化
  if (!FFat.begin()) {
    console("**Error** Filesystem cannot open.\n");
    beep(BEEP_ERROR);
    delay(3000);
  }

  // FatFSに保存した設定を読み込む
  cf._debug = debug;
  if (cf.loadConfig(conf) && conf.loaded) {
    sp("restore setting successful from config-file");
  } else {
    console("**Error** config-file not loaded.\n");
    beep(BEEP_ERROR);
    delay(3000);
  }

  // IV（AES暗号化の初期ベクトル）を生成してメモリ上に保存する　値は一意になる
  if (getIV(status.iv, sizeof(status.iv), IV_PHRASE)) {
    if (debug) {
      spn("IV: ");
      printDump1Line(status.iv, sizeof(status.iv));
    }
  } else {
    console("IV generate failed");
    delay(999999);
  }

  // IVを元にNFCのパスワードを生成する　値は一意になる
  memcpy(passwdNfc.keyByte, status.iv, sizeof(passwdNfc.keyByte));
  nfc.setAuthKey(&passwdNfc);

  // M5Unit-RFID2の初期化
  if (USE_RFID) {
    spn("M5Unit-RFID2 initialize...");
    nfc._debug = true;  // デバッグ出力有効
    nfc._dbgopt = NFCOPT_DUMP_AUTHFAIL_CONTINUE;  // デバッグ: DumpAll時認証エラー後も継続
    nfc.init();
    status.unitRFIDready = nfc.firmwareVersionCheck();
    sp(status.unitRFIDready ? "success" : "failed");
  }

  // 初回設定が済んでいない場合はセットアップを開く
  if (!conf.loaded || conf.saveSecret == CONF_SECRET_NONE) {
    tickerUpdateUISkip = true;
    ui._dst->fillScreen(TFT_BLACK);
    res = funcInitialSetup();
    String message = res ? "初期設定が正常に完了しました。" : "初期設定は失敗しました。";
    message = message + "再起動します。";
    ui.selectNotice("OK", "セットアップ", message, 64, false); // ダイアログ表示
    restart();  // 再起動
  }

  // 時刻が正しく設定されているかチェックする
  Tms tms = getMultiDateTime(true);
  spp("RTC time", tms.ymd);
  if (tms.epoch < 1700000000) {
    String message = "時刻が正しく設定されていません。設定メニューで時刻を設定してください。";
    ui.selectNotice("OK", "セットアップ", message, 64, false); // ダイアログ表示
  }

  // M5Unit-QRの初期化 I2Cモード
  // auto pinTX = M5.getPin(m5::pin_name_t::port_b_pin2);  // UARTモードの場合
  // auto pinRX = M5.getPin(m5::pin_name_t::port_b_pin1);  // UARTモードの場合
  if (USE_QRCODE) {
    spn("M5Unit-QR initialize...");
    qrcodeUnitInitI2C(0);    // タイムアウト0ですぐ抜ける。ここで初期化できなくても使用時にするから問題ない
    sp(status.unitQRready ? "success" : "failed");
  }

  // BLEの設定
  bleKeyboard.begin();  // メモ：バッテリー駆動時の起動にここで落ちることがある
  tickerBleConn.attach_ms(250, tickerBleConnectionMonitor);

  // 秘密鍵をロードする（秘密鍵を本体に保存している場合）
  deleteSecret(CONF_SECRET_MEMORY);
  status.unlock = false;
  if (conf.saveSecret == CONF_SECRET_FATFS) {
    if (loadSecret(conf.saveSecret)) {
      status.unlock = true;
    } else {
      console("**Error** secret-key cannot load from FatFS.\n");
      beep(BEEP_ERROR);
      delay(3000);
    }
  }

  // FatFSからSSL秘密鍵と証明書をロードしてHTTPSサーバーを初期化する
  res = loadInitServer();
  spp("loadInitServer",tf(res));
  if (!res) {
    console("**Error** certificate cannot load from FatFS.\n");
    beep(BEEP_ERROR);
    delay(3000);
  }

  // 自動スリープ機能
  tickerAutooff.attach_ms(1000, wctTicker);
  ui.setEncoderCallback(wctInterrupt);  // DinMeterUI ロータリーエンコーダー操作時のコールバック追加
  ui.setButtonFunction(wctInterrupt);   // DinMeterUI ボタン押下時のコールバック追加

  // その他
  getBatteryLevel(true);  // デバッグ バッテリーレベルチェック
  beep(BEEP_DOUBLE);  // ブザー
  debug_free_memory("Setup-last");
}


// =================================================================================
//  メイン
// =================================================================================

// 右メニュー（ロータリー）の初期状態
MenuDef menuTop = {
  .title = "メインメニュー",
  .select = 0,
  .selected = -1,
  .idx = 0,
  .cur = 0,
  .lists = {
    { Itype::none, ICON_key, "パスワード生成", funcOtpShow, "ワンタイムパスワードをPCに送信します" },
    { Itype::none, ICON_barcode, "バーコードリーダー", funcBarcodeReader, "バーコードをスキャンして、PCに送信します" },
    { Itype::none, ICON_clock, "時計", funcClock, "現在時刻を表示します" },
    { Itype::setting, ICON_setting, "設定", nullptr, "デバイスの各種設定をします" },
    { Itype::none, ICON_power, "電源オフ", funcPoweroff, "電源を切ります" },
  },
};

// 設定メニューの初期状態
MenuDef menuSet = {
  .title = "設定メニュー",
  .select = 0,
  .selected = -1,
  .idx = 0,
  .cur = 0,
  .lists = {
    { Itype::back, 0, "<< 戻る", nullptr, "" },
    { Itype::subtitle, 0, "          機能", nullptr, "" },
    { Itype::none, 0, "サイトの追加", funcAddOtp, "サイトの二段階認証を追加します" },
    { Itype::none, 0, "サイトの削除", funcDelOtp, "サイトの二段階認証を削除します" },
    { Itype::none, 0, "BLEペアリング", funcPairing, "PCとBLEでペアリングします" },
    { Itype::goRestart, 0, "バックアップ", funcWebserver, "ファイルの転送が可能なWebサーバーを起動します" },
    { Itype::back, 0, "<< 戻る", nullptr, "" },
    { Itype::subtitle, 0, "          設定", nullptr, "" },
    { Itype::goRestart, 0, "NTP時刻同期", functRtc, "WiFiを使用してNTPサーバーと時刻を同期します" },
    { Itype::none, 0, "手動時刻設定", functAdjustTime, "手動で時刻を設定します" },
    { Itype::none, 0, "自動改行の設定", funcAutoEnter, "【設定】PCへの送信時に最後に改行を入れるか設定します" },
    { Itype::none, 0, "無操作 自動電源オフの設定", funcAutoSleepAc, "無操作時に自動的に電源をオフにする秒数を設定します" },
    { Itype::none, 0, "PW送信 自動電源オフの設定", funcAutoSleepPw, "パスワード送信後に自動的に電源をオフにする秒数を設定します" },
    { Itype::none, 0, "静音モード", funcSetQuiet, "BEEP音の設定を変更できます" },
    { Itype::none, 0, "JIS配列モード", funcSetJiskey, "BLEキーボードをJIS配列かUS配列に変更できます" },
    { Itype::none, 0, "開発者モード", funcDevelop, "開発者モードを有効にします" },
    { Itype::back, 0, "<< 戻る", nullptr, "" },
    { Itype::subtitle, 0, "        キュリティ", nullptr, "" },
    { Itype::none, 0, "秘密鍵の移動", funcKeyMove, "秘密鍵をNFCまたは本体に移動します" },
    { Itype::none, 0, "NFCの秘密鍵を複製", funcKeyDuplicate, "予備用にNFCの秘密鍵を複製します" },
    { Itype::none, 0, "NFCフォーマット", funcFormatNfc, "NFCカードの秘密鍵を初期化します" },
    { Itype::none, 0, "サイトのエクスポート", funcExportOtp, "サイトの二段階認証をデータを他のアプリにエクスポートします" },
    { Itype::subtitle, 0, "          開発者", nullptr, "" },
    { Itype::goRestart, 0, "本体フォーマット", funcFormatFatfs, "本体のFatFSや設定の初期化します" },
    { Itype::none, 0, "HEXダンプ", funcHexDump, "シリアルコンソールにファイルのHEXデータをダンプします" },
    { Itype::none, 0, "DEBUG BLE全ASCII送信", funcDevelopSendAscii, "BLEで全ASCIIコードを送信" },
    { Itype::goRestart, 0, "SSL証明書再生成", funcRegenerateOreoreSSL, "SSL証明書を削除して再生成します" },
    { Itype::back, 0, "<< 戻る", nullptr, "" },
  },
};

// メインループ
void loop() {
  int dir;
  M5.update();

  // 初期表示　右メニュー（ロータリー）
  ui.drawStatusPanel(&status);  // UI左描画
  ui.drawRotaryPanel(&menuTop);   // UI右描画
  ui.drawMainPanel_preview(&menuTop);   // UI中央描画

  // 右メニュー（ロータリー）の表示とメニューの選択 ---------------------------------------------------
  while (menuTop.selected == -1) {
    // エンコーダー
    dir = ui.encoderChanged(&menuTop, false);  // エンコーダーの変化あり
    if (dir != 0) {
      ui.drawRotaryPanel(&menuTop);   // UI右描画
      ui.drawMainPanel_preview(&menuTop);   // UI中央描画
    }
    // ボタン押下
    M5.update();
    if (m5BtnAwasReleased()) {
      menuTop.selected = menuTop.select;
      if (debug) spf("Menu %d selected.\n", menuTop.selected);
      break;
    }
    delay(5);
  }
  if (menuTop.selected == -1) return;

  // メニュー：「設定」を押したとき --------------------------------------------------------------
  if (menuTop.lists[menuTop.selected].type == Itype::setting) {
    while (menuTop.selected != -1) {
      String title = menuTop.lists[menuTop.selected].name;
      String description = menuTop.lists[menuTop.selected].description;
      int selected;
      if (menuSet.selected == -1) {
        selected = ui.selectMenuList(&menuSet, -1, 4, description, 20);  // リスト形式のメニューを選択する
      }
      // 選択したサブメニューに進む
      bool res = false;
      if (menuSet.lists[selected].function != nullptr) {
        res = menuSet.lists[selected].function();
      }
      uint8_t type = menuSet.lists[selected].type;
      if (type == Itype::back) {    // 戻る
        menuTop.selected = -1;
      } else if (type == Itype::goRestart && res) {    // 要再起動の場合
        restart();  // 再起動
      } else if (type == Itype::goPowerdown && res) {    // 要電源オフの場合
        funcPoweroff();  // 電源オフ
      }
      menuSet.selected = -1;
    }
  } else {
    // メニュー：それ以外を押したとき
    if (menuTop.lists[menuTop.selected].function != nullptr) {
      menuTop.lists[menuTop.selected].function();
    }
    menuTop.selected = -1;
  }

  delay(30);
}

// =================================================================================



