/*
  DinMeterUI.h
  M5 DinMeterのユーザーインターフェース　描画とコントロール

  Copyright (c) 2024 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#pragma once
#include <M5DinMeter.h>
#include "common.h"

struct ItemDef {  // メニューアイテムの構造体
  uint8_t type;
  uint8_t icon;
  String name;
  bool (*function)();
  String description;
};
struct MenuDef {  // メニューの構造体
  String title;
  int select;
  int selected;
  int idx;
  int cur;
  std::vector<ItemDef> lists;
};

struct XYaddress { int x, y; };
struct WHaddress { int w, h; };

// struct TextBoxOption {  // テキストボックス描画オプション
//   bool fillBg = true;
//   uint16_t bgColor = TFT_BLACK;
//   XYaddress pxy = { 0, 0 };    // 子canvasを出力する親canvasの座標
//   WHaddress wh = { 128, 16 };
//   uint8_t textDatum = TL_DATUM;
//   const lgfx::IFont *font;
//   uint16_t textColor = TFT_WHITE;
//   XYaddress fxy = { 2, 1 };   // 文字の座標
//   bool textScroll = false;
// };

// デバッグに便利なマクロ定義 --------
#define sp(x) Serial.println(x)
#define spn(x) Serial.print(x)
#define spp(k,v) Serial.println(String(k)+"="+String(v))
#define spf(fmt, ...) Serial.printf(fmt, __VA_ARGS__)

class DinMeterUI {
public:
  struct ImageInfo {  // 画像データの構造体
    const unsigned short* data;
    const uint16_t width;
    const uint16_t height;
    const uint16_t size;
    const uint16_t posX;
    const uint16_t posY;
    const unsigned short transparent;
  };

  LovyanGFX* _dst = nullptr;    // 出力先のキャンバスまたはディスプレイ
  bool _dstLock = false;        // 出力先のキャンバスのロック状態
  uint16_t _bgColor = 0x0001;   // 出力時の透明色（使ってない）
  int _lastEncPos = 0;          // ロータリーエンコーダーの最終位置
  bool _debug = true;           // シリアルデバッグ出力

  // 各パネルの基準座標
  const WHaddress m5wh = { 240, 135 };    // M5 DinMeter
  const WHaddress swh  = { 28, m5wh.h };  // Statsパネル幅28
  const XYaddress sxy  = { 0, 0 };
  const XYaddress sxye = { sxy.x+swh.w-1, sxy.y+swh.h-1 };
  const WHaddress rwh  = { 28, m5wh.h };  // Rotaryパネル幅28
  const XYaddress rxy  = { m5wh.w-rwh.w, 0 };
  const XYaddress rxye = { rxy.x+rwh.w-1, rxy.y+rwh.h-1 };
  const WHaddress mwh  = { m5wh.w-swh.w-rwh.w, m5wh.h };  // メインパネル
  const XYaddress mxy  = { swh.w, 0 };
  const XYaddress mxye = { mxy.x+mwh.w-1, mxy.y+mwh.h-1 };

  // デフォルト色
  const uint16_t PCOL_ROTARY = TFT_DARKGREEN;
  const uint16_t PCOL_MAIN = TFT_BLACK;
  const uint16_t PCOL_STATUS = TFT_NAVY;
  const uint16_t PCOL_TITLE = rgb565(0xd7d775);

  // デフォルトフォント
  // const lgfx::U8g2font* TitleFont = &fonts::lgfxJapanGothic_16;  //&fonts::FreeMonoBold9pt7b;
  // const lgfx::U8g2font* TextFont = &fonts::lgfxJapanGothicP_12; //&fonts::FreeMono9pt7b;
  // const lgfx::U8g2font* BoldFont = &fonts::lgfxJapanGothic_12;  //&fonts::FreeMonoBold9pt7b;
  const lgfx::U8g2font* TitleFont = &fonts::efontJA_12;//efontJA_16_b;  //&fonts::FreeMonoBold9pt7b;
  const lgfx::U8g2font* TextFont = &fonts::efontJA_12;  //&fonts::FreeMono9pt7b;
  const lgfx::U8g2font* BoldFont = &fonts::efontJA_12;  //&fonts::FreeMonoBold9pt7b;

  // メンバ関数
  DinMeterUI();
  ~DinMeterUI() = default;
  void setDrawDisplay(LovyanGFX* dst, uint16_t bgColor);  // UIの出力先を設定する
  void lockCanvas(uint32_t timeout=1000);   // 上位Canvasの出力可能な状態になるまで待つ
  void unlockCanvas();   // 上位Canvasの出力ロックを解除する
  bool m5BtnAwasReleased();  // ボタン押下判定

  // コールバック
  typedef void (*CallbackFunc)();
  CallbackFunc _callbackEnc = nullptr;
  CallbackFunc _callbackBtn = nullptr;
  void setEncoderCallback(CallbackFunc func) { _callbackEnc = func; }  // ロータリーエンコーダーのコールバック
  void setButtonFunction(CallbackFunc func) { _callbackBtn = func; }  // ボタン押下のコールバック

  // 描画系
  void drawStatusPanel(StatusInfo* st);   // 左パネル（ステータス表示）を描画する
  void drawRotaryPanel(MenuDef* menu);    // 右パネル（ロータリー表示）を描画する
  void drawMainPanel_preview(MenuDef* menu);    // メインパネルを描画する　選択前の情報表示用
  void drawMainPanel_vselect(MenuDef* menu, int orig, int boxnum, String description="", int desch=0);    // メインパネルを描画する　縦スクロールの項目選択
  void drawMainPanel_dialog(MenuDef* menu, int orig, String description="", int desch=0);    // メインパネルを描画する　ダイアログ

  // ユーティリティ
  uint16_t rgb565(uint32_t rgb);    // 32bit RGBから16bit RGB565に変換する
  void setStringArea(int x, int y, int w, int h, bool scr=false);   // 描画エリアを限定する
  void clearStringArea();   // 描画エリアの限定を解除する
  void setConsoleArea(int x, int y, int w, int h);  // ミニコンソール領域を作成する
  void clearConsoleArea();  // ミニコンソール領域を解除する
  void drawTextBox(M5Canvas* _parent, int x, int y, int w, int h, uint16_t bgColor, int fx, int fy, uint8_t textDatum, const lgfx::IFont *font, uint16_t textColor, String text, bool scr=false);   // テキストボックスを描画する

  // 操作系
  int encoderChanged(MenuDef* menu, bool looped, bool reverse=false);   // エンコーダーを回したらメニューの表示位置(.select)を変更する
  void encoderCacheClear();    // エンコーダーのゴミデータをクリアする（encoderChanged()以外を使った場合）
  int selectMenuList(MenuDef* menu, int orig, int boxnum, String description="", int desch=0);   // リスト形式のメニューを選択する
  int selectDialog(const std::vector<String>& texts, int orig, String title, String description="", int desch=0, bool skipPress=false);   // ダイアログ形式のメニューを選択する
  int selectNotice(String btntext, String title, String description, int desch, bool skipPress=false);    // 1ボタンだけのダイアログボックスを表示する
  void imageNotice(uint8_t no, String title, bool skipPress);   // 画像のダイアログボックスを表示する
};
