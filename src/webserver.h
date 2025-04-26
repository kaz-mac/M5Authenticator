/*
  webserver.h
  HTTPS対応Webサーバーのサブルーチン

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#pragma once

#include "common.h"
#include "utility.h"

#include <FFat.h>
#include <WiFi.h>

// Webサーバー関連
// #define USING_ESP_TLS    // まだ非対応らしい
#define HTTPS_LOGLEVEL 2    // Error, Warning
#include <HTTPS_Server_Generic.h>
using namespace httpsserver;
#define HEADER_USERNAME "X-USERNAME"
#define HEADER_GROUP    "X-GROUP"
HTTPSServer* secureServer;

// Webコンテンツ
#include "webpage.h"    // TOPページのHTML

// メインで定義した変数を使用するためのもの
//bool m5BtnAwasReleased();

// 定義
void handleRoot(HTTPRequest * req, HTTPResponse * res);
void handleFiles(HTTPRequest * req, HTTPResponse * res);
void handleDownload(HTTPRequest * req, HTTPResponse * res);
void handleUpload(HTTPRequest * req, HTTPResponse * res);
void handleDelete(HTTPRequest * req, HTTPResponse * res);
void handle404(HTTPRequest * req, HTTPResponse * res);
void middlewareAuthentication(HTTPRequest * req, HTTPResponse * res, std::function<void()> next);
void middlewareAuthorization(HTTPRequest * req, HTTPResponse * res, std::function<void()> next);

String _fileRootDir = "/";
const String _protectUris[] = { "/" };  // 認証が必要なURI


// --------------------------------------------------------------------------------------
// オレオレ証明書を作成してFatFSに保存する
// --------------------------------------------------------------------------------------
bool httpsGenerateCertificate() {
  SSLCert* cert = new SSLCert();
  bool res = false;
  // 証明書を作成
  if (debug) spn("Generating self-signed certificate...");
  int createCertResult = createSelfSignedCert(
    *cert,
    KEYSIZE_1024,
    "CN=esp32.local,O=OreoreCompany,C=JP",
    "20240101000000",
    "20350101000000"
  );
  if (createCertResult != 0) {
    if (debug) sp("Error! cannot generate certificate.");
    return false;
  }
  if (debug) sp("done");

  // FatFSに保存
  if (cert->getPKLength() > 0 && cert->getCertLength() > 0) {
    res = saveFile(cert->getPKData(), cert->getPKLength(), FN_SSL_KEY);
    if (!res) return false;
    res = saveFile(cert->getCertData(), cert->getCertLength(), FN_SSL_CERT);
  }

  delete cert;
  return true;
}

// --------------------------------------------------------------------------------------
// FatFSからSSL秘密鍵と証明書をロードしてHTTPSサーバーを初期化する
// --------------------------------------------------------------------------------------
bool loadInitServer() {
  int sizePk = getFileSize(FN_SSL_KEY);
  int sizeCt = getFileSize(FN_SSL_CERT);
  if (sizePk > 0 && sizeCt > 0) {
    byte* dataPk = new byte[sizePk];
    byte* dataCt = new byte[sizeCt];
    int rlenPk = loadFile(dataPk, sizePk, FN_SSL_KEY);
    int rlenCt = loadFile(dataCt, sizeCt, FN_SSL_CERT);
    if (rlenPk == sizePk && rlenCt == sizeCt) {
      SSLCert* cert = new SSLCert(dataCt, sizeCt, dataPk, sizePk);
      secureServer = new HTTPSServer(cert);
      return true;
    }
  }
  return false;
}

// --------------------------------------------------------------------------------------
// Webサーバーを起動し、ループ処理を継続し、ボタンを押したらWebサーバー終了する
// --------------------------------------------------------------------------------------
bool httpsStartWebserver() {
  bool res;

  // サーバー設定
  ResourceNode nodeRoot("/", "GET", &handleRoot);
  ResourceNode nodeFiles("/files.json", "GET", &handleFiles);
  ResourceNode nodeDownload("/download", "GET", &handleDownload);
  ResourceNode nodeUpload("/upload", "POST", &handleUpload);
  ResourceNode nodeDelete("/delete", "GET", &handleDelete);
  ResourceNode node404("", "GET", &handle404);
  secureServer->registerNode(&nodeRoot);
  secureServer->registerNode(&nodeFiles);
  secureServer->registerNode(&nodeDownload);
  secureServer->registerNode(&nodeUpload);
  secureServer->registerNode(&nodeDelete);
  secureServer->setDefaultNode(&node404);
  secureServer->addMiddleware(&middlewareAuthentication);
  secureServer->addMiddleware(&middlewareAuthorization);

  // サーバー起動
  if (debug) sp("Web Server start");
  secureServer->start();
  res = secureServer->isRunning();
  if (debug) spp("Web Server running",tf(res));
  if (!res) return false;

  // サーバー起動中のループ（ボタンを押したら終了）
  while (1) {
    secureServer->loop();
    M5.update();
    if (m5BtnAwasReleased()) break;
    delay(5);
  }

  // サーバー終了
  if (debug) sp("Web Server stop");
  secureServer->stop();

  return true;
}

// --------------------------------------------------------------------------------------
// ファイル名が安全かチェックする
// --------------------------------------------------------------------------------------
bool checkSafeFilename(String filename) {
  const String disallowed = "\\/:*?\"<>|'"; // 禁止する記号
  int len = filename.length();
  if (len < 1 || len > 32) return false;
  for (int i=0; i<len; i++) {
    char c = filename.charAt(i);
    if (c < 32 || c > 126) return false;
    if (disallowed.indexOf(c) != -1) return false;
  }
  return true;
}

// --------------------------------------------------------------------------------------
// URLエンコード
// --------------------------------------------------------------------------------------
String urlEncode(String inputText) {
  String encoded = "";
  for (int i=0; i<inputText.length(); i++) {
    char c = inputText.charAt(i);
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      char buf[4];
      sprintf(buf, "%%%02X", c);
      encoded += buf;
    }
  }
  return encoded;
}

// --------------------------------------------------------------------------------------
//  BASIC認証　パスワード比較
// --------------------------------------------------------------------------------------
void middlewareAuthentication(HTTPRequest * req, HTTPResponse * res, std::function<void()> next) {
  req->setHeader(HEADER_USERNAME, "");
  req->setHeader(HEADER_GROUP, "");
  String reqUsername = String(req->getBasicAuthUser().c_str());
  String reqPassword = String(req->getBasicAuthPassword().c_str());

  // パスワード比較
  if (reqUsername.length() > 0 && reqPassword.length() > 0) {
    bool authValid = false;
    String group = "";
    if (reqUsername == webAuthUser && reqPassword == webAuthPasswd) {
      authValid = true;
      group = "USER";
    }
    if (authValid) {
      // 認証成功
      req->setHeader(HEADER_USERNAME, std::string(reqUsername.c_str()));
      req->setHeader(HEADER_GROUP, std::string(group.c_str()));
      next();
    } else {
      // 認証失敗
      res->setStatusCode(401);
      res->setStatusText("Unauthorized");
      res->setHeader("Content-Type", "text/plain");
      res->setHeader("WWW-Authenticate", "Basic realm=\"ESP32 privileged area\"");
      res->println("401. Unauthorized");
    }
  } else {
    next();
  }
}

// --------------------------------------------------------------------------------------
//  BASIC認証　認証が必要なURIの判定
// --------------------------------------------------------------------------------------
void middlewareAuthorization(HTTPRequest * req, HTTPResponse * res, std::function<void()> next) {
  bool reqAuth = false;
  // 認証が必要かどうかチェック
  String username = String(req->getHeader(HEADER_USERNAME).c_str());
  String uri = String(req->getRequestString().c_str());
  int protectUrisLen = sizeof(_protectUris) / sizeof(_protectUris[0]);
  for (int i=0; i<protectUrisLen; i++) {
    if (username == "" && uri.startsWith(_protectUris[i])) {
      reqAuth = true;
      break;
    }
  }
  // 認証を促す
  if (reqAuth) {
    res->setStatusCode(401);
    res->setStatusText("Unauthorized");
    res->setHeader("Content-Type", "text/plain");
    res->setHeader("WWW-Authenticate", "Basic realm=\"ESP32 privileged area\"");
    res->println("401. Unauthorized");
  } else {
    next();
  }
}

// --------------------------------------------------------------------------------------
// 【コンテンツ】TOPページ
// --------------------------------------------------------------------------------------
void handleRoot(HTTPRequest * req, HTTPResponse * res) {
  res->setHeader("Content-Type", "text/html");
  res->println(HTMLPAGE_TOP);
}

// --------------------------------------------------------------------------------------
// 【コンテンツ】404ページ
// --------------------------------------------------------------------------------------
void handle404(HTTPRequest * req, HTTPResponse * res) {
  req->discardRequestBody();  // リクエストボディを消去
  res->setStatusCode(404);
  res->setStatusText("Not Found");
  res->setHeader("Content-Type", "text/html");
  res->println("<!DOCTYPE html>");
  res->println("<html>");
  res->println("<head><title>Not Found</title></head>");
  res->println("<body><h1>404 Not Found</h1><p>The requested resource was not found on this server.</p></body>");
  res->println("</html>");
}

// --------------------------------------------------------------------------------------
// 【コンテンツ】ファイル一覧を返すJSON
// --------------------------------------------------------------------------------------
void handleFiles(HTTPRequest * req, HTTPResponse * res) {
  // ファイル一覧を取得しJSONを作成
  String json = "{\"files\":[";
  File root = FFat.open(_fileRootDir);
  if (!root || !root.isDirectory()) return;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      json = json + "{\"name\":\"" + _fileRootDir + urlEncode(file.name()) + "\",\"size\":" + String(file.size()) + "},";
    }
    file = root.openNextFile();
  }
  root.close();
  if (json.endsWith(",")) json.remove(json.length() - 1, 1);
  json = json + "]}";
  if (debug) spp("json",json);

  // JSONを送信
  res->setHeader("Content-Type", "application/json");
  res->setHeader("Content-Length", std::to_string(json.length()));
  res->print(json);
}

// --------------------------------------------------------------------------------------
// 【コンテンツ】ダウンロード
// --------------------------------------------------------------------------------------
void handleDownload(HTTPRequest * req, HTTPResponse * res) {
  std::string filename;
  int filesize = -1;
  String path;

  // ファイル確認
  ResourceParameters * params = req->getParams();
  if (params->getQueryParameter("filename", filename)) {
    if (!checkSafeFilename(filename.c_str())) {  // 安全性チェック
      path = String(filename.c_str());  //filenameはパスを含んだ文字列が入ってる
      filesize = getFileSize(path);
    }
  }

  // ダウンロード
  if (filename.front() == '/') filename.erase(0, 1);
  if (debug) spf("File Download: %s (%d)\n", path.c_str(), filesize);
  if (filesize > 0) {
    byte buff[256];
    int remain = filesize;
    File file = FFat.open(path, FILE_READ);
    if (file) {
      res->setHeader("Content-Type", "application/octet-stream");
      res->setHeader("Content-Disposition", (std::string("attachment; filename=\"") + filename.c_str() + "\"").c_str());
      res->setHeader("Content-Length", std::to_string(filesize));
      while (remain > 0) {
        size_t rlen = file.read(reinterpret_cast<uint8_t *>(buff), sizeof(buff));
        res->write(buff, rlen);
        remain -= rlen;
      }
    } else {
      if (debug) sp("loadFile open failed "+path);
    }
    file.close();
  } else {
    handle404(req, res);
  }
}

// --------------------------------------------------------------------------------------
// 【コンテンツ】アップロード
// --------------------------------------------------------------------------------------
void handleUpload(HTTPRequest * req, HTTPResponse * res) {
  HTTPBodyParser *parser;

  // データの種別チェック
  std::string contentType = req->getHeader("Content-Type");
  size_t semicolonPos = contentType.find(";");
  if (semicolonPos != std::string::npos) {
    contentType = contentType.substr(0, semicolonPos);
  }
  if (contentType == "multipart/form-data") {
    parser = new HTTPMultipartBodyParser(req);
  } else {
    handle404(req, res);
    return;
  }

  // アップロードされたファイルの取り出し
  while (parser->nextField()) {
    String name = String(parser->getFieldName().c_str());
    String filename = String(parser->getFieldFilename().c_str());
    String mimeType = String(parser->getFieldMimeType().c_str());
    if (debug) spf("Upload: name=%s, filename=%s, mimetype=%s\n", name.c_str(), filename.c_str(), mimeType.c_str());
    if (name != "file") continue;
    if (!checkSafeFilename(filename.c_str())) continue;  // 安全性チェック

    // ファイルのオープン
    String path = "/" + filename;
    File file = FFat.open(path, FILE_WRITE);
    if (!file) {
      if (debug) sp("saveFile open failed "+path);
      continue;
    }

    // 取り出したデータをファイルに書き込む
    int filesize = 0;
    while (!parser->endOfField()) {
      byte buff[256];
      size_t rlen = parser->read(buff, 256);
      file.write(buff, rlen);
      filesize += filesize;
    }
    file.close();
    if (debug) sp("file saved. filename="+path+" size="+String(filesize));
  }

  // アップロードが完了したらリダイレクトする
  res->setStatusCode(302);
  res->setHeader("Location", "/");

  delete parser;
}

// --------------------------------------------------------------------------------------
// 【コンテンツ】削除
// --------------------------------------------------------------------------------------
void handleDelete(HTTPRequest * req, HTTPResponse * res) {
  std::string filename;
  int filesize = -1;
  String path;

  // ファイル確認
  ResourceParameters * params = req->getParams();
  if (params->getQueryParameter("filename", filename)) {
    if (!checkSafeFilename(filename.c_str())) {  // 安全性チェック
      path = String(filename.c_str());  //filenameはパスを含んだ文字列が入ってる
      filesize = getFileSize(path);
    }
  }

  // 削除
  if (debug) spf("File Delete: %s (%d)\n", path.c_str(), filesize);
  if (filesize > -1) {
    deleteFile(path);
  } else {
    handle404(req, res);
  }

  // 削除したらリダイレクトする
  res->setStatusCode(302);
  res->setHeader("Location", "/");
}
