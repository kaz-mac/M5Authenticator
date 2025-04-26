/*
  utility.h
  ユーティリティ系

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#pragma once
#include "common.h"
#include "secret.h"

#include <WiFi.h>
#include <FFat.h>
#include "mbedtls/md.h"
#include "mbedtls/aes.h"
#include <M5UnitQRCode.h>   // https://github.com/m5stack/M5Unit-QRCode

// メインで定義した変数を使用するためのもの
extern int8_t pinSda, pinScl;
#include "DinMeterUI.h"
extern DinMeterUI ui;
extern BleKeyboard  bleKeyboard;
bool m5BtnAwasReleased();

// プロテクトの範囲
const uint8_t  prtVaddrCL = SECRET_NFC_PARTITION_ADDR;  // 物理sector=1から48バイト単位後まで
const uint16_t prtSizeCL = ceil((double)SECRET_SAVE_SIZE / 48) * 48;
const uint8_t  prtVaddrUL = SECRET_NFC_PARTITION_ADDR;  // 物理page=6以降全て
const uint16_t prtSizeUL = ceil((double)SECRET_SAVE_SIZE / 16) * 16;

// JIS/US配列対応表 funcDevelopSendAscii()で調査
const uint8_t keyMapTable[][2] = {  // 入力したい文字, このキーを入力すればそれが出るやつ
  { '*', '\"' },
  { '\'', '&' },
  { ':', '\'' },
  { ')', '(' },
  // {'', ')' }, // 該当文字なし
  { '(', '*' },
  { '~', '+' },
  { '+', ':' },
  { '^', '=' },
  { '\"', '@' },
  { '@', '[' },
  { ']', '\\' },
  { '[', ']' },
  { '&', '^' },
  { '=', '_' },
  // {'', '`' }, // 該当文字なし
  { '`', '{' },
  { '}', '|' },
  { '{', '}' },
  // { '', '~' }, // 該当文字なし
  { '_', ' ' }, // アンダーバー _ は入力する手段なし
};


//--------------------------------------------------------------
// ボタン押し待ち
//--------------------------------------------------------------
bool waitPressButton(uint32_t wait) {   // default: 0
  uint32_t tm = millis() + wait;
  while (wait == 0 || millis() < tm) {
    M5.update();
    if (m5BtnAwasReleased()) return true;
    delay(10);
  }
  return false;
}

//--------------------------------------------------------------
// 内蔵ブザーを鳴らす
//--------------------------------------------------------------
void beep(int8_t pattern, bool foreGround) {  // default: BEEP_SHORT, true
  if (conf.quiet) return;
  if (pattern == BEEP_SHORT) {
    DinMeter.Speaker.tone(4000, 50);
    if (foreGround) delay(50);
  } else if (pattern == BEEP_LONG) {
    DinMeter.Speaker.tone(4000, 300);
    if (foreGround) delay(400);
  } else if (pattern == BEEP_DOUBLE) {
    DinMeter.Speaker.tone(4000, 50);
    delay(100);
    DinMeter.Speaker.tone(4000, 50);
    if (foreGround) delay(50);
  } else if (pattern == BEEP_ERROR) {
    for (int i=0; i<5; i++) {
      DinMeter.Speaker.tone(4000, 50);
      delay(100);
      if (i < 1 || foreGround) delay(50);
    }
  }
}

//--------------------------------------------------------------
// Wi-Fi接続する
//--------------------------------------------------------------
bool wifiConnect() {
  bool stat = false;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Wifi connecting.");
    for (int j=0; j<10; j++) {
      WiFi.disconnect();
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASS);  //  Wi-Fi APに接続
      for (int i=0; i<10; i++) {
        if (WiFi.status() == WL_CONNECTED) break;
        Serial.print(".");
        delay(500);
      }
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("connected!");
        Serial.println(WiFi.localIP());
        stat = true;
        break;
      } else {
        Serial.println("failed");
        WiFi.disconnect();
      }
    }
  }
  return stat;
}

//--------------------------------------------------------------
// Wi-Fi切断する
//--------------------------------------------------------------
void wifiDisconnect() {
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  Serial.print("Wifi disconnect.");
}

//--------------------------------------------------------------
// Unit-QR(I2C接続)を初期化する
//--------------------------------------------------------------
void qrcodeUnitInitI2C(uint32_t timeout) {
  if (!USE_QRCODE) return;
  if (status.unitQRready) return;

  uint32_t tmqrexp = millis();
  while (!status.unitQRready) {
    // if (qr.begin(&Serial2, UNIT_QRCODE_UART_BAUD, pinRX, pinTX)) { // for UART
    if (qr.begin(&Wire, UNIT_QRCODE_ADDR, pinSda, pinScl, 100000U)) { // for I2C
      qr.setTriggerMode(MANUAL_SCAN_MODE);
      status.unitQRready = true;
    } else if (tmqrexp+timeout < millis()) {  // timeout
      break;
    }
    delay(200);
  }
}

//--------------------------------------------------------------
// バイナリのdumpを出力する
//--------------------------------------------------------------
void printDump(const byte *data, size_t dataSize, String sepa, String cr, String crend) { // default -,\n,\n
  if (data == nullptr) return;
  size_t i;
  for (i=0; i<dataSize; i++) {
    spf("%02X%s", data[i], sepa);
    if (i % 16 == 15) spn(cr);
  }
  if (i % 16 != 15) spn(cr);
  spn(crend);
}
void printDump1Line(const byte *data, size_t dataSize) {
  printDump(data, dataSize, " ", "", "\n");
}

//--------------------------------------------------------------
// ファイルのHEXダンプを出力する
//--------------------------------------------------------------
void hexDumpFatfs(String filename) {
  uint8_t buffer[16];
  size_t bytesRead;
  size_t address = 0;

  // ファイルを開く
  File file = FFat.open(filename, FILE_READ);
  if (!file) return;
  spf("\nHex Dump of %s:\n", filename.c_str());

  // ダンプ開始
  while ((bytesRead = file.read(buffer, sizeof(buffer))) > 0) {
    spf("%08X: ", address);
    for (size_t i=0; i<bytesRead; i++) spf("%02X ", buffer[i]);
    for (size_t i=bytesRead; i<16; i++) spn("   ");
    spn(" | ");
    for (size_t i=0; i<bytesRead; i++) {
      char c = (buffer[i] >= 32 && buffer[i] <= 126) ? buffer[i] : '.';
      spn(c);
    }
    sp("");
    address += 16;
  }
  file.close();
  sp("");
}

//--------------------------------------------------------------
// 日時を取得して扱いやすいように様々な形式にする
//--------------------------------------------------------------
Tms getMultiDateTime(bool syncRtc) {  // default: false
  Tms tms;
  if (syncRtc) {
    DinMeter.Rtc.setSystemTimeFromRtc();
    configTzTime(NTP_TIMEZONE, NTP_SERVER1, NTP_SERVER2);   // TIME ZONEを再設定（RTCから取得すると消えるらしい）
  }
  tms.epoch = time(nullptr);
  tms.tm = localtime(&tms.epoch);
  char buff[32];
  sprintf(buff, "%04d-%02d-%02d %02d:%02d:%02d", 
    tms.tm->tm_year+1900, tms.tm->tm_mon+1, tms.tm->tm_mday, 
    tms.tm->tm_hour, tms.tm->tm_min, tms.tm->tm_sec);
  tms.ymd = String(buff);
  if (debug && syncRtc) spp("RTC_Sync",tms.ymd);
  return tms;
}

//--------------------------------------------------------------
// PASS/FAIL
//--------------------------------------------------------------
String tf(bool res) {
  return res ? "PASS" : "FAIL";
}

//--------------------------------------------------------------
// NFCのプロテクトを変更する
//--------------------------------------------------------------
bool nfcChangeProtect(bool protect, bool formatAll) {
  if (!nfc.isMounted()) return false;
  ProtectMode bfMode, afMode;
  bool res = false;

  // プロテクトの範囲
  uint8_t vaddr = (nfc.isClassic()) ? prtVaddrCL : prtVaddrUL;
  uint16_t size = (nfc.isClassic()) ? prtSizeCL : prtSizeUL;
  bfMode = (protect) ? PRT_NOPASS_RW : PRT_PASSWD_RW;
  afMode = (protect) ? PRT_PASSWD_RW : PRT_NOPASS_RW;
  if (debug) spf("varrd=%d size=%d\n", vaddr, size);

  // 既存のデータを消去する
  if (bfMode == PRT_NOPASS_RW) {
    res = nfc.format(formatAll);   // ULはNDEFダミー書込み、全領域0クリア(formatAll=true時)
    if (debug) spp("format", tf(res));
  }

  // プロテクト / アンプロテクトの実行
  AuthKey* newPasswd = (protect) ? &passwdNfc : nullptr;
  if (nfc.isUltralight() && !protect) {
    res = nfc.writeProtectUL(afMode, newPasswd, 255, true, bfMode);  // Ultralightで解除時は物理アドレス指定
    if (debug) spp("writeProtectUL", tf(res));
  } else {
    res = nfc.writeProtect(afMode, newPasswd, vaddr, size, bfMode);  // 仮想アドレス指定
    if (debug) spp("writeProtect", tf(res));
  }
  if (!res) return false;

  // Ultralightの場合は再マウントする
  if (nfc.isUltralight()) {
    nfc.unauthUL(afMode);
  }

  // 既存のデータを消去する
  if (afMode == PRT_NOPASS_RW) {
    res = nfc.format(formatAll);   // ULはNDEFダミー書込み、全領域0クリア(formatAll=true時)
    if (debug) spp("format", tf(res));
    if (res) {
      byte data[size] = {0};
      res = nfc.writeData(vaddr, data, sizeof(data));
    }
  }

  return res;
}

//--------------------------------------------------------------
// バイナリファイルを保存する(FatFS)
//--------------------------------------------------------------
bool saveFile(void *data, size_t dataSize, String filename) {
  // 書き込み
  File file = FFat.open(filename, FILE_WRITE);
  if (!file) {
    if (debug) sp("saveFile open failed "+filename);
    return false;
  }
  size_t wlen = file.write(reinterpret_cast<const uint8_t *>(data), dataSize);
  file.close();
  // チェック
  if (wlen != dataSize) {
    if (debug) sp("saveFile write failed "+filename);
    return false;
  }
  if (debug) spf("Write %d bytes to file: %s\n", wlen, filename.c_str());
  return true;
}

//--------------------------------------------------------------
// バイナリファイルを保存する(NFC)
//--------------------------------------------------------------
bool saveNfc(void *data, size_t dataSize, uint16_t vaddr, ProtectMode mode) {
  if (mode == PRT_AUTO) mode = nfc._lastProtectMode;
  bool res = nfc.writeData(vaddr, reinterpret_cast<void *>(data), dataSize, mode);
  if (!res) {
    if (debug) sp("saveNfc failed.");
    return false;
  }
  if (debug) spf("Write %d bytes to NFC: vaddr=%d\n", dataSize, vaddr);
  return true;
}

//--------------------------------------------------------------
// バイナリファイルを読み込む(FatFS)
//--------------------------------------------------------------
size_t loadFile(void *data, size_t dataSize, String filename) {
  File file = FFat.open(filename, FILE_READ);
  if (!file) {
    if (debug) sp("loadFile open failed "+filename);
    return 0;
  }
  size_t rlen = file.read(reinterpret_cast<uint8_t *>(data), dataSize);
  file.close();
  if (debug) spf("Read %d bytes from file: %s\n", rlen, filename.c_str());
  return rlen;
}

//--------------------------------------------------------------
// バイナリファイルを読み込む(NFC)
//--------------------------------------------------------------
size_t loadNfc(void *data, size_t dataSize, uint16_t vaddr, ProtectMode mode) {
  if (mode == PRT_AUTO) mode = nfc._lastProtectMode;
  bool res = nfc.readData(vaddr, reinterpret_cast<void *>(data), dataSize, mode);
  if (!res) {
    if (debug) sp("loadNfc failed.");
    return false;
  }
  if (debug) spf("Read %d bytes from NFC: vaddr=%d\n", dataSize, vaddr);
  return dataSize;
}

//--------------------------------------------------------------
// バイナリファイルを削除する(FatFS)
//--------------------------------------------------------------
bool deleteFile(String filename) {
  bool res = FFat.remove(filename);
  if (debug) spf("Delete %s file: %s\n", tf(res), filename.c_str());
  return res;
}

//--------------------------------------------------------------
// ファイルサイズを取得する（存在しなければ-1）
//--------------------------------------------------------------
int getFileSize(String filename) {
  int fileSize = -1;
  File file = FFat.open(filename, FILE_READ);
  if (file) {
    fileSize = (int) file.size();
  }
  file.close();
  return fileSize;
}

//--------------------------------------------------------------
// SHA256でハッシュ化する  output:32byte=256bit
//--------------------------------------------------------------
bool sha256(String input, byte* output, size_t outputSize) {
  if (input == nullptr || output == nullptr || outputSize < 32) return false;

  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (mdInfo == nullptr) {
    mbedtls_md_free(&ctx);
    return false;
  }
  if (mbedtls_md_setup(&ctx, mdInfo, 0) != 0) {
    mbedtls_md_free(&ctx);
    return false;
  }
  mbedtls_md_starts(&ctx);
  mbedtls_md_update(&ctx, (const unsigned char*)input.c_str(), input.length());
  mbedtls_md_finish(&ctx, output);
  mbedtls_md_free(&ctx);
  return true;
}

//--------------------------------------------------------------
// IVを取得(生成)する  output:16byte=128bit　一意な値
//--------------------------------------------------------------
bool getIV(uint8_t* iv, size_t ivSize, String addText) {
  if (iv == nullptr || ivSize != 16) return false;

  // MACアドレスと任意のフレーズをハッシュ化する
  String text = WiFi.macAddress() + addText;
  if (debug) spp("getIV HASH source text",text);
  uint8_t hash[32];
  if (!sha256(text, hash, sizeof(hash))) return false;
  memcpy(iv, hash, 16);
  return true;
}

//--------------------------------------------------------------
// 秘密鍵暗号化用の秘密鍵を生成する  32byte=256bit　一意な値
//--------------------------------------------------------------
bool getBSecret(uint8_t* bsecret, size_t bsecretSize, String addText) {
  if (bsecret == nullptr || bsecretSize != 32) return false;

  // MACアドレスと任意のフレーズをハッシュ化する
  String text = WiFi.macAddress() + addText;
  if (debug) spp("getBSecret HASH source text",text);
  uint8_t hash[32];
  if (!sha256(text, hash, sizeof(hash))) return false;
  memcpy(bsecret, hash, 32);
  return true;
}

//--------------------------------------------------------------
// AES 256bitで暗号化する
//--------------------------------------------------------------
size_t encrypt(byte* data, size_t dataSize, const byte* iv, const byte* key, byte* encryptedData) {
  if (data == nullptr || iv == nullptr || key == nullptr || encryptedData == nullptr) return 0;
  size_t encsize = 0;
  byte ivCopy[16];

  // AES初期化
  memcpy(ivCopy, iv, 16);
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if (mbedtls_aes_setkey_enc(&aes, key, 256) != 0) {
    mbedtls_aes_free(&aes);
    return 0;
  }

  // 暗号化処理
  for (int i=0; i<dataSize; i+=16) {
    size_t blockSize = (dataSize-i >= 16) ? 16 : (dataSize-i);
    byte block[16] = {0};
    memcpy(block, data+i, blockSize);
    if (mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, 16, ivCopy, block, encryptedData+i) != 0) {
      mbedtls_aes_free(&aes);
      return 0;
    }
    encsize += 16;
  }
  mbedtls_aes_free(&aes);
 
  return encsize;
}

//--------------------------------------------------------------
// AES 256bitで復号化する
//--------------------------------------------------------------
size_t decrypt(byte* decryptedData, const byte* encryptedData, size_t encryptedSize, const byte* iv, const byte* key) {
  if (decryptedData == nullptr || encryptedData == nullptr || iv == nullptr || key == nullptr)  return 0;
  byte ivCopy[16];
  size_t decsize = 0;

  // AES初期化
  memcpy(ivCopy, iv, sizeof(ivCopy));
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  if (mbedtls_aes_setkey_dec(&aes, key, 256) != 0) {
    mbedtls_aes_free(&aes);
    return 0;
  }
  
  // 復号化処理
  for (int i=0; i<encryptedSize; i+=16) {
    byte block[16];
    if (mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, 16, ivCopy, encryptedData+i, block) != 0) {
      mbedtls_aes_free(&aes);
      return 0;
    }
    memcpy(decryptedData+i, block, 16);
    decsize += 16;
  }
  mbedtls_aes_free(&aes);

  return decsize;
}

//--------------------------------------------------------------
// 秘密鍵を読み込む（ストレージ→メモリ）
//--------------------------------------------------------------
bool loadSecret(SecretStore device) {
  byte bsecret[32];
  SecretDef sdef;
  byte deced[32];
  size_t rlen = 0;

  // ストレージからの読み込み
  getBSecret(bsecret, sizeof(bsecret) , BSECRET_PHRASE);   // 秘密鍵を複合化するための秘密鍵を生成する
  if (device == CONF_SECRET_FATFS) {  // FatFSの場合
    if (getFileSize(FN_SECRETENC) == sizeof(SecretDef)) {
      rlen = loadFile(&sdef, sizeof(SecretDef), FN_SECRETENC);
    }
  } else if (device == CONF_SECRET_NFC) { // NFCの場合
    rlen = loadNfc(&sdef, sizeof(SecretDef), SECRET_NFC_PARTITION_ADDR, PRT_PASSWD_RW);
  }
  if (rlen != SECRET_SAVE_SIZE || memcmp(sdef.magic, SecretMagic, sizeof(sdef.magic)) != 0) {
    if (debug) sp("Load Secret failed! file cannot read.");
    return false;
  }

  // 複合化してメモリに格納する
  if (debug) {
    spn("Loaded Secret-enc: ");
    printDump1Line(sdef.secretEnc, sizeof(SecretDef::secretEnc));
  }
  size_t declen = decrypt(deced, sdef.secretEnc, sizeof(SecretDef::secretEnc), status.iv, bsecret);  // 複合化
  if (debug) {
    spn("Loaded Secret: ");
    printDump1Line(deced, sizeof(deced));
  }
  if (declen == sizeof(deced)) {
    memcpy(status.secret, deced, sizeof(status.secret));
    if (debug) sp("Load Secret success!");
  } else {
    if (debug) sp("Load Secret failed! file cannot decrypt.");
    return false;
  }

  return true;
}

//--------------------------------------------------------------
// 秘密鍵を保存する（メモリ→ストレージ）
//--------------------------------------------------------------
bool saveSecret(SecretStore device) {
  bool res;
  byte bsecret[32];
  SecretDef sdef;
  memcpy(sdef.magic, SecretMagic, sizeof(sdef.magic));
  size_t enclen;

  // メモリ上の秘密鍵を暗号化する
  getBSecret(bsecret, sizeof(bsecret) , BSECRET_PHRASE);   // 秘密鍵暗号化用の秘密鍵を生成する
  if (debug) {
    spn("B-Secret: ");
    printDump1Line(bsecret, sizeof(bsecret));
  }
  enclen = encrypt(status.secret, sizeof(status.secret), status.iv, bsecret, sdef.secretEnc);
  if (debug) {
    spn("Secret-enc: ");
    printDump1Line(sdef.secretEnc, enclen);
  }
  if (enclen != 32) {
    if (debug) sp("Save Secret failed! secret cannot encrypt.");
    return false;
  }

  // ストレージに保存する
  res = false;
  if (device == CONF_SECRET_FATFS) {  // FatFSの場合
    res = saveFile(&sdef, sizeof(SecretDef), FN_SECRETENC);
    if (debug) spp("saveFile", tf(res));
  } else if (device == CONF_SECRET_NFC && nfc.isMounted()) { // NFCの場合
    // res = nfcChangeProtect(true, false);   // Protect On, Format Quick
    // if (debug) spp("NFC Protect", tf(res));
    // if (res) {
      res = saveNfc(&sdef, sizeof(SecretDef), SECRET_NFC_PARTITION_ADDR, PRT_PASSWD_RW);
      if (debug) spp("saveNfc", tf(res));
    // }
  }
  if (!res) {
    if (debug) sp("Save Secret failed! secret cannot save.");
    return false;
  }

  // 再読み込み(複合化)して同一値になるかチェックする
  byte secretOrig[32];
  memcpy(secretOrig, status.secret, sizeof(secretOrig));
  memset(status.secret, 0, sizeof(secretOrig));
  if (!loadSecret(device)) {
    if (debug) sp("Verify Secret: cannot load!");   
    return false;
  }
  if (memcmp(secretOrig, status.secret, sizeof(secretOrig)) == 0) {
    if (debug) sp("Verify Secret: compare success!");
  } else {
    memcpy(status.secret, secretOrig, sizeof(secretOrig));
    if (debug) sp("Verify Secret: compare failed!");
    return false;
  }

  return true;
}

//--------------------------------------------------------------
// 秘密鍵を削除する（メモリ or ストレージ）
//--------------------------------------------------------------
bool deleteSecret(SecretStore device, ProtectMode mode) {
  bool res = false;
  byte dummy[SECRET_SAVE_SIZE] = {0};
  if (device == CONF_SECRET_NONE) { // メモリの場合
    memset(status.secret, 0, sizeof(status.secret));
    res = true;
    if (debug) sp("deleteSecret: memory");
  } else if (device == CONF_SECRET_NFC) {  // FatFSの場合
    if (mode == PRT_AUTO) mode = nfc._lastProtectMode;
    res = saveNfc(dummy, sizeof(dummy), SECRET_NFC_PARTITION_ADDR, mode);   // NFCにダミーデータを上書き
    if (debug) spp("deleteSecret: NFC", tf(res));
  } else if (device == CONF_SECRET_FATFS) { // NFCの場合
    res = saveFile(dummy, sizeof(dummy), FN_SECRETENC); // FatFSにダミーデータを保存
    res = deleteFile(FN_SECRETENC); // ファイルを削除
    if (debug) spp("deleteSecret: FatFS", tf(res));
  }
  return res;
}

// --------------------------------------------------------------------------------------
// 再起動
// --------------------------------------------------------------------------------------
void restart() {
  ESP.restart();
}

// --------------------------------------------------------------------------------------
// 空きメモリ情報を出力
// --------------------------------------------------------------------------------------
void debug_free_memory(String str) {
  if (!debug) return;
  sp("## "+str);
  spf("heap_caps_get_free_size(MALLOC_CAP_DMA): %d\n", heap_caps_get_free_size(MALLOC_CAP_DMA) );
  spf("heap_caps_get_largest_free_block(MALLOC_CAP_DMA): %d\n", heap_caps_get_largest_free_block(MALLOC_CAP_DMA) );
  spf("heap_caps_get_free_size(MALLOC_CAP_SPIRAM): %d\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) );
  spf("heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM): %d\n\n", heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) );
}

//--------------------------------------------------------------
// シリアルポートと画面の両方にprintする
//--------------------------------------------------------------
void console(String str) {
  spn(str);
  auto fontorig = ui._dst->getFont();
  ui.lockCanvas();
  ui._dst->setFont(&fonts::Font0);
  ui._dst->print(str);
  ui._dst->setFont(fontorig);
  ui.unlockCanvas();
}

//--------------------------------------------------------------
// ボタンが押されたら中断と判断する　ミニコンソール用
//--------------------------------------------------------------
bool isConsoleAbort(bool showprint) {   // default: true
  bool res = false;
  M5.update();
  if (m5BtnAwasReleased()) {
    if (showprint) console("Abort!!\n");
    res = true;
  }
  return res;
}

//--------------------------------------------------------------
// プログレスバーを表示する
//--------------------------------------------------------------
void progressbar(int per, int pbx, int pby, int pbw, int pbh) {   // default: pbx,y,w,h=-1
  static int bx, by, bw, bh;
  int x, y, w, h;
  int th = 10;
  if (pbx > 0) bx = pbx;
  if (pby > 0) by = pby;
  if (pbw > 0) bw = pbw;
  if (pbh > 0) bh = pbh;
  // バーの表示
  x = bx + 15;
  y = by + th;
  w = bw - 30;
  h = bh - th;
  ui.lockCanvas();
  ui._dst->drawRect(x,y, w,h, ui.rgb565(0xBBBBBB));
  int x2 = map(per, 0,100, x+1, x+1+w-2);
  ui._dst->fillRect(x+1,y+1, (x2-x),h-2, TFT_GREEN);
  // テキストの表示
  ui._dst->fillRect(bx,by, bw,th, TFT_BLACK);
  auto datumorig = ui._dst->getTextDatum();
  ui._dst->setTextDatum(BC_DATUM);
  ui._dst->drawString(String(per)+"%", x2,y-1, &fonts::Font0);
  ui._dst->setTextDatum(datumorig);
  ui.unlockCanvas();
}

//--------------------------------------------------------------
// ダイアログ付き NFCマウント
//--------------------------------------------------------------
bool nfcMountSequence(String title, uint32_t waitms) {
  // ダイアログを表示して、NFCが置かれるまで待つ。ボタンが押されたら中断
  beep(BEEP_DOUBLE);
  uint32_t tm = millis();
  bool blink = false;
  while (1) {
    if (tm <= millis()) {
      blink = !blink;
      ui.imageNotice((blink ? IMAGE_nfc1 : IMAGE_nfc0), title, true); // 画像ダイアログ表示
      tm = millis() + 300;
    }
    if (nfc.mountCard(1)) break;      // マウント待ち (1ms待機=すぐ抜ける)
    if (waitPressButton(100)) break;  // ボタン押し待ち (100ms待機)
  }
  bool res = nfc.isMounted();
  // 認識したらダイアログを変更
  if (res) {
    if (debug) sp("NFC mounted");
    String message = "NFCカードを動かさないでください";
    ui.selectNotice("CANCEL", title, message, 72, true); // ダイアログ表示
  }
  beep(res ? BEEP_SHORT : BEEP_ERROR);
  if (waitms > 0) waitPressButton(waitms);
  return res;
}

//--------------------------------------------------------------
// ダイアログ付き NFCアンマウント
//--------------------------------------------------------------
void nfcUnmountSequence(String title, bool dialog) {
  // ダイアログを表示する
  if (dialog) {
    String message = "NFCカードを外してください";
    ui.selectNotice("OK", title, message, 72, false); // ダイアログ表示
  }

  // NFCをアンマウントする
  nfc.unmountCard();
  beep(BEEP_LONG);
  if (debug) sp("NFC unmounted");
  return;
}

//--------------------------------------------------------------
// M5Unit-QR読み取り前にゴミデータが入ってたらクリアする
//--------------------------------------------------------------
void qrBufferClear() {
  if (!status.unitQRready) return;
  // if (qr.available()) String devnull = qr.getDecodeData();
  if (qr.getDecodeReadyStatus() == 1) {
    uint8_t buff[512] = {0};
    size_t len = qr.getDecodeLength();
    if (len > sizeof(buff)-1) len = sizeof(buff)-1;
    qr.getDecodeData(buff, len);
    if (debug) sp("clear old QR-data");
  }
}

//--------------------------------------------------------------
// US配列のキーボードでJISに対応させる
//--------------------------------------------------------------
uint8_t keycodeJisToUs(uint8_t ascii) {
  size_t num = sizeof(keyMapTable) / sizeof(keyMapTable[0]);
  for (size_t i=0; i<num; i++) {
    if (keyMapTable[i][0] == ascii) {
      ascii = keyMapTable[i][1];
      break;
    }
  }
  return ascii;
}

//--------------------------------------------------------------
// バッテリーの残量を取得する（これは正常に機能しない）
//--------------------------------------------------------------
uint8_t getBatteryLevel(bool exdebug) {
  int16_t v;
  int32_t lv, ic, crg;
  int r;
  float v2;
  lv = DinMeter.Power.getBatteryLevel();  // USB/バッテリー問わず常に100が返る
  // デバッグ  バッテリー情報 https://docs.m5stack.com/ja/arduino/m5unified/power_class
  if (exdebug) {
    int16_t v = DinMeter.Power.getBatteryVoltage();   // USB駆動時は4.4Vくらいだが、バッテリー駆動時は6Vというおかしな値が出ている
    int32_t ic = DinMeter.Power.getBatteryCurrent();  // 常に0が返る
    int32_t crg = DinMeter.Power.isCharging();  // 常に2=unknownが返る
    int r = analogRead(10); // バッテリー駆動時は4095と振り切っていた。
    float v2 = (float)analogReadMilliVolts(10) * 2 / 1000;
    if (debug) {
      spf("Power: v=%d lv=%d ic=%d crg=%d, r=%d, v2=%.2f\n", v, lv, ic, crg, r, v2);
    }
    // 結論。M5DinMeterでバッテリーの電圧測定は諦める(;_;)
  }
  return lv;
}
