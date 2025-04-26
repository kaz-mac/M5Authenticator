/*
  configure.cpp
  設定の管理

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#include "Configure.h"
#include "common.h"

Configure::Configure() {
  //FFat.begin();
}

// 初期設定ファイルを書き込み、デバイスにロードする　（通常FatFSのフォーマットと同時に行う）
bool Configure::initConfig() {
  bool success = false;
  ConfigInfo iniconf = {
    .loaded = false,
    .version = 0,
    .saveSecret = CONF_SECRET_NONE,
    .autoEnter = true,
    .autoSleep = 0,
    .quiet = false,
    .keyJis = true,
    .develop = false,
    .autoKeyOff = 10,
  };
  if (_debug) sp("config initialize");
  if (saveConfig(iniconf)) {
    success = loadConfig(conf);   //デバイスにロードする
  }
  return success;
}

// 設定ファイルを書き込む
bool Configure::saveConfig(ConfigInfo &conf) {
  bool success = false;
  ConfigInfo fconf = conf;

  // バイナリ形式で構造体を保存
  File file = FFat.open(CONFIG_FILENAME, FILE_WRITE);
  if (file) {
    file.write(CONFIG_VERSION);   // 先頭1バイトはバージョン
    fconf.loaded = false;
    fconf.version = CONFIG_VERSION;
    size_t wlen = file.write((const uint8_t*)&fconf, sizeof(ConfigInfo));
    file.close();
    if (wlen == sizeof(ConfigInfo)) success = true;
  }
  if (_debug) sp((String)"Save configfile "+(success ? "done" : "failed" ));
  return success;
}

// 設定ファイルを読み込む
bool Configure::loadConfig(ConfigInfo &fconf) {
  bool success = false;
  uint8_t buffer[128];

  // 設定ファイルを開く
  File file = FFat.open(CONFIG_FILENAME, FILE_READ);
  if (file) {
    uint8_t version = file.read();
    size_t fileSize = file.size();
    // spp("fileSize",fileSize);
    // spp("size",sizeof(ConfigInfo));
    if (version == 1) {
      if (fileSize-1 == sizeof(ConfigInfo)) {
        size_t rlen = file.read((uint8_t*)&fconf, sizeof(ConfigInfo));
        if (rlen == sizeof(ConfigInfo)) {
          fconf.loaded = true;
          success = true;
        }
      }
    }
    file.close();
  }
  if (_debug) sp((String)"Load configfile "+(success ? "done" : "failed" ));
  return success;
}

