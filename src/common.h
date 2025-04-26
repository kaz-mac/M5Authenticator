/*
  common.h
  共通の定義など

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#pragma once
#include "NfcEasyWriter.h"

// 各種定数
const String FN_SECRETENC = "/secret_enc.bin";  // 暗号化した秘密鍵
const String FN_SSL_KEY = "/ssl_private.der";   // Webサーバーの秘密鍵
const String FN_SSL_CERT = "/ssl_cert.crt";     // Webサーバーの証明書
#define BEEP_SHORT   1
#define BEEP_LONG    2
#define BEEP_DOUBLE  3
#define BEEP_ERROR   4
#define SECRET_NFC_PARTITION_ADDR  0  // NFCに格納する先頭アドレス(仮想アドレスで指定)
#define SECRET_SAVE_SIZE  40    // 秘密鍵ファイルのサイズ（秘密鍵32+マジックナンバー4+RFUI 4）
const byte SecretMagic[4] = { 0x9E, 0x36, 0xAE, 1 }; // 秘密鍵ファイルのマジックナンバー[3] + バージョン

// メニューの項目
enum Itype : uint8_t {
  none, back, setting, goRestart, goPowerdown, subtitle
};
// 秘密鍵の保存先
enum SecretStore : uint8_t {
  CONF_SECRET_NONE,
  CONF_SECRET_FATFS,  // 本体FatFS
  CONF_SECRET_NFC,    // NFCカード
  CONF_SECRET_MEMORY, // メモリ（削除時に指定する用）
};

// 設定情報（FATFSにも同じ内容が保存される）
struct ConfigInfo { 
  bool        loaded;      // 設定が読み込まれた（フラグ。ファイルでは常にfalse）
  uint8_t     version;     // 設定ファイルのバージョン
  SecretStore saveSecret;  // 秘密鍵の保存先（0=未設定、1=内蔵FLASH、2=NFC）
  bool        autoEnter;   // OTP入力後に自動的にEnterを入力する
  uint16_t    autoSleep;   // 無操作時の自動スリープ(秒)
  bool        quiet;       // 静音モード
  bool        keyJis;      // JIS配列変換モード
  bool        develop;     // 開発者モード
  uint16_t    autoKeyOff;  // OTP送信後の自動スリープ(秒)
  byte        rfui[32];    // 予約
};

// 状態表示用の情報
struct StatusInfo { 
  bool     ble = false;           // BLEで接続中
  uint8_t  rssi = 0;              // 電波強度（未使用）
  bool     unlock = false;        // 秘密鍵が有効
  uint8_t  battery = 0;           // バッテリー残量
  bool     unitQRready = false;   // UNIT-QRCODEの接続状態
  bool     unitRFIDready = false; // UNIT-RFID 2の接続状態
  byte     iv[16] = {0};          // AES暗号化の初期ベクトル 128bit
  byte     secret[32] = {0};      // 秘密鍵 256bit
};

// 日付情報
struct Tms {
  String ymd = "";
  time_t epoch = 0;
  struct tm* tm;
};

// 保存する秘密鍵の構造体
struct SecretDef {
  byte magic[4] = {0};
  byte secretEnc[32] = {0};
  byte rfui[4] = {0};    // 予約
};

// TOTP URIのパース結果、FatFS保存形式
struct TotpParams {
  uint8_t version = 1;
  char    issuer[32] = {0};
  char    account[32] = {0};
  char    secret[64] = {0};
  uint8_t algorithm = 1;
  uint8_t digit = 6;
  uint8_t period = 30;
  byte    rfui[28] = {0};    // 予約
};
struct TotpParamsList {
  String filename;
  TotpParams tp;
};

//==============================================================
// function.h 各メニューに対応するサブルーチン
//==============================================================

// 初回セットアップ
bool funcInitialSetup();

// メインメニュー
bool funcOtpShow();         // ワンタイムパスワードをPCに送信する 
bool funcBarcodeReader();   // Barcode/QR-codeを読み込んでキータイプする 
bool funcClock();           // 現在時刻を表示する
bool funcPoweroff();        // 電源オフ

// 機能メニュー
bool funcAddOtp();      // OTPを追加する
bool funcDelOtp();      // OTPを削除する
bool funcExportOtp();   // OTPのエクスポート
bool functRtc();        // NTPで日時を同期してRTCに設定する
bool funcPairing();     // BLEのペアリングをする
bool funcFormatFatfs(); // フォーマット FatFS
bool funcFormatNfc();   // フォーマット NFC
bool funcKeyMove();     // 秘密鍵を移動する
bool funcKeyDuplicate();// 秘密鍵を複製する(NFC)
bool funcHexDump();     // ストレージのHEXダンプ

// 設定メニュー
bool functAdjustTime(); // 手動で時刻を設定する
bool funcAutoEnter();   // 最後にEnterを自動送信するかどうか
bool funcAuthMode();    // 認証方式を設定する
bool funcAutoSleepAc(); // オートスリープ時間 無操作時
bool funcAutoSleepPw(); // オートスリープ時間 PW送信後
bool funcDevelop();     // 開発者モードの有効化
bool funcSetQuiet();    // 静音モードの設定
bool funcSetJiskey();   // JIS配列モードの設定

// その他
bool funcDevelopSendAscii();    // BLEで全ASCIIコードを送信する（キー刻印との不一致確認用） 
bool funcRegenerateOreoreSSL(); // SSL証明書を削除して再生成する 


//==============================================================
// totp.h TOTP認証関連のサブルーチン
//==============================================================

// URIの処理
String urlDecode(String str);   // URLデコード
bool parseUriOTP(TotpParams* tp, String uri);   // OTPのURIをパースする

// OTP生成
String getTotp(TotpParams* tp, time_t epoch);   // 指定時刻のワンタイムパスワードを取得する

// ファイル操作関連
String makeOtpFilename(TotpParams* tp);    // OTPの保存ファイル名を作成する
bool saveOtpFile(String filename, TotpParams* tp);    // FatFSにOTPを保存する
bool loadOtpFile(String filename, TotpParams* tp, bool decryptSecret); // FatFSからOTPを読み込む
std::vector<String> listOtpFiles();   // FatFSのOTPファイル名一覧を取得する
int listAllOtpFiles(std::vector<TotpParamsList> *tps, bool decryptSecret);  // FatFSのOTP情報を全て取得する


//==============================================================
// utility.h ユーティリティ系
//==============================================================

// ユーザーインターフェース関連1
bool waitPressButton(uint32_t wait=0);    // ボタン押し待ち
void beep(int8_t pattern=BEEP_SHORT, bool foreGround=true);   // 内蔵ブザーを鳴らす
void console(String str); // シリアルポートとLCDの両方にprintする
bool isConsoleAbort(bool showprint=true); // ボタンが押されたら中断と判断する　ミニコンソール用
void progressbar(int per, int pbx=-1, int pby=-1, int pbw=-1, int pbh=-1);  // プログレスバーを表示する

// ユーザーインターフェース関連2
bool nfcMountSequence(String title, uint32_t waitms=0); // ダイアログ付き NFCマウント
void nfcUnmountSequence(String title, bool dialog);     // ダイアログ付き NFCアンマウント

// Wi-Fi関連
bool wifiConnect();     // Wi-Fi接続
void wifiDisconnect();  // Wi-Fi切断

// 外部接続デバイス関連
void qrcodeUnitInitI2C(uint32_t timeout=0);   // Unit-QR(I2C接続)を初期化する
void qrBufferClear();   // M5Unit-QR読み取り前にゴミデータが入ってたらクリアする

// ファイルシステム関連(NFC含む)
bool nfcChangeProtect(bool protect, bool formatAll=false);  // NFCのプロテクトを変更する
bool saveFile(void *data, size_t dataSize, String filename);  // バイナリファイルを保存する(FatFS)
bool saveNfc(void *data, size_t dataSize, uint16_t vaddr, ProtectMode mode=PRT_AUTO);  // バイナリファイルを保存する(NFC)
size_t loadFile(void *data, size_t dataSize, String filename);  // バイナリファイルを読み込む(FatFS)
size_t loadNfc(void *buffer, size_t bufferSize, uint16_t vaddr, ProtectMode mode=PRT_AUTO);  // バイナリファイルを読み込む(NFC)
bool deleteFile(String filename);   // バイナリファイルを削除する(FatFS)
int getFileSize(String filename);   // ファイルサイズを取得する(FatFS)（存在しなければ-1）

// 暗号化関連
bool sha256(String input, byte* output, size_t outputSize);   // SHA256でハッシュ化する  output:32byte=256bit
bool getIV(uint8_t* iv, size_t ivSize, String addText);   // IVを取得(生成)する  output:16byte=128bit
bool getBSecret(uint8_t* bsecret, size_t bsecretSize, String addText);  // 秘密鍵暗号化用の秘密鍵を生成する  32byte=256bit
size_t encrypt(byte* data, size_t dataSize, const byte* iv, const byte* key, byte* encryptedData);  // AES 256bitで暗号化
size_t decrypt(byte* decryptedData, const byte* encryptedData, size_t encryptedSize, const byte* iv, const byte* key);  // AES 256bitで複合化

// 秘密鍵の操作関連
bool loadSecret(SecretStore device);    // 読み込み（ストレージ→メモリ）
bool saveSecret(SecretStore device);    // 書き込み（メモリ→ストレージ）
bool deleteSecret(SecretStore device, ProtectMode mode=PRT_AUTO);  // 削除（メモリ or ストレージ）

// システム関連
void restart();   // ESP32をリセット
void debug_free_memory(String str);   // 空きメモリ情報を出力
uint8_t getBatteryLevel(bool exdebug);  // バッテリーの残量を取得する（これは正常に機能しない）

// ユーティリティ
Tms getMultiDateTime(bool syncRtc=false);   // 日時を取得して扱いやすいように様々な形式にする
uint8_t keycodeJisToUs(uint8_t ascii);  // US配列のキーボードでJISに対応させる

// デバッグ関連
void printDump(const byte *data, size_t dataSize, String sepa="-", String cr="\n", String crend="\n");  // バイナリのdumpを出力する
void printDump1Line(const byte *data, size_t dataSize);   // バイナリのdumpを出力する
void hexDumpFatfs(String filename);   // ファイルのHEXダンプを出力する
String tf(bool res);  // PASS/FAIL


//==============================================================
// HTTPS対応Webサーバーのサブルーチン
//==============================================================

// サーバー関連
bool httpsGenerateCertificate();    // オレオレ証明書を作成してFatFSに保存する
bool loadInitServer();    // FatFSからSSL秘密鍵と証明書をロードしてHTTPSサーバーを初期化する
bool httpsStartWebserver();    // Webサーバーを起動し、ループ処理を継続し、ボタンを押したらWebサーバー終了する

// ユーティリティ
bool checkSafeFilename(String filename);    // ファイル名が安全かチェックする
String urlEncode(String inputText);   // URLエンコード

// コンテンツ（webserver.hで定義する）
// void handleRoot(HTTPRequest * req, HTTPResponse * res);   // TOPページ
// void handle404(HTTPRequest * req, HTTPResponse * res);    // 404ページ
// void handleFiles(HTTPRequest * req, HTTPResponse * res);  // ファイル一覧を返すJSON
// void handleDownload(HTTPRequest * req, HTTPResponse * res);   // ダウンロード
// void handleUpload(HTTPRequest * req, HTTPResponse * res);   // アップロード
// void handleDelete(HTTPRequest * req, HTTPResponse * res);   // 削除

