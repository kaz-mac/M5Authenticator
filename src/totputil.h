/*
  totp.h
  TOTP認証関連のサブルーチン

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT

  参考ページ
    TOTPのURIの仕様 https://github.com/google/google-authenticator/wiki/Key-Uri-Format
    TOTP QRジェネレーター https://stefansundin.github.io/2fa-qr/
    Base32エンコーダー https://dencode.com/ja/string/base32
*/
#pragma once

#include "common.h"

#include <Base32-Decode.h>


//--------------------------------------------------------------
// URLデコード
//--------------------------------------------------------------
String urlDecode(String str) {
	String decoded = "";
	for (int i=0; i<str.length(); i++) {
	  char c = str.charAt(i);
	  if (c == '%' && i+2 < str.length()) {
      String hex = str.substring(i+1, i+3);
      char decodedChar = (char) strtol(hex.c_str(), NULL, 16);
      decoded += decodedChar;
      i += 2;
	  } else if (c == '+') {
  		decoded += ' ';
	  } else {
	  	decoded += c;
	  }
	}
	return decoded;
}

//--------------------------------------------------------------
// OTPのURIをパースする
//--------------------------------------------------------------
bool parseUriOTP(TotpParams* tp, String uri) {
	String issuer = "", account = "", secret = "";
	String remainder, label, query;
	bool result = false;
	
	// URIが otpauth://totp/ で始まっているか？
	String prefix = "otpauth://totp/";
	if (!uri.startsWith(prefix)) {
	  if (debug) sp("URI not start with otpauth://totp/");
	  return false;
	}
	
	// ?の箇所でラベルとクエリに分離
	remainder = uri.substring(prefix.length());
	int qIndex = remainder.indexOf('?');
	if (qIndex != -1) {
	  label = remainder.substring(0, qIndex);
	  query = remainder.substring(qIndex + 1);
	} else {
	  label = remainder;
	}
	
	// ラベルの中に:がある場合はissuerとaccountに分割
	label = urlDecode(label);
	int colonIndex = label.indexOf(':');
	if (colonIndex != -1) {
	  issuer = label.substring(0, colonIndex);
	  String accountPart = label.substring(colonIndex + 1);
	  accountPart.trim();
	  account = accountPart;
	} else {
	  account = label;
	}
	
	// クエリパラメータ(key=value&key=value)のパース
	if (query.length() > 0) {
	  int start = 0;
	  while (start < query.length()) {
      // & で分割
      int ampIndex = query.indexOf('&', start);
      String pair;
      if (ampIndex == -1) {
        pair = query.substring(start);
        start = query.length();
      } else {
        pair = query.substring(start, ampIndex);
        start = ampIndex + 1;
      }
      // = で分割
      int equalIndex = pair.indexOf('=');
      if (equalIndex != -1) {
        String key = pair.substring(0, equalIndex);
        String value = pair.substring(equalIndex + 1);
        key = urlDecode(key);
        value = urlDecode(value);
        // パラメーターごとの処理        
        if (key.equalsIgnoreCase("secret")) {
          if (value.length() < sizeof(TotpParams::secret)-1) {
            secret = value;
            result = true;
          }
        } else if (key.equalsIgnoreCase("issuer")) {
          issuer = value;
        } else if (key.equalsIgnoreCase("digit")) {
          int num = value.toInt();
          if (num == 6 || num == 8) tp->digit = (uint8_t)num;
        } else if (key.equalsIgnoreCase("period")) {
          int num = value.toInt();
          if (num > 0 || num <= 255) tp->period = (uint8_t)num;
        }
      }
	  }
	}
  
	// 構造体に代入
	if (result) {
	  issuer.toCharArray(tp->issuer, sizeof(TotpParams::issuer));
	  account.toCharArray(tp->account, sizeof(TotpParams::account));
	  secret.toCharArray(tp->secret, sizeof(TotpParams::secret));
	  if (debug) {
      spf("URI: %s\n", uri.c_str());
      spf("  Issuer:  %s\n", tp->issuer);
      spf("  Account: %s\n", tp->account);
      spf("  Secret:  %s\n", tp->secret);
      spf("  Digit:   %d\n", tp->digit);
      spf("  Period:  %d\n", tp->period);
	  }
	} else {
    if (debug) sp("Error! URI cannot decode");
  }
	return result;
}

//--------------------------------------------------------------
// 指定時刻のワンタイムパスワードを取得する
//--------------------------------------------------------------
String getTotp(TotpParams* tp, time_t epoch) {
  size_t maxOut = strlen(tp->secret);
  char decodedSecret[maxOut];
  int decLen = base32decode(tp->secret, (unsigned char*) decodedSecret, maxOut);
  if (decLen < 0) return "";
  TOTP totp = TOTP(reinterpret_cast<uint8_t*>(decodedSecret), decLen, tp->period);
  char* code = totp.getCode(epoch);
  if (0 && debug) {
    spp("Secret (base32)",tp->secret);
    spn("Secret (decode): ");
    printDump1Line(reinterpret_cast<byte*>(decodedSecret), decLen);
    spp("epoch",epoch);
    spp("code",code);
  }
  return (String)code;
}

//--------------------------------------------------------------
// OTPの保存ファイル名を作成する
//--------------------------------------------------------------
String makeOtpFilename(TotpParams* tp) {
  uint8_t hash[32];
  String filename = "/otp-";
  // ハッシュ値をHEX表記にする
  String text = String(tp->issuer) + String(tp->account) + String(tp->secret);
  if (debug) spp("makeOtpFilename HASH source text",text);
  if (!sha256(text, hash, sizeof(hash))) return "";
  for (int i=0; i<8; i++) {
    char buff[4];
    sprintf(buff, "%02x", hash[i]);
    filename = filename + String(buff);
  }
  filename = filename + ".bin";
  return filename;
}

//--------------------------------------------------------------
// FatFSにOTPを保存する
//--------------------------------------------------------------
bool saveOtpFile(String filename, TotpParams* tp) {
  if (!status.unlock) return false;
  bool res = false;

  // "tp.secret"を暗号化する
  TotpParams tps = *tp;
  size_t len = sizeof(TotpParams::secret);
  size_t enclen = encrypt(reinterpret_cast<byte*>(tp->secret), len, status.iv, status.secret, reinterpret_cast<byte*>(tps.secret)); // 暗号化
  if (debug) {
    spn("otp-secret: ");
    printDump1Line(reinterpret_cast<byte*>(tp->secret), len);
    spn("otp-secret-enc: ");
    printDump1Line(reinterpret_cast<byte*>(tps.secret), enclen);
  }

  // ファイル保存
  res = saveFile(&tps, sizeof(tps), filename);
  if (debug) spp("saveFile", tf(res));
  return res;
}

//--------------------------------------------------------------
// FatFSからOTPを読み込む
//--------------------------------------------------------------
bool loadOtpFile(String filename, TotpParams* tp, bool decryptSecret) {
  if (decryptSecret && !status.unlock) return false;
  bool res = false;
  size_t rlen = 0;

  // ファイル読み込み
  size_t deflen = sizeof(TotpParams);
  int filelen = getFileSize(filename);
  if (filelen == deflen) {
    rlen = loadFile(tp, deflen, filename);
    if (debug) spp("loadFile size", rlen);
  }
  if (rlen != deflen) return false;

  // "tp.secret"を複合化する
  size_t seclen = sizeof(TotpParams::secret);
  if (decryptSecret) { 
    byte decSecret[seclen];
    size_t declen = decrypt(decSecret, reinterpret_cast<byte*>(tp->secret), seclen, status.iv, status.secret);  // 複合化
    res = (declen == seclen && tp->version == 1);
    if (debug) {
      spn("otp-secret-enc: ");
      printDump1Line(reinterpret_cast<byte*>(tp->secret), seclen);
      spn("otp-secret: ");
      printDump1Line(decSecret, declen);
    }
    memcpy(reinterpret_cast<char*>(tp->secret), decSecret, seclen);
  } else {
    res = (tp->version == 1);
    memset(reinterpret_cast<char*>(tp->secret), 0, seclen);
  }
  
  return res;
}

//--------------------------------------------------------------
// FatFSのOTPファイル名一覧を取得する
//--------------------------------------------------------------
std::vector<String> listOtpFiles() {
  const String dirname = "/";
  std::vector<String> fileList;
  String filename;

  // ファイル一覧を取得
  if (debug) sp("File list");
  File root = FFat.open(dirname);
  File file = root.openNextFile();
  while (file) {
    filename = file.name();
    if (!file.isDirectory()) {
      if (filename.startsWith("otp-") && filename.endsWith(".bin")) {
        if (debug) spf("  File: %s%s (%d)\n", dirname, filename.c_str(), file.size());
        fileList.push_back(dirname + filename);
      }
    }
    file = root.openNextFile();
  }
  root.close();

  return fileList;
}

//--------------------------------------------------------------
// FatFSのOTP情報を全て取得する
//--------------------------------------------------------------
int listAllOtpFiles(std::vector<TotpParamsList> *tps, bool decryptSecret) {
  std::vector<String> otpFiles = listOtpFiles();  // ファイル名一覧を取得
  bool debugOrig = debug;
  debug = false;
  for (int i=0; i<otpFiles.size(); i++) {
    TotpParams tp;
    bool res = loadOtpFile(otpFiles[i], &tp, decryptSecret);
    if (res) tps->push_back({ otpFiles[i], tp });
  }
  debug = debugOrig;
  return tps->size();
}
