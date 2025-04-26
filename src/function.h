/*
  function.h
  各メニューに対応するサブルーチン

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#pragma once

#include "common.h"
#include "secret.h"

#include <esp_sntp.h>   // for NTP
// #define USE_NIMBLE
#include <BleKeyboard.h>
#include <FFat.h>
#include <WiFi.h>

// メインで定義した変数を使用するためのもの
#include "DinMeterUI.h"
extern DinMeterUI ui;
extern const char NTP_TIMEZONE[];
extern const char NTP_SERVER1[];
extern const char NTP_SERVER2[];
extern BleKeyboard  bleKeyboard;

#include "Configure.h"
extern Configure cf;
extern StatusInfo status;
extern ConfigInfo conf;
void wctInterrupt();
bool m5BtnAwasReleased();

// --------------------------------------------------------------------------------------
// 【設定】初回セットアップ
// --------------------------------------------------------------------------------------
bool funcInitialSetup() {
  const String title = "セットアップ";
  std::vector<String> yesno;
  String message;
  int selected;
  bool res;

  // 確認画面
  message = "全てのデータが削除されます。よろしいですか?";
  yesno = { "No", "Yes" };
  selected = ui.selectDialog(yesno, 0, title, message, 72); // ダイアログ表示
  if (selected != 1) return false;

  // フォーマット
  if (debug) sp("Format FatFS Start");
  FFat.end();
  res = FFat.format();
  if (res) res = FFat.begin();
  if (debug) spp("Format FatFS", tf(res));
  // 結果表示
  if (res) {
    message = "FatFSフォーマット成功\n";
    message += (String) "Total: "+(FFat.totalBytes() / 1024)+" KB\n";
    message += (String) "Used:  "+(FFat.usedBytes() / 1024)+" KB\n";
    message += (String) "Free:  "+(FFat.freeBytes() / 1024)+" KB\n";
  } else {
    message = "FatFSフォーマット失敗\n";
  }
  ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
  if (debug) sp(message);
  if (!res) return false;

  // 設定ファイルを初期化する（初期状態で一旦FatFSに保存する。この後再度保存する）
  res = cf.initConfig();
  if (debug) spp("Init Config", tf(res));
  if (!res) return false;

  // 秘密鍵の保存先
  message = "秘密鍵はどちらに保存しますか?";
  yesno = { "本体", "NFC" };
  selected = ui.selectDialog(yesno, 0, title, message, 72); // ダイアログ表示
  if (selected == 1 && !status.unitRFIDready) {
    message = "エラー! RFIDリーダーが接続されていません";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }
  conf.saveSecret = (selected == 1) ? CONF_SECRET_NFC : CONF_SECRET_FATFS;
  if (debug) sp("saveSecret: "+ (String)((selected == 1) ? "NFC" : "FatFS"));

  // 秘密鍵を生成し、メモリ上に保存する
  for (int i=0; i<sizeof(status.secret); i++) {
    status.secret[i] = esp_random() & 0xFF;
  }
  if (debug) {
    spn("Secret: ");
    printDump1Line(status.secret, sizeof(status.secret));
  }
  
  // 保存先がNFCの場合、NFCカードをマウントする
  if (conf.saveSecret == CONF_SECRET_NFC) {
    res = nfcMountSequence(title, 500);  // ダイアログ付き NFCマウント
    // 使用可能なNFCカードかチェック
    if (res) {
      uint16_t freeSize = nfc.getVCapacities();   // 使用可能な容量
      if (debug) spp("NFC Card mounted. freesize", freeSize);
      res = (freeSize >= SECRET_NFC_PARTITION_ADDR + SECRET_SAVE_SIZE);
      if (debug) spp("NFC Capacity", tf(res));
    }
    if (!res) {
      message = "エラー! このNFCカードは使用できません";
      ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
      return false;
    }
    // NFCをプロテクトモードに書き換える
    res = nfcChangeProtect(true, false);   // Protect On, Format Quick
    if (debug) spp("NFC Protect", tf(res));
    if (!res) {
      message = "エラー! このNFCカードは書込みできません";
      ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
      return false;
    }
  }

  // 秘密鍵を保存する
  res = saveSecret(conf.saveSecret);
  if (debug) spp("Save Secret", tf(res));
  if (conf.saveSecret == CONF_SECRET_NFC) {   // 保存先がNFCの場合、NFCカードをアンマウントする
    nfcUnmountSequence(title, false);  // NFCのアンマウント（ダイアログなし）
    if (res) {
      message = "書き込み完了! NFCカードを取り外してかまいません";
      ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    }
  }
  if (!res) {
    message = "エラー! 秘密鍵の保存に失敗しました";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // SSLの証明書を生成
  message = "SSLのサーバー証明書を生成します。この処理は 10 秒程度時間がかかります。そのままお待ちください。";
  ui.selectNotice("wait...", title, message, 72, true); // ダイアログ表示　のみ
  res = httpsGenerateCertificate();
  if (debug) spp("httpsGenerateCertificate", tf(res));
  if (!res) {
    message = "エラー! サーバー証明書の保存に失敗しました";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // 設定ファイルをFatFSに保存する
  res = cf.saveConfig(conf);
  if (debug) spp("Save Config", tf(res));
  if (!res) return false;

  return true;
}

// --------------------------------------------------------------------------------------
// 【メイン】ワンタイムパスワードをPCに送信する 
// --------------------------------------------------------------------------------------
bool funcOtpShow() {
  String title = "パスワード生成";
  String message = "";
  std::vector<String> yesno;
  int selected;
  bool res, err = false;

  // チェック
  bool needNfc = (!status.unlock && conf.saveSecret == CONF_SECRET_NFC);
  if (needNfc && !status.unitRFIDready) {
    message = "エラー! RFIDリーダーが接続されていません";
  }

  // OTP一覧の取得  
  std::vector<TotpParamsList> tps;
  int cnt = listAllOtpFiles(&tps, false);
  if (cnt < 1) {
    message = "エラー! 保存されているデータはありません";
  }

  // 時刻の取得
  Tms tms = getMultiDateTime(true);  // 現在時刻の取得
  if (tms.epoch < 1700000000) {
    message = "エラー! デバイスの時刻が正しく設定されていません";
  }

  // エラー
  if (message.length() > 0) {
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // メニュー変数の作成
  MenuDef menu = {
    .title = title,
    .select = 1,
    .selected = -1,
    .idx = 0,
    .cur = 1,
  };
  menu.lists.push_back({ 0, 0, "戻る", nullptr, "" });
  for (int i=0; i<cnt; i++) {
    String text = String(tps[i].tp.issuer) + " " + String(tps[i].tp.account);
    menu.lists.push_back({ 0, 0, text, nullptr, "" });
  }

  // 一覧から選択
  int seltp = -1;
  String description = "サイトを選択してください";
  int boxnum = (menu.lists.size() < 4) ? menu.lists.size() : 4;
  selected = ui.selectMenuList(&menu, -1, boxnum, description, 20);  // リスト形式のメニューを選択する
  if (selected > 0) {
    seltp = selected - 1;
  }
  if (seltp < 0) return false;

  // 秘密鍵が読み込まれてない場合は読み込む
  if (needNfc) {
    if (nfcMountSequence(title, 100)) {  // ダイアログ付き NFCマウント
      if (loadSecret(conf.saveSecret)) status.unlock = true;
    }
    nfcUnmountSequence(title, false);  // ダイアログなし NFCアンマウント
  }
  if (!status.unlock) {
    message = "エラー! 秘密鍵が正常に読み込めませんでした";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // OTPファイルを読み込む
  TotpParams tp;
  res = loadOtpFile(tps[seltp].filename, &tp, true);
  if (debug) spp("loadOtpFile",tf(res));
  if (!res) return false;

  // canvasの作成
  M5Canvas canvas(ui._dst);
  canvas.setColorDepth(16);
  canvas.createSprite(ui.mwh.w-4, ui.mwh.h-32);
  canvas.setTextColor(TFT_WHITE);
  int cw = canvas.width();
  
  // メニュー変数の作成2
  LABEL_OTP_SHOW:
  MenuDef menu2 = {
    .title = "",//tps[seltp].tp.issuer,
    .select = 1,
    .selected = -1,
    .idx = 0,
    .cur = 1,
    .lists = {
      {0,0,"<<",nullptr,""}, {0,0,"送信",nullptr,""}
    },
  };

  // ワンタイムパスワード表示
  bool init = true;
  String code;
  int lastselno = -1, cntDwn = 999999;
  int cntDwnDef = (conf.autoKeyOff == 0) ? 999998 : conf.autoKeyOff;
  bool btned = false;
  while (1) {
    // ダイアログ表示　表示のみ
    uint32_t tm = millis() + 1000;
    if (menu2.select != lastselno) {
      menu2.cur = menu2.select;
      ui.drawMainPanel_dialog(&menu2, menu2.select, "", 72);   // UI中央描画
      lastselno = menu2.select;
    }
    // ワンタイムパスワード生成
    bool sync = (init || tms.epoch % tp.period == 0);
    tms = getMultiDateTime(sync);  // 現在時刻の取得
    code = getTotp(&tp, tms.epoch);
    String code0 = getTotp(&tp, tms.epoch - tp.period);
    String code2 = getTotp(&tp, tms.epoch + tp.period);
    int remain = tp.period - tms.epoch % tp.period;
    init = false;
    // ワンタイムパスワード表示
    canvas.fillSprite(TFT_BLACK);
    canvas.fillRect(0,0, cw,16, ui.PCOL_TITLE);
    canvas.setTextColor(TFT_BLACK);
    canvas.drawString(String(tps[seltp].tp.issuer)+" "+String(tps[seltp].tp.account), 3,0, &fonts::Font2);  // 16px
    canvas.setTextDatum(TC_DATUM);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString(tms.ymd, cw/2,17, &fonts::Font2);  // 16px
    canvas.setTextColor((remain < 5) ? ui.rgb565(0xFF8080) : TFT_WHITE);
    canvas.drawString(code, cw/2,39, &fonts::Font6);  // 48px
    canvas.setTextColor(TFT_WHITE);
    canvas.setTextDatum(TR_DATUM);
    int y = 84;
    canvas.drawString("> "+code2, cw-3,y, &fonts::Font2);  // 16px
    canvas.setTextDatum(TL_DATUM);
    canvas.drawString(code0+" <", 2,y, &fonts::Font2);  // 16px
    // プログレスバーの表示
    int x = cw / 2 - 20;
    int per = (remain * 100) / tp.period;
    int pw = map(per, 0,100, 0,40);
    canvas.fillRect(x,y+2, 40-pw,12, TFT_RED);
    canvas.fillRect(x+(40-pw),y+2, pw,12, TFT_GREEN);
    canvas.drawRect(x,y+1, 41,14, ui.rgb565(0x666666));
    // canvasの出力
    ui.lockCanvas();
    ui._dst->startWrite(); 
    canvas.pushSprite(ui.mxy.x+2, ui.mxy.y);
    ui._dst->endWrite();
    ui.unlockCanvas();
    // ボタン押し待ち＆ロータリーエンコーダー選択（UI関数を使用せずに描画する）
    int encpos = DinMeter.Encoder.read();
    static int lastpos = encpos;
    int dir;
    selected = -1;
    while (millis() < tm) {
      // ボタン
      M5.update();
      if (m5BtnAwasReleased()) {
        selected = menu2.select;
        cntDwn = cntDwnDef;
        btned = true;
        break;
      }
      // ロータリーエンコーダー
      encpos = DinMeter.Encoder.read();
      if (encpos != lastpos && encpos % 2 == 0) { // エンコーダーが変化した場合
        dir = -1 * ((lastpos < encpos) ? 1 : -1);
        if (dir == -1 && menu2.select > 0) menu2.select --;
        else if (dir == 1 && menu2.select <= menu2.lists.size()-2) menu2.select ++;
        lastpos = encpos;
        if (cntDwn < cntDwnDef) cntDwn = cntDwnDef;
        wctInterrupt(); // 無操作スリープ割込
        break;
      }
      delay(10);
    } //while(2)
    ui.encoderCacheClear();
    // 電源オフのカウントダウン
    cntDwn --;
    if (cntDwn < cntDwnDef) lastselno = -1; // ボタン再描画のため
    if (cntDwn < 1) {
      funcPoweroff(); // 電源オフ
    }
    if (btned) {
      String str = (cntDwn < 10) ? "電源OFF(" + String(cntDwn) + ")" : "電源OFF";
      menu2.lists = { {0,0,"<<",nullptr,""}, {0,0,"再送信",nullptr,""}, {0,0,str,nullptr,""} };
    }
    // ボタンを押したあとの処理
    if (selected == 0) {  // 「<<」ボタンを押した場合
      break;
    } else if (selected == 1) {  // 「送信」ボタンを押した場合
      // BLEキーボードで送信
      if (status.ble) {
        bleKeyboard.print(code);    // BLEキー送信
        delay(100);
        if (conf.autoEnter) bleKeyboard.write(KEY_RETURN);
        if (debug) sp("BLE Send Key: "+code);
      } else {
        if (debug) sp("Error! BLE not connected");
      }
    } else if (selected == 2) {  // 「電源OFF」ボタンを押した場合
      funcPoweroff(); // 電源オフ
    }
  } //while(1)

  return true;
}

// --------------------------------------------------------------------------------------
// 【メイン】Barcode/QR-codeを読み込んでキータイプする 
// --------------------------------------------------------------------------------------
bool funcBarcodeReader() {
  bool success = false, abort = false;
  String title = "バーコードリーダー";
  String message;
  uint8_t buff[512] = {0};
  size_t len;

  // M5Unit-QRが未初期化だったら初期化 I2Cモード
  if (!status.unitQRready) {
    if (debug) spn("M5Unit-QR initialize...");
    qrcodeUnitInitI2C(5000);
    if (debug) sp(status.unitQRready ? "success" : "failed");
  }

  // チェック
  if (!status.ble) {
    message = "エラー! BLEが未接続です";
    abort = true;
  } else if (!status.unitQRready) {
    message = "エラー! M5Unit-QRが接続されていません";
    abort = true;
  } else {
    qrBufferClear();  // 読み取り前にゴミデータが入ってたら取り出す
    if (debug) sp("Barcode scaning...");
    qr.setDecodeTrigger(1);   // QRスキャン開始
    message = "バーコードまたはQRコードをスキャンしてください";
  }

  // 画面枠とボタンの表示
  ui.selectNotice("CANCEL", title, message, 64, !abort); // 枠のみ表示

  // QRスキャン
  while (!abort) {
    M5.update();
    if (m5BtnAwasReleased()) {  // ボタン押したら中断
      qr.setDecodeTrigger(0);   // QRスキャン終了
      abort = true;
      break;
    }
    if (qr.getDecodeReadyStatus() == 1) {   // スキャン完了
    // if (qr.available()) {   // スキャン完了
      // 読んだ値を取得
      len = qr.getDecodeLength();
      if (len > sizeof(buff)-1) len = sizeof(buff)-1;
      qr.getDecodeData(buff, len);
      if (debug) {
        spp("scaned len", len);
        spp("scaned data", (const char*)buff);
      }
      break;
    }
    delay(10);
  }
  if (abort) return false;

  // BLEキーボードで送信
  String text = "";
  for (int i=0; i<len; i++) {
    if (buff[i] >= 0x20 && buff[i] <= 0x7F) text += char(buff[i]);
  }
  if(bleKeyboard.isConnected()) {
    //bleKeyboard.print(text);
    for (int i=0; i<len; i++) {
      if (buff[i] >= 0x20 && buff[i] <= 0x7F) {
        uint8_t ascii = buff[i];
        if (conf.keyJis) ascii = keycodeJisToUs(ascii);  // US配列のキーボードでJISに対応させる
        bleKeyboard.print((char)ascii);
        delay(20);
      }
    }
    if (conf.autoEnter) bleKeyboard.write(KEY_RETURN);
    success = true;
  } else {
    if (debug) sp("Error! BLE not connected");
  }

  // 表示
  ui.selectNotice("OK", title, text, 64, true); // 枠のみ表示
  delay(1000);

  return success;
}

// --------------------------------------------------------------------------------------
// 【メイン】現在時刻を表示する
// --------------------------------------------------------------------------------------
bool funcClock() {
  String title = "現在時刻";
  bool abort = false;
  Tms tms;
  uint32_t tm;
  static int kp = 0;
  char buff[64] = {0};
  int th = 24;

  // 画面枠とボタンの表示
  tickerUpdateUISkip = true;  // ステータスUIの自動更新を停止
  ui.selectNotice("", title, "", 24, true); // 枠のみ表示

  // canvasの作成
  M5Canvas canvas(ui._dst);
  canvas.setColorDepth(16);
  // canvas.createSprite(ui.m5wh.w, ui.m5wh.h);
  canvas.createSprite(ui.mwh.w, ui.mwh.h-th);
  canvas.setTextColor(TFT_WHITE);

  // ループ
  while (!abort) {
    tm = millis() + 500;            // 0.5秒ごとに更新
    bool sync = (kp++ % 20 == 0);   // 20回つまり10秒に1回RTCと同期
    // 日付
    tms = getMultiDateTime(sync);  // 現在時刻の取得
    if (sync) delay(100);   // おまじない
    canvas.fillSprite(TFT_BLACK);
    sprintf(buff, "%04d / %02d / %02d", tms.tm->tm_year+1900, tms.tm->tm_mon+1, tms.tm->tm_mday);
    canvas.drawString(buff, 43,5, &fonts::Font2);  // 日付 16px
    canvas.setTextDatum(TR_DATUM);
    sprintf(buff, "%d : %02d", tms.tm->tm_hour, tms.tm->tm_min);
    canvas.drawString(buff, 153,35, &fonts::Font6);  // 時刻 48px
    canvas.setTextDatum(TL_DATUM);
    sprintf(buff, ": %02d", tms.tm->tm_sec);
    canvas.drawString(buff, 158,60, &fonts::Font2);  // 秒 16px
    // canvasの出力
    ui.lockCanvas();
    ui._dst->startWrite(); 
    // canvas.pushSprite(0,0);
    canvas.pushSprite(ui.mxy.x, ui.mxy.y+th);
    ui._dst->endWrite();
    ui.unlockCanvas();
    // ボタン押し待ち
    while (millis() < tm) {
      M5.update();
      if (m5BtnAwasReleased()) {
        abort = true;
        break;
      }
      delay(10);
    }
    if (abort) break;
  }
  tickerUpdateUISkip = false;  // ステータスUIの自動更新を再開
  return true;
}

// --------------------------------------------------------------------------------------
// 【メイン】電源オフ
// --------------------------------------------------------------------------------------
bool funcPoweroff() {
  // BLE切断
  bleKeyboard.end();  // 実際は何も実装されてない

  // HOLDピンをLOWにして電源を切る（バッテリー駆動時はDC-DC回路がオフになる）
  if (debug) sp("byebye");
  DinMeter.Rtc.clearIRQ();
  DinMeter.Rtc.disableIRQ();
  DinMeter.Display.fillScreen(TFT_BLACK);
  while (M5.BtnA.isPressed()) {
    M5.update();
    delay(100);
  }
  delay(200);
  sp("Power Off");
  digitalWrite(POWER_HOLD_PIN, LOW);  // スリープする。ボタンAで復帰
  delay(1000);

  // USB接続時は電源が切れないのでdeepsleepする
  DinMeter.Display.setBrightness(0);
  while (!M5.BtnA.isPressed()) {
    M5.update();
    delay(100);
  }
  sp("Zzz...");
  esp_sleep_enable_ext0_wakeup((gpio_num_t)GPIO_BTN_A, LOW);

  // 起きたら再起動
  sp("...!?");
  restart();  // 再起動
  return true;
}

// --------------------------------------------------------------------------------------
// 【設定】 OTPの追加
// --------------------------------------------------------------------------------------
bool funcAddOtp() {
  String title = "サイトの追加";
  const std::vector<String> yesno = { "NO", "YES" };
  String message, message2;
  int selected;
  bool res, abort = false, success = false;
  uint8_t buff[512] = {0};
  size_t len;

  // M5Unit-QRが未初期化だったら初期化 I2Cモード
  if (!status.unitQRready) {
    if (debug) spn("M5Unit-QR initialize...");
    qrcodeUnitInitI2C(5000);
    if (debug) sp(status.unitQRready ? "success" : "failed");
  }

  // チェック
  bool needNfc = (!status.unlock && conf.saveSecret == CONF_SECRET_NFC);
  if (!status.unitQRready) {
    message = "エラー! M5Unit-QRが接続されていません";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }
  if (needNfc && !status.unitRFIDready) {
    message = "エラー! RFIDリーダーが接続されていません";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  } 

  // 画面枠とボタンの表示
  message = "QRコードをスキャンしてください";
  ui.selectNotice("CANCEL", title, message, 64, !abort); // 枠のみ表示

  // スキャン開始
  qrBufferClear();  // 読み取り前にゴミデータが入ってたら取り出す
  if (debug) sp("Barcode scaning...");
  qr.setDecodeTrigger(1);   // QRスキャン開始

  // QRスキャン
  while (!abort) {
    M5.update();
    if (m5BtnAwasReleased()) {  // ボタン押したら中断
      qr.setDecodeTrigger(0);   // QRスキャン終了
      abort = true;
      break;
    }
    if (qr.getDecodeReadyStatus() == 1) {   // スキャン完了
      // 読んだ値を取得
      len = qr.getDecodeLength();
      if (len > sizeof(buff)-1) len = sizeof(buff)-1;
      qr.getDecodeData(buff, len);
      if (debug) {
        spp("scaned len", len);
        spp("scaned data", (const char*)buff);
      }
      break;
    }
    delay(10);
  }
  if (abort) return false;

  // URLをパースする
  TotpParams tp;
  res = parseUriOTP(&tp, String((const char*)buff));   // OTPのURIをパースする
  if (debug) spp("parseUriOTP", tf(res));
  if (!res || tp.digit != 6) {    // 6桁以外のTOTPはライブラリ側が対応していないので
    message = "エラー! このQRコード非対応です";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // 時刻チェック
  Tms tms = getMultiDateTime(true);  // 現在時刻の取得
  if (tms.epoch < 1700000000) {
    message = "エラー! デバイスの時刻が正しく設定されていません";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // canvasの作成
  M5Canvas canvas(ui._dst);
  canvas.setColorDepth(16);
  canvas.createSprite(140, 45);
  canvas.setTextColor(TFT_WHITE);
  
  // ワンタイムパスワード表示
  bool init = true;
  message = (String)"発行者 "+tp.issuer+"\n";
  message = message + "アカウント "+tp.account+"\n";
  ui.selectNotice("NEXT", title, message, 72, true); // ダイアログ表示
  while (1) {
    uint32_t tm = millis();
    // ワンタイムパスワード生成
    bool sync = (init || tms.epoch % tp.period == 0);
    tms = getMultiDateTime(sync);  // 現在時刻の取得
    String code = getTotp(&tp, tms.epoch);
    init = false;
    // ワンタイムパスワード表示
    canvas.fillSprite(TFT_BLACK);
    canvas.drawString(code, 25,0, &fonts::Font4);  // 26px
    canvas.drawString(tms.ymd, 0,28, &fonts::Font2);  // 16px
    // canvasの出力
    ui.lockCanvas();
    ui._dst->startWrite(); 
    canvas.pushSprite(ui.mxy.x+20, ui.mxy.y+56);
    ui._dst->endWrite();
    ui.unlockCanvas();
    // ボタン押し待ち
    uint32_t sa = (millis() - tm);
    if (sa <= 1000) {
      if (waitPressButton(1000 - sa)) break;
    }
  }

  // 確認
  message2 = "以下のサイトを追加しますか?\n" + message;
  if (needNfc) message2 = message2 + "\nNFCカードを用意してください";
  selected = ui.selectDialog(yesno, 0, title, message2, 72); // ダイアログ表示
  if (selected != 1) return false;

  // 秘密鍵が読み込まれてない場合は読み込む
  if (needNfc) {
    if (nfcMountSequence(title, 100)) {  // ダイアログ付き NFCマウント
      if (loadSecret(conf.saveSecret)) status.unlock = true;
    }
    nfcUnmountSequence(title, false);  // ダイアログなし NFCアンマウント
  }

  // OTPデータの保存
  String filename;
  if (status.unlock) {
    filename = makeOtpFilename(&tp);
    if (debug) spp("filename",filename);
    // if (filename.length() < 20) return false;
    success = saveOtpFile(filename, &tp);   // FatFSにOTPを保存する
    if (debug) spp("saveOtp",tf(success));
    if (!success) message = "エラー! OTPの保存に失敗しました";
  } else {
    message = "エラー! 秘密鍵が正常に読み込めませんでした";
  }

  // 最終表示
  if (success) {
    nextOtpFilename = filename;   // メニューを抜けたらOTP表示画面へ遷移するための情報
  } else {
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
  }
  return success;
}

// --------------------------------------------------------------------------------------
// 【設定】 OTPの削除
// --------------------------------------------------------------------------------------
bool funcDelOtp() {
  String title = "サイトの削除";
  const std::vector<String> yesno = { "NO", "YES" };
  String message;
  int selected;
  bool res;

  // OTP一覧の取得  
  std::vector<TotpParamsList> tps;
  int cnt = listAllOtpFiles(&tps, false);
  if (cnt < 1) {
    message = "エラー! 保存されているデータはありません";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // メニュー変数の作成
  MenuDef menu = {
    .title = title,
    .select = 0,
    .selected = -1,
    .idx = 0,
    .cur = 0,
  };
  menu.lists.push_back({ 0, 0, "戻る", nullptr, "" });
  for (int i=0; i<cnt; i++) {
    String text = String(tps[i].tp.issuer) + " " + String(tps[i].tp.account);
    menu.lists.push_back({ 0, 0, text, nullptr, "" });
  }

  // 一覧から選択
  String description = "サイトを選択してください";
  int boxnum = (menu.lists.size() < 4) ? menu.lists.size() : 4;
  selected = ui.selectMenuList(&menu, -1, boxnum, description, 20);  // リスト形式のメニューを選択する

  // 削除
  res = false;
  if (selected > 0) {
    // 確認
    int delno = selected - 1;
    message = "以下のサイトを削除しますか?\n";
    message = (String)"発行者 "+tps[delno].tp.issuer+"\n";
    message = message + "アカウント "+tps[delno].tp.account+"\n";
    selected = ui.selectDialog(yesno, 0, title, message, 72); // ダイアログ表示
    if (selected != 1) return false;
    message = "本当に削除してよろしいですか?\n";
    selected = ui.selectDialog(yesno, 0, title, message, 72); // ダイアログ表示
    if (selected != 1) return false;
    // 削除実行
    if (debug) sp("File Delete: "+tps[delno].filename);
    res = deleteFile(tps[delno].filename);
    if (debug) spp("deleteFile",tf(res));
  }

  return res;
}

// --------------------------------------------------------------------------------------
// 【設定】 OTPのエクスポート
// --------------------------------------------------------------------------------------
bool funcExportOtp() {
  String title = "サイトのエクスポート";
  String message = "";
  int selected;
  bool res;

  // チェック
  bool needNfc = (!status.unlock && conf.saveSecret == CONF_SECRET_NFC);
  if (needNfc && !status.unitRFIDready) {
    message = "エラー! M5Unit-RFIDが接続されていません";
  }

  // OTP一覧の取得  
  std::vector<TotpParamsList> tps;
  int cnt = listAllOtpFiles(&tps, false);
  if (cnt < 1) {
    message = "エラー! 保存されているデータはありません";
  }

  // エラー
  if (message.length() > 0) {
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // メニュー変数の作成
  MenuDef menu = {
    .title = title,
    .select = 0,
    .selected = -1,
    .idx = 0,
    .cur = 0,
  };
  menu.lists.push_back({ 0, 0, "戻る", nullptr, "" });
  for (int i=0; i<cnt; i++) {
    String text = String(tps[i].tp.issuer) + " " + String(tps[i].tp.account);
    menu.lists.push_back({ 0, 0, text, nullptr, "" });
  }

  // 一覧から選択
  int seltp = -1;
  String description = "サイトを選択してください";
  int boxnum = (menu.lists.size() < 4) ? menu.lists.size() : 4;
  selected = ui.selectMenuList(&menu, -1, boxnum, description, 20);  // リスト形式のメニューを選択する
  if (selected > 0) {
    seltp = selected - 1;
  }
  if (seltp < 0) return false;

  // 秘密鍵が読み込まれてない場合は読み込む
  if (needNfc) {
    if (nfcMountSequence(title, 100)) {  // ダイアログ付き NFCマウント
      if (loadSecret(conf.saveSecret)) status.unlock = true;
    }
    nfcUnmountSequence(title, false);  // ダイアログなし NFCアンマウント
  }
  if (!status.unlock) {
    message = "エラー! 秘密鍵が正常に読み込めませんでした";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // OTPファイルを読み込む
  TotpParams tp;
  res = loadOtpFile(tps[seltp].filename, &tp, true);
  if (debug) spp("loadOtpFile",tf(res));
  if (!res) return false;
  
  // URL作成
  String url = "otpauth://totp/";
  if (strlen(tp.issuer) > 0 && strlen(tp.account) > 0) {
    url = url + urlEncode(tp.issuer) + ":%20" + urlEncode(tp.account);
  } else if (strlen(tp.issuer) > 0 && strlen(tp.account) == 0) {
    url = url + urlEncode(tp.issuer);
  } else if (strlen(tp.issuer) == 0 && strlen(tp.account) > 0) {
    url = url + urlEncode(tp.account);
  }
  if (strlen(tp.secret) > 0) {
    url = url + "?secret=" + urlEncode(tp.secret);
  }
  if (strlen(tp.issuer) > 0) {
    url = url + "&issuer=" + urlEncode(tp.issuer);
  }
  if (tp.period > 0) {
    url = url + "&period=" + String(tp.period);
  }
  if (tp.digit > 0) {
    url = url + "&digit=" + String(tp.digit);
  }
  if (debug) spp("URL",url);
  
  // QRコード表示
  int qh = ui.mwh.h;
  ui.lockCanvas();
  // ui._dst->fillScreen(TFT_BLACK);
  ui._dst->fillRect(ui.mxy.x,ui.mxy.y, ui.mwh.w,ui.mwh.h, TFT_BLACK);
  // ui._dst->qrcode(url, 0,0, qh, 6);
  ui._dst->qrcode(url, ui.mxy.x+ui.mwh.w/2-qh/2,ui.mxy.y, qh, 6);
  ui._dst->setTextColor(TFT_WHITE);
  // ui._dst->drawString("Issure:", qh+10, 10, &fonts::Font2);
  // ui._dst->drawString(tp.issuer, qh+20, 30, &fonts::Font2);
  // ui._dst->drawString("Account:", qh+10, 60, &fonts::Font2);
  // ui._dst->drawString(tp.account, qh+20, 80, &fonts::Font2);
  ui.unlockCanvas();

  // ボタン押し待ち
  waitPressButton();

  return res;
}

// --------------------------------------------------------------------------------------
// 【設定】 NTPで日時を同期してRTCに設定する
// --------------------------------------------------------------------------------------
bool functRtc() {
  String title = "NTP時刻同期";
  const std::vector<String> yesno = { "NO", "YES" };
  String message;
  int selected;
  bool success = false, abort = false;
  int kp = 0;
  Tms tms;

  // 確認画面
  message = "インターネットに接続して時刻を取得しますか?";
  selected = ui.selectDialog(yesno, 0, title, message, 72); // ダイアログ表示
  if (selected != 1) return false;

  // 画面枠とボタンの表示
  ui.selectNotice("CANCEL", title, "しばらくお待ちください...", 24, true); // 枠のみ表示
  int w = ui.mwh.w - 4;
  int x = ui.mxy.x + 2;
  int y2 = ui.mxy.y + 24 + 24;

  // プログレスバーの表示
  int py = y2;
  int ph = 22;
  progressbar(0, x,py, w,ph);
  y2 += ph + 3;

  // テキストスクロールの作成
  int h = 27;
  ui.setConsoleArea(x,y2, w,h);

  //　現在時刻を表示
  tms = getMultiDateTime(false);
  console("Now time:\n"+tms.ymd+"\n");

  // WiFi接続してNTPで同期する
  console("WiFi connecting\n");
  progressbar(10);
  if (wifiConnect()) {  // WiFi接続開始
    progressbar(40);
    // NTPサーバーへの接続
    console("ntp connecting..");
    configTzTime(NTP_TIMEZONE, NTP_SERVER1, NTP_SERVER2);
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && !abort) {
      abort = isConsoleAbort();
      if (kp++ % 100 == 0) console(".");
      delay(10);
    }
    progressbar(70);
    // NTP同期
    time_t t = time(nullptr) + 1;  // Advance one second.
    if (!abort) {
      console("done\nntp syncing..");
      while (t > time(nullptr) && !abort) {  /// Synchronization in seconds
        abort = isConsoleAbort();
        if (kp++ % 100 == 0) console(".");
        delay(10);
      }
    }
    progressbar(99);
    if (!abort) {
      console("done\n");
      DinMeter.Rtc.setDateTime(gmtime(&t)); // RTCに時刻をセットする
      success = true;
    }
    // NTP同期後の時刻を表示
    tms = getMultiDateTime(false);
    console("After NTP time:\n"+tms.ymd+"\n");
    // WiFi切断
    wifiDisconnect();
    console("WiFi disconnect\n");
    progressbar(100);
  }
  ui.clearConsoleArea(); // テキストスクロール範囲解除

  // 結果表示
  if (success) {
    delay(2000);
    ui.selectNotice("OK", title, "時刻同期に成功しました!", 64, false); // ダイアログ表示
  } else {
    console("*** ERROR ***\n");
    waitPressButton();  // ボタン押し待ち
  }

  // 以降、LCD描画がバグるので、抜けた後は再起動する
  // return success;
  return true;
}

// --------------------------------------------------------------------------------------
// 手動で時刻を設定する
// --------------------------------------------------------------------------------------
bool functAdjustTime() {
  String title = "手動時刻設定";
  const std::vector<String> yesno = { "NO", "YES" };
  String message;
  int selected;
  bool success = false, abort = false;
  char buff[8] = {0};
  int16_t dt[6];

  // 確認画面
  message = "手動で時刻を修正しますか? この先の作業は中断できません。";
  selected = ui.selectDialog(yesno, 0, title, message, 72); // ダイアログ表示
  if (selected != 1) return false;

  // 画面枠とボタンの表示
  ui.selectNotice("NEXT", title, "", 24, true); // 枠のみ表示

  // canvasの作成
  M5Canvas canvas(ui._dst);
  canvas.setColorDepth(16);
  canvas.createSprite(ui.mwh.w-20, 50);
  canvas.setTextColor(TFT_WHITE);

  // 現在時刻の取得
  Tms tms = getMultiDateTime(true);
  dt[0] = tms.tm->tm_year - 100;
  dt[1] = tms.tm->tm_mon + 1;
  dt[2] = tms.tm->tm_mday;
  dt[3] = tms.tm->tm_hour;
  dt[4] = tms.tm->tm_min;
  dt[5] = tms.tm->tm_sec;

  // 日時とカーソルの表示
  int encpos, n, dir = 0;
  bool btn = false;
  for (int hcur=0; hcur<6; hcur++) {
    btn = false;
    while (1) {
      // 日時表示
      canvas.fillSprite(TFT_BLACK);
      canvas.setCursor(0, 0);
      canvas.setFont(&fonts::Font4);  // 26px
      for (int i=0; i<6; i++) {
        sprintf(buff, ((i==0) ? "20%02d" : "%02d"), dt[i]);
        canvas.setTextColor((i==hcur) ? TFT_YELLOW : TFT_WHITE);
        canvas.print(buff);
        message = (i<=1) ? " / " : ((i>=3&&i<=4) ? " : " : "");
        canvas.setTextColor(TFT_WHITE);
        canvas.print(message);
        if (i == 2) canvas.print("\n");
      }
      // canvasの出力
      ui.lockCanvas();
      ui._dst->startWrite(); 
      canvas.pushSprite(ui.mxy.x+10, ui.mxy.y+40);
      ui._dst->endWrite();
      ui.unlockCanvas();
      // ロータリーエンコーダー操作
      while (1) {
        M5.update();
        encpos = DinMeter.Encoder.read();
        static int lastpos = encpos;
        if (encpos != lastpos && encpos % 2 == 0) { // エンコーダーが変化した場合
          dir = (lastpos < encpos) ? 1 : -1;
          n = dt[hcur] - dir;
          if (hcur == 0) n = (n < 0) ? 99 : ((n > 99) ? 0 : n);   // year
          else if (hcur == 1) n = (n < 1) ? 12 : ((n > 12) ? 1 : n);   // month
          else if (hcur == 2) n = (n < 1) ? 31 : ((n > 31) ? 1 : n);   // day
          else if (hcur == 3) n = (n < 0) ? 23 : ((n > 23) ? 0 : n);   // hour
          else if (hcur == 4 || hcur == 5) n = (n < 0) ? 59 : ((n > 59) ? 0 : n);   // min,sec
          dt[hcur] = n;
          lastpos = encpos;
          wctInterrupt(); // 無操作スリープ割込
          break;
        }
        if (m5BtnAwasReleased()) {  // ボタンを押した場合
          btn = true;
          break;
        }
        delay(10);
      }
      if (btn) break; // 次のカーソルに進む
    }
  }
  ui.encoderCacheClear();

  // RTCに保存
  struct tm tmLocal;
  tmLocal.tm_year = dt[0] + 100;
  tmLocal.tm_mon  = dt[1] - 1;
  tmLocal.tm_mday = dt[2];
  tmLocal.tm_hour = dt[3];
  tmLocal.tm_min  = dt[4];
  tmLocal.tm_sec  = dt[5];
  tmLocal.tm_isdst = -1;
  time_t timeLocal = mktime(&tmLocal);
  DinMeter.Rtc.setDateTime(gmtime(&timeLocal)); // RTCに時刻をセットする
  // RTCに設定した値を同期して確認
  tms = getMultiDateTime(true);
  if (debug) spp("Set Date", tms.ymd);

  return true;
}

// --------------------------------------------------------------------------------------
// 【設定】最後にEnterを自動送信するかどうか
// --------------------------------------------------------------------------------------
bool funcAutoEnter() {
  String title = "自動改行の設定";
  bool success = false;
  if (!conf.loaded) return false;

  // ダイアログの表示
  String description = "PCへの送信時、自動的に改行を入れるようにしますか?";
  std::vector<String> seltxt = { "<<", "No", "Yes" };
  int defval = (conf.autoEnter) ? 2 : 1;
  uint8_t selected = ui.selectDialog(seltxt, defval, title, description, 72);

  // 設定の保存
  if (selected != -1) {
    conf.autoEnter = (selected == 2);
    success = cf.saveConfig(conf);
  }
  return success;
}

// --------------------------------------------------------------------------------------
// 【設定】 BLEのペアリングをする
// --------------------------------------------------------------------------------------
bool funcPairing() {
  String title = "BLEペアリング";
  bool success = false, abort = false;
  String message;

  // 画面枠とボタンの表示
  if (!bleKeyboard.isConnected()) {  // ペアリングしてない場合
  message = "PCでBluetoothデバイスの設定画面を開き、\""+String(BLE_DEVICE_NAME)+"\" に接続してください";
    ui.selectNotice("CANCEL", title, message, 64, true); // 枠のみ表示
    while (!bleKeyboard.isConnected()) {
      if (isConsoleAbort()) break; // ボタンを押したら中断
      delay(50);
    }
  }
  success = bleKeyboard.isConnected();
  message = (success) ? "ペアリングに成功しました" : "ペアリングが失敗しました";
  ui.selectNotice("OK", title, message, 64, false); // ダイアログ表示
  return success;
}

// --------------------------------------------------------------------------------------
// 【設定】無操作時 自動的に電源をオフにする秒数を設定する
// --------------------------------------------------------------------------------------
bool funcAutoSleepAc() {
  bool success = false;
  String message;
  int boxnum, selected, orig = -1;

  // メニュー変数の作成
  MenuDef menu = {
    .title = "自動電源オフの設定",
    .select = 0,
    .selected = -1,
    .idx = 0,
    .cur = 0,
  };
  menu.lists.push_back({ 0, 0, "戻る", nullptr, "" });
  menu.lists.push_back({ 0, 0, "無制限", nullptr, "" });
  const uint16_t nums[] = { 10, 20, 30, 45, 60, 120, 240, 300, 600 };
  size_t numsSize = sizeof(nums) / sizeof(nums[0]);
  for (int i=0; i<numsSize; i++) {
    menu.lists.push_back({ 0, 0, String(nums[i])+String(" 秒"), nullptr, "" });
    if (nums[i] == conf.autoSleep) orig = i + 2;
  }
  if (conf.autoSleep == 0) orig = 1;

  // リストの選択
  message = "無操作時に自動的に電源をオフにする秒数を設定します";
  boxnum = (menu.lists.size() < 3) ? menu.lists.size() : 3;
  selected = ui.selectMenuList(&menu, orig, boxnum, message, 35);  // リスト形式のメニューを選択する

  // 設定の保存
  if (selected > 0 && selected < numsSize+2) {
    uint16_t trustCountNew = (selected == 1) ? 0 : nums[selected-2];
    if (trustCountNew != conf.autoSleep) {
      conf.autoSleep = trustCountNew;
      success = cf.saveConfig(conf);
    }
  }
  return success;
}

// --------------------------------------------------------------------------------------
// 【設定】PW送信後 自動的に電源をオフにする秒数を設定する
// --------------------------------------------------------------------------------------
bool funcAutoSleepPw() {
  bool success = false;
  String message;
  int boxnum, selected, orig = -1;

  // メニュー変数の作成
  MenuDef menu = {
    .title = "自動電源オフの設定",
    .select = 0,
    .selected = -1,
    .idx = 0,
    .cur = 0,
  };
  menu.lists.push_back({ 0, 0, "戻る", nullptr, "" });
  menu.lists.push_back({ 0, 0, "無制限", nullptr, "" });
  const uint16_t nums[] = { 3, 5, 10, 15, 20, 30, 60, 120, 240, 300 };
  size_t numsSize = sizeof(nums) / sizeof(nums[0]);
  for (int i=0; i<numsSize; i++) {
    menu.lists.push_back({ 0, 0, String(nums[i])+String(" 秒"), nullptr, "" });
    if (nums[i] == conf.autoKeyOff) orig = i + 2;
  }
  if (conf.autoKeyOff == 0) orig = 1;

  // リストの選択
  message = "パスワード送信後に自動的に電源をオフにする秒数を設定します";
  boxnum = (menu.lists.size() < 3) ? menu.lists.size() : 3;
  selected = ui.selectMenuList(&menu, orig, boxnum, message, 35);  // リスト形式のメニューを選択する

  // 設定の保存
  if (selected > 0 && selected < numsSize+2) {
    uint16_t trustCountNew = (selected == 1) ? 0 : nums[selected-2];
    if (trustCountNew != conf.autoKeyOff) {
      conf.autoKeyOff = trustCountNew;
      success = cf.saveConfig(conf);
    }
  }
  return success;
}

// --------------------------------------------------------------------------------------
// 【設定】 フォーマット FatFS
// --------------------------------------------------------------------------------------
bool funcFormatFatfs() {
  const String title = "本体フォーマット";
  const std::vector<String> yesno = { "NO", "YES" };
  String message;
  int selected;
  bool res;
  if (!conf.develop) return false;

  // チェック
  // if (conf.saveSecret == CONF_SECRET_NFC) {
  //   message = "エラー! 秘密鍵をNFCカードに保存している場合は、まず先に本体に移動してください。"
  //     "また、複数のNFCカードがある場合は、2枚目以降はフォーマットしてください。";
  //   ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
  //   return false;
  // }

  // 確認画面
  message = "全てのデータが削除されます。よろしいですか?";
  selected = ui.selectDialog(yesno, 0, title, message, 72); // ダイアログ表示
  if (selected != 1) return false;
  message = "【警告!!】\n本当に全てのデータが消えますがよろしいですか?";
  selected = ui.selectDialog(yesno, 0, title, message, 72); // ダイアログ表示
  if (selected != 1) return false;

  // FatFSのフォーマット
  if (debug) sp("Formatting FatFS");
  FFat.end();
  res = FFat.format();
  if (res) res = FFat.begin();
  else if (debug) sp("format error!");
  if (debug) spp("FFat.format", tf(res));
  // 結果表示
  message = (String) (res ? "フォーマット成功" : "フォーマット失敗") + "\n";
  if (res) {
    message += (String) "Total: "+(FFat.totalBytes() / 1024)+" KB\n";
    message += (String) "Used:  "+(FFat.usedBytes() / 1024)+" KB\n";
    message += (String) "Free:  "+(FFat.freeBytes() / 1024)+" KB\n";
  }
  ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示

  return res;
}

// --------------------------------------------------------------------------------------
// 【設定】 フォーマット NFC
// --------------------------------------------------------------------------------------
bool funcFormatNfc() {
  const String title = "NFCフォーマット";
  const std::vector<String> yesno = { "NO", "YES" };
  String message;
  int selected;
  bool res;

  // 事前チェック
  if (!status.unitRFIDready) {
    message = "エラー! RFIDリーダーが接続されていません";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // 確認画面
  message = "NFCカードのデータ（秘密鍵）が削除されます。よろしいですか?";
  selected = ui.selectDialog(yesno, 0, title, message, 72); // ダイアログ表示
  if (selected != 1) return false;
  message = "【警告!!】\n本当にNFCカードのデータが消えますがよろしいですか?";
  selected = ui.selectDialog(yesno, 0, title, message, 72); // ダイアログ表示
  if (selected != 1) return false;

  // NFCのフォーマット
  res = false;
  if (nfcMountSequence(title, 500)) {  // ダイアログ付き NFCマウント
    // NFCを非プロテクトモードに書き換える
    res = nfcChangeProtect(false, true);   // Protect Off, Format All
    if (debug) spp("NFC Protect", tf(res));
    nfcUnmountSequence(title, false);  // ダイアログ付き NFCアンマウント
  }
  message = (res) ? "NFCカードをフォーマットしました" : "エラー! NFCの消去に失敗しました";
  ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示

  return res;
}

// --------------------------------------------------------------------------------------
// 【設定】秘密鍵を移動する
// --------------------------------------------------------------------------------------
bool funcKeyMove() {
  String title = "秘密鍵の移動";
  String message = "";
  const std::vector<String> yesno = { "NO", "YES" };
  SecretStore newSaveSecret = CONF_SECRET_NONE;
  uint8_t selected;
  bool res;

  // 事前チェック
  if (!status.unitRFIDready || conf.saveSecret == CONF_SECRET_NONE) {
    message = "エラー! RFIDリーダーが接続されていません";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // 確認
  if (conf.saveSecret == CONF_SECRET_NFC) { // NFC to FatFS
    message = "NFC → 内部";
  } else if (conf.saveSecret == CONF_SECRET_FATFS) { // FatFS to NFC
    message = "内部 → NFC";
  }
  message += "\n本当に秘密鍵を移動しますか? 移動元のデータは消去されます";
  selected = ui.selectDialog(yesno, 0, title, message, 72); // ダイアログ表示
  if (selected != 1) return false;

  // トランザクションの開始　NFCマウント中の処理
  bool abort = false;
  message = "";
  while (1) {
    // 確認
    String str = (conf.saveSecret == CONF_SECRET_NFC) ? "秘密鍵が保存されている" : "新しい";
    message = str +" NFCカードを手元に用意してください";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示

    // NFCのマウント
    res = nfcMountSequence(title, 500);  // ダイアログ付き NFCマウント
    if (!res) {
      message = "エラー! NFCカードが認識できませんでした";
      abort = true;
      break;
    }

    // 秘密鍵が未ロードなら読み込む（NFC→メモリ）
    if (!status.unlock && conf.saveSecret == CONF_SECRET_NFC) {
      if (loadSecret(conf.saveSecret)) status.unlock = true;
    }
    if (!status.unlock) {
      message = "エラー! 秘密鍵が読み込めませんでした";
      abort = true;
      break;
    }

    // NFCをプロテクトモードに書き換える（移動先がNFCの場合）
    if (conf.saveSecret == CONF_SECRET_FATFS) { // FatFS to NFC
      res = nfcChangeProtect(true, false);   // Protect On, Format Quick
      if (debug) spp("NFC Protect", tf(res));
      if (!res) {
        message = "エラー! このNFCカードは書込みできません";
        abort = true;
        break;
      }
    }

    // 秘密鍵のコピー（メモリ→新保存先）
    if (conf.saveSecret == CONF_SECRET_NFC) { // NFC to FatFS
      newSaveSecret = CONF_SECRET_FATFS;
    } else if (conf.saveSecret == CONF_SECRET_FATFS) { // FatFS to NFC
      newSaveSecret = CONF_SECRET_NFC;
    }
    res = saveSecret(newSaveSecret);  // 秘密鍵の保存（メモリ→ストレージ）
    if (debug) spp("saveSecret", tf(res));
    if (!res) {
      message = "エラー! 秘密鍵が書き込めませんでした";
      abort = true;
      break;
    }

    // NFCのプロテクト解除（移動先がFatFSの場合）
    if (conf.saveSecret == CONF_SECRET_NFC) { // NFC to FatFS
      res = nfcChangeProtect(false, false);   // Protect Off, Format Quick
      if (debug) spp("NFC Protect", tf(res));
      if (!res) {
        message = "エラー! NFCカードのプロテクトを解除できませんでした";
        abort = true;
        break;
      }
    }

    // 秘密鍵の削除（旧保存先）
    res = deleteSecret(conf.saveSecret);
    if (debug) spp("deleteSecret", tf(res));
    if (!res) {
      message = "エラー! 古い秘密鍵の削除に失敗しました";
      abort = true;
      break;
    }
    break;
  }

  // トランザクションの終了
  nfcUnmountSequence(title, false);  // NFCのアンマウント（ダイアログなし）
  if (abort) {
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // 設定の変更と保存
  conf.saveSecret = newSaveSecret;
  res = cf.saveConfig(conf);
  if (debug) spp("Save Config", tf(res));

  // 結果表示
  message = "秘密鍵の移動が";
  message += (String) (res ? "成功しました" : "失敗しました") + "\n";
  ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
  return true;
}

// --------------------------------------------------------------------------------------
// 【設定】秘密鍵を複製する(NFC)
// --------------------------------------------------------------------------------------
bool funcKeyDuplicate() {
  String title = "NFCの秘密鍵を複製";
  String message = "";
  const std::vector<String> yesno = { "NO", "YES" };
  uint8_t selected;
  bool res;

  // 事前チェック
  if (!status.unitRFIDready || conf.saveSecret != CONF_SECRET_NFC) {
    message = "エラー! まずはじめに、秘密鍵はNFCカードに保存してください";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // 確認
  message = "【警告!!】\n新しいNFCカードは全てのデータが消去されます。よろしいですか?";
  selected = ui.selectDialog(yesno, 0, title, message, 72); // ダイアログ表示
  if (selected != 1) return false;

  // トランザクションの開始　NFCマウント中の処理
  bool abort = false;
  message = "";
  while (1) {
    // 確認
    message = "はじめに、\"オリジナル\" のNFCカードを用意してください";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示

    // NFCのマウント
    res = nfcMountSequence(title, 500);  // ダイアログ付き NFCマウント
    if (!res) {
      message = "エラー! NFCカードが認識できませんでした";
      abort = true;
      break;
    }

    // 秘密鍵を読み込む（NFC→メモリ）
    status.unlock = false;
    deleteSecret(CONF_SECRET_MEMORY);
    if (loadSecret(CONF_SECRET_NFC)) status.unlock = true;
    if (!status.unlock) {
      message = "エラー! NFCから秘密鍵が読み込めませんでした";
      abort = true;
      break;
    }

    // NFCのアンマウント
    nfcUnmountSequence(title, false);   // NFCのアンマウント（ダイアログなし）

    // 確認
    message = "次に、新しいNFCカードを用意してください";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示

    // NFCの再マウント
    res = nfcMountSequence(title, 500);  // ダイアログ付き NFCマウント
    if (!res) {
      message = "エラー! NFCカードが認識できませんでした";
      abort = true;
      break;
    }

    // NFCをプロテクトモードに書き換える
    res = nfcChangeProtect(true, false);   // Protect On, Format Quick
    if (debug) spp("NFC Protect", tf(res));
    if (!res) {
      message = "エラー! NFCカードをフォーマットできませんでした";
      message = "エラー! このNFCカードは書込みできません";
      abort = true;
      break;
    }

    // 秘密鍵の保存（メモリ→新保存先）
    res = saveSecret(CONF_SECRET_NFC);  // 秘密鍵の保存（メモリ→ストレージ）
    if (debug) spp("saveSecret", tf(res));
    if (!res) {
      message = "エラー! NFCに秘密鍵を書き込めませんでした";
      abort = true;
      break;
    }
    break;
  }

  // トランザクションの終了
  nfcUnmountSequence(title, false);  // NFCのアンマウント（ダイアログなし）
  if (abort) {
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // 結果表示
  message = "秘密鍵の複製が";
  message += (String) (res ? "成功しました" : "失敗しました") + "\n";
  ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
  return true;
}

// --------------------------------------------------------------------------------------
// 【設定】 開発者モードの有効化
// --------------------------------------------------------------------------------------
bool funcDevelop() {
  bool success = false;
  if (!conf.loaded) return false;

  // ダイアログの表示
  String title = "開発者モード";
  String description = "デバッグ用。開発者モードを有効にしますか?";
  std::vector<String> seltxt = { "<<", "NO", "YES" };
  int defval = (conf.develop) ? 2 : 1;
  uint8_t selected = ui.selectDialog(seltxt, defval, title, description, 72);

  // 設定の保存
  if (selected != -1) {
    conf.develop = (selected == 2);
    success = cf.saveConfig(conf);
  }
  return success;
}

// --------------------------------------------------------------------------------------
// 【設定】ストレージのHEXダンプ
// --------------------------------------------------------------------------------------
bool funcHexDump() {
  const String dirname = "/";
  String filename;
  File root = FFat.open(dirname);
  if (!root || !root.isDirectory()) return false;
  if (!conf.develop) return false;

  // メニュー変数の作成
  MenuDef menu = {
    .title = "HEXダンプ",
    .select = 0,
    .selected = -1,
    .idx = 0,
    .cur = 0,
  };
  menu.lists.push_back({ 0, 0, "<< 戻る", nullptr, "" });
  if (status.unitRFIDready) {
    menu.lists.push_back({ 0, 0, "NFC (非暗号化)", nullptr, "" });
    menu.lists.push_back({ 0, 0, "NFC (暗号化済)", nullptr, "" });
  }

  // ファイル一覧を取得
  if (debug) sp("File list");
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      if (debug) spf("  File: %s (%d)\n", file.name(), file.size());
      menu.lists.push_back({ 0, 0, file.name(), nullptr, "" });
    }
    file = root.openNextFile();
  }
  root.close();

  // ファイル選択
  String description = "ファイルを選択してください";
  int boxnum = (menu.lists.size() < 4) ? menu.lists.size() : 4;
  int selected = -1;
  while (selected != 0) {
    selected = ui.selectMenuList(&menu, -1, boxnum, description, 20);  // リスト形式のメニューを選択する
    if (status.unitRFIDready && selected >= 1 && selected <= 2) {
      if (nfcMountSequence(menu.title, 100)) {  // ダイアログ付き NFCマウント
        if (selected == 1) {  // 通常のNFCダンプ
          nfc.dumpAll();
        } else if (selected == 2) {  // プロテクトのかかったNFCをダンプする
          PhyAddr pa1 = nfc.addr2PhysicalAddr(SECRET_NFC_PARTITION_ADDR, nfc._cardType);
          PhyAddr pa2 = nfc.addr2PhysicalAddr(SECRET_NFC_PARTITION_ADDR + SECRET_SAVE_SIZE - 1, nfc._cardType);
          nfc.dumpAll(true, pa1.blockAddr, pa2.blockAddr);
        }
        nfcUnmountSequence(menu.title, false);  // ダイアログなし NFCアンマウント
      }
      menu.selected = -1;
    } else if (selected >= (status.unitRFIDready ? 3 : 1)) {
      hexDumpFatfs(dirname + menu.lists[selected].name);  // FatFSダンプ
      menu.selected = -1;
    }
    delay(10);
  }

  return true;
}

// --------------------------------------------------------------------------------------
// 【設定】静音モードの設定
// --------------------------------------------------------------------------------------
bool funcSetQuiet() {
  String title = "静音モード";
  bool success = false;
  if (!conf.loaded) return false;

  // ダイアログの表示
  String description = "BEEP音を鳴らします";
  std::vector<String> seltxt = { "<<", "OFF", "ON" };
  int defval = (conf.quiet) ? 2 : 1;
  uint8_t selected = ui.selectDialog(seltxt, defval, title, description, 72);

  // 設定の保存
  if (selected != -1) {
    conf.quiet = (selected == 2);
    success = cf.saveConfig(conf);
  }
  return success;
}
// --------------------------------------------------------------------------------------
// 【設定】JIS配列モードの設定
// --------------------------------------------------------------------------------------
bool funcSetJiskey() {
  String title = "JIS配列モード";
  bool success = false;
  if (!conf.loaded) return false;

  // ダイアログの表示
  String description = "BLEキーボードをUS配列で入力しますか? JIS配列にしますか? 【注意】JIS配列では入力できない文字があります。";
  std::vector<String> seltxt = { "<<", "US配列", "JIS配列" };
  int defval = (conf.keyJis) ? 2 : 1;
  uint8_t selected = ui.selectDialog(seltxt, defval, title, description, 72);

  // 設定の保存
  if (selected != -1) {
    conf.keyJis = (selected == 2);
    success = cf.saveConfig(conf);
  }
  return success;
}

// --------------------------------------------------------------------------------------
// ファイルの転送が可能なWebサーバーを起動 
// --------------------------------------------------------------------------------------
bool funcWebserver() {
  String title = "バックアップ";
  String url, message = "";

  // BASIC認証のパスワードを作成
  char buff[5];
  sprintf(buff, "%04d", random(0,9999));
  webAuthPasswd = buff;
  webAuthUser = "user";

  // WiFi接続
  message = "Wi-Fiに接続しています。このままお待ちください。";
  ui.selectNotice("wait...", title, message, 72, true); // ダイアログ表示　のみ

  if (!wifiConnect()) {  // WiFi接続開始
    message = "エラー! Wi-Fiに接続できませんでした";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
    return false;
  }

  // QRコード表示
  tickerUpdateUISkip = true;  // ステータスUIの自動更新を停止
  // url = "https://" + webAuthUser + ":" + webAuthPasswd + "@" +WiFi.localIP().toString();
  url = "https://" + WiFi.localIP().toString();
  if (debug) spp("URL", url);
  // message = "ブラウザで以下のURLにアクセスしてください。\n" + url;
  // ui.selectNotice("終了", title, message, 72, true); // ダイアログ表示　のみ
  int qh = ui.m5wh.h;
  ui.lockCanvas();
  ui._dst->fillScreen(TFT_BLACK);
  ui._dst->qrcode(url, 0,0, qh, 2);
  ui._dst->setTextColor(TFT_WHITE);
  ui._dst->drawString("Username:", qh+10, 10, &fonts::Font2);
  ui._dst->drawString(webAuthUser, qh+20, 30, &fonts::Font2);
  ui._dst->drawString("Password:", qh+10, 60, &fonts::Font2);
  ui._dst->drawString(webAuthPasswd, qh+20, 80, &fonts::Font2);
  ui.unlockCanvas();

  // Webサーバーを起動（BtnAを押すまで戻ってこない）
  httpsStartWebserver();

  // WiFi切断
  wifiDisconnect();

  // 以降、LCD描画がバグるので、抜けた後は再起動する
  // tickerUpdateUISkip = false;  // ステータスUIの自動更新を再開
  return true;
}

// =================================================================================

// --------------------------------------------------------------------------------------
// 【デバッグ】BLEで全ASCIIコードを送信する（キー刻印との不一致確認用） 
// --------------------------------------------------------------------------------------
bool funcDevelopSendAscii() {
  const String title = "DEBUG BLE全ASCII送信";
  const std::vector<String> yesno = { "NO", "YES" };
  if (!status.ble) return false;
  if (!conf.develop) return false;
  // 確認
  String message = "実行しますか?";
  int selected = ui.selectDialog(yesno, 0, title, message, 72); // ダイアログ表示
  if (selected != 1) return false;
  // 実行
  if(bleKeyboard.isConnected()) {
    for (uint8_t c=0x20; c<=0x7F; c++) {
      if (debug) spf("ASCII %02X (%c)\n", c, c);
      bleKeyboard.printf("%02X,", c);
      bleKeyboard.print((char)c);
      bleKeyboard.print((char)c);
      bleKeyboard.write(KEY_RETURN);
      delay(500);
    }
  }
  return true;
}

// --------------------------------------------------------------------------------------
// 【デバッグ】SSL証明書を削除して再生成する 
// --------------------------------------------------------------------------------------
bool funcRegenerateOreoreSSL() {
  const String title = "SSL証明書再生成";
  const std::vector<String> yesno = { "NO", "YES" };
  bool res;
  if (!status.ble) return false;
  if (!conf.develop) return false;
  // 確認
  String message = "実行しますか?";
  int selected = ui.selectDialog(yesno, 0, title, message, 72); // ダイアログ表示
  if (selected != 1) return false;
  // 実行
  res = deleteFile(FN_SSL_KEY);
  res = deleteFile(FN_SSL_CERT);
  res = httpsGenerateCertificate();
  if (debug) spp("httpsGenerateCertificate", tf(res));
  return res;
}
