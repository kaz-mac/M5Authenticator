/*
  configure.h
  設定の管理

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#pragma once
#include <M5DinMeter.h>
#include "common.h"

#include <FFat.h>

extern ConfigInfo conf;

// デバッグに便利なマクロ定義 --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)
#define spp(k,v) Serial.println(String(k)+"="+String(v))
#define spf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)

class Configure {
public:
  const char* CONFIG_FILENAME = "/config.bin";
  const uint8_t CONFIG_VERSION = 1;
  bool _debug = true;

  Configure();
  ~Configure() = default;

  bool initConfig();   // 初期設定ファイルを書き込み、デバイスにロードする　（通常FatFSのフォーマットと同時に行う）
  bool saveConfig(ConfigInfo &fconf);  // 設定ファイルを書き込む
  bool loadConfig(ConfigInfo &fconf);  // 設定ファイルを読み込む

};
