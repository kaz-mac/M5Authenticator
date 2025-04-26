/*
  DinMeterUI.cpp
  M5 DinMeterのユーザーインターフェース　描画とコントロール

  Copyright (c) 2025 Kaz  (https://akibabara.com/blog/)
  Released under the MIT license.
  see https://opensource.org/licenses/MIT
*/
#include "DinMeterUI.h"
#include "icon.h"

// コンストラクタ
DinMeterUI::DinMeterUI() {
    //
}

// UIの出力先を設定する
void DinMeterUI::setDrawDisplay(LovyanGFX* display, unsigned short bgColor) {
  _dst = display;
  _bgColor = bgColor;
}

// 上位Canvasの出力可能な状態になるまで待つ
void DinMeterUI::lockCanvas(uint32_t timeout) {
  if (_dstLock) {
    uint32_t tm = millis();
    while (tm > millis()-timeout) {
      if (!_dstLock) break;
      delay(5);
    }
  }
  _dstLock = true;
}

// 上位Canvasの出力ロックを解除する
void DinMeterUI::unlockCanvas() {
  _dstLock = false;
}

// ボタン押下判定
bool DinMeterUI::m5BtnAwasReleased() {
  bool res = M5.BtnA.wasReleased();
  if (res && _callbackBtn != nullptr) {
    _callbackBtn();   // コールバック関数を実行
  } 
  return res;
}

// 左パネル（ステータス表示）を描画する
void DinMeterUI::drawStatusPanel(StatusInfo* st) {
  uint8_t iconList[5];
  int x,y;
  // canvasの作成
  M5Canvas canvas(_dst);
  canvas.setColorDepth(16);
  canvas.createSprite(swh.w, swh.h);
  canvas.fillRect(0,0, swh.w,swh.h, PCOL_STATUS);
  // 表示するアイコンの選択
  iconList[0] = (st->unlock) ? ICON_unlock_on : ICON_lock_off;
  iconList[1] = (st->ble) ? ICON_ble_on : ICON_ble_off;
  iconList[2] = (st->unitQRready) ? ICON_qr_on : ICON_qr_off;
  iconList[3] = (st->unitRFIDready) ? ICON_rfid_on : ICON_rfid_off;
  iconList[4] = ICON_battery;
  // アイコンを表示
  for (int i=0; i<5; i++) {
    x = 2;
    y = sxy.y + 3 + i*(24+2);
    if (iconList[i] != ICON_none) {
      int no = iconList[i];
      canvas.pushImage(x,y, imgInfo[no].width, imgInfo[no].height, imgInfo[no].data);
    } else {
      canvas.drawRect(x,y, 24,24, TFT_BLUE);      
    }
  }
  // バッテリー残量を描画
  x = sxy.x + 11;
  y = sxy.y + 3 + 4*(24+2) + 7;
  if (st->battery > 70) canvas.fillRect(x,y, 6,3, TFT_GREEN);
  y += 4;
  if (st->battery > 30) canvas.fillRect(x,y, 6,3, TFT_GREEN);
  y += 4;
  auto color = (st->battery < 30) ? TFT_RED : TFT_GREEN;
  canvas.fillRect(x,y, 6,4, color);
  // canvasの出力
  lockCanvas();
  _dst->startWrite(); 
  canvas.pushSprite(sxy.x, sxy.y);
  _dst->endWrite();
  unlockCanvas();
}

// 右パネル（ロータリー表示）を描画する
void DinMeterUI::drawRotaryPanel(MenuDef* menu) {
  int num, idx, x, y, no;
  num = menu->lists.size();
  // canvasの作成
  M5Canvas canvas(_dst);
  canvas.setColorDepth(16);
  canvas.createSprite(rwh.w, rwh.h);
  canvas.fillSprite(PCOL_ROTARY);
  // アイコンを表示
  for (int i=0; i<num; i++) {
    x = 2;
    y = rxy.y + 3 + i*(24+2);
    if (i == menu->select) {
      canvas.drawRect(x-1,y-1, 26,26, TFT_YELLOW);
    }
    if (menu->lists[i].icon != ICON_none) {
      no = menu->lists[i].icon;
      canvas.pushImage(x,y, imgInfo[no].width, imgInfo[no].height, imgInfo[no].data);
    }
  }
  // canvasの出力
  lockCanvas();
  _dst->startWrite(); 
  canvas.pushSprite(rxy.x, rxy.y);
  _dst->endWrite();
  unlockCanvas();
}

// メインパネルを描画する　選択前の情報表示用
void DinMeterUI::drawMainPanel_preview(MenuDef* menu) {
  int x, y, w, h, y2, no;
  no = menu->select;
  if (no < 0 || no >= (int)menu->lists.size()) return;
  // canvasの作成
  M5Canvas canvas(_dst);
  canvas.setColorDepth(16);
  canvas.createSprite(mwh.w, mwh.h);
  canvas.fillSprite(PCOL_MAIN);
  // タイトル
  h = 22;
  drawTextBox(&canvas, 2,0, mwh.w-4,h, PCOL_TITLE, 
    (mwh.w-8)/2,2, TC_DATUM, TitleFont, TFT_BLACK, menu->lists[no].name);
  y2 = h + 1;
  // 説明
  drawTextBox(&canvas, 5,y2, mwh.w-10,mwh.h-y2-24, TFT_BLACK, 
    2,1, TL_DATUM, TextFont, TFT_WHITE, menu->lists[no].description);
  // Enter
  h = 24;
  drawTextBox(&canvas, 40,mwh.h-h-12, 80,h, rgb565(0x888888), 
    40,2, TC_DATUM, BoldFont, TFT_BLACK, "決定");
  // canvasの出力
  lockCanvas();
  _dst->startWrite(); 
  canvas.pushSprite(mxy.x, mxy.y);
  _dst->endWrite();
  unlockCanvas();
}

// メインパネルを描画する　縦スクロールの項目選択
void DinMeterUI::drawMainPanel_vselect(MenuDef* menu, int orig, int boxnum, String description, int desch) {
  int x, y, w, h, y2;
  if (menu->select < 0 || menu->select >= (int)menu->lists.size()) menu->select = 0;
  const uint16_t infoSize = 15;
  // canvasの作成
  M5Canvas canvas(_dst);
  canvas.setColorDepth(16);
  canvas.createSprite(mwh.w, mwh.h);
  canvas.fillSprite(PCOL_MAIN);
  // タイトル
  if (menu->title.length() > 0) {
    h = 22;
    drawTextBox(&canvas, 2,0, mwh.w-4,h, PCOL_TITLE, 
      (mwh.w-8)/2,2, TC_DATUM, TitleFont, TFT_BLACK, menu->title);
    y2 = h + 1;
  }
  // 説明
  if (desch > 0) {
    drawTextBox(&canvas, 2,y2, mwh.w-4,desch, TFT_BLACK, 
      2,1, TL_DATUM, TextFont, TFT_WHITE, description);
    y2 += desch;
  }
  // 選択項目
  h = 20;
  for (int i=0; i<boxnum; i++) {
    if (menu->idx+i >= menu->lists.size()) break;
    x = 10;
    y = y2 + (h+3) * i;
    w = mwh.w - x*2;
    if (i == menu->cur) { // カーソル位置
      canvas.drawRect(x-1,y-1, w+2,20+2, TFT_YELLOW);
    }
    uint16_t bgColor = (menu->idx+i == orig) ? rgb565(0x444400) : rgb565(0x222222);
    if (menu->lists[menu->idx+i].type == Itype::subtitle) bgColor = rgb565(0x006666);
    drawTextBox(&canvas, x,y, w,h, bgColor, 
      4,2, TL_DATUM, BoldFont, TFT_WHITE, menu->lists[menu->idx+i].name);
  }
  // canvasの出力
  lockCanvas();
  _dst->startWrite(); 
  canvas.pushSprite(mxy.x, mxy.y);
  _dst->endWrite();
  unlockCanvas();
}

// メインパネルを描画する　ダイアログ
void DinMeterUI::drawMainPanel_dialog(MenuDef* menu, int orig, String description, int desch) {
  int x, y, w, h, y2;
  if (menu->select < 0 || menu->select >= (int)menu->lists.size()) menu->select = 0;
  const uint16_t infoSize = 15;
  // canvasの作成
  M5Canvas canvas(_dst);
  canvas.setColorDepth(16);
  canvas.createSprite(mwh.w, mwh.h);
  canvas.fillSprite(PCOL_MAIN);
  // 文字列の横幅を推定する
  const int spcb = 10;
  const int spcp = 5;
  int tw = (menu->lists.size() - 1) * spcb;
  for (int i=0; i<menu->lists.size(); i++) {
    tw += canvas.textWidth(menu->lists[i].name, BoldFont) + spcp * 2;
  }
  // タイトル
  if (menu->title.length() > 0) {
    h = 22;
    drawTextBox(&canvas, 2,0, mwh.w-4,h, PCOL_TITLE, 
      (mwh.w-8)/2,2, TC_DATUM, TitleFont, TFT_BLACK, menu->title);
    y2 = h + 5;
  }
  // 説明
  if (desch > 0) {
    drawTextBox(&canvas, 2,y2, mwh.w-4,desch, TFT_BLACK, 
      2,1, TL_DATUM, TextFont, TFT_WHITE, description);
    y2 += desch + 10;
  }
  // 選択項目
  h = 20;
  x = (mwh.w - tw) / 2;
  y = mwh.h - h - 10;
  if (x < 0) x = 0;
  canvas.setFont(BoldFont);
  for (int i=0; i<menu->lists.size(); i++) {
    if (menu->lists[i].name.length() == 0) continue;
    w = canvas.textWidth(menu->lists[i].name) + spcp * 2;
    if (i == menu->cur) { // カーソル位置
      canvas.drawRect(x-1,y-1, w+2,h+2, TFT_YELLOW);
    }
    uint16_t bgColor = (i == orig) ? rgb565(0x444400) : rgb565(0x222222);
    drawTextBox(&canvas, x,y, w,h, bgColor, 
      spcp,2, TL_DATUM, BoldFont, TFT_WHITE, menu->lists[i].name);
    x += w + spcb;
  }
  // canvasの出力
  lockCanvas();
  _dst->startWrite(); 
  canvas.pushSprite(mxy.x, mxy.y);
  _dst->endWrite();
  unlockCanvas();
}

// 32bit RGBから16bit RGB565に変換する
uint16_t DinMeterUI::rgb565(uint32_t rgb) {
  uint32_t r = (rgb >> 16) & 0xFF;
  uint32_t g = (rgb >>  8) & 0xFF;
  uint32_t b = (rgb      ) & 0xFF;
  uint16_t ret  = (r & 0xF8) << 8;  // 5 bits
           ret |= (g & 0xFC) << 3;  // 6 bits
           ret |= (b & 0xF8) >> 3;  // 5 bits
  return ret;
}

// 描画エリアを限定する
void DinMeterUI::setStringArea(int x, int y, int w, int h, bool scr) {
  _dst->setClipRect(x, y, w, h);  //描画範囲
  _dst->setScrollRect(x, y, w, h);  //スクロール範囲
  _dst->setTextScroll(scr);
}

// 描画エリアの限定を解除する
void DinMeterUI::clearStringArea() {
  _dst->clearClipRect();
  _dst->clearScrollRect();
  _dst->setTextScroll(false);
}

// ミニコンソール領域を作成する
void DinMeterUI::setConsoleArea(int x, int y, int w, int h) {
  _dst->drawRect(x,y, w,h, rgb565(0x444444));
  _dst->fillRect(x+1,y+1, w-2,h-2, TFT_BLACK);
  //setStringArea(x+3,y+2, w-6,h-3, true);
  _dst->setScrollRect(x+3,y+2, w-6,h-3);  //スクロール範囲
  _dst->setTextScroll(true);
  _dst->setTextColor(TFT_WHITE);
  _dst->setTextDatum(TL_DATUM);
  _dst->setCursor(x+2, y+1);
}

// ミニコンソール領域を解除する
void DinMeterUI::clearConsoleArea() {
  _dst->clearScrollRect();
  _dst->setTextScroll(false);
}

// テキストボックスを描画する
void DinMeterUI::drawTextBox(M5Canvas* _parent, int x, int y, int w, int h, uint16_t bgColor, 
int fx, int fy, uint8_t textDatum, const lgfx::IFont *font, uint16_t textColor, String text, bool scr) {
  // canvasの作成
  M5Canvas box(_parent);
  box.setColorDepth(16);
  box.createSprite(w, h);
  box.fillSprite(bgColor);
  // ボックスとテキストを描画する
  box.setTextColor(textColor);
  if (textDatum == TL_DATUM) {
    box.setScrollRect(fx, fy, w-fx*2, h-fy*2);  //テキストスクロール範囲
    if (scr) box.setTextScroll(scr);
    box.setFont(font);
    box.setCursor(fx, fy);
    box.print(text);
    if (scr) box.setTextScroll(false);
    box.clearScrollRect();
  } else {
    box.setTextDatum(textDatum);
    box.drawString(text, fx,fy, font);
    box.setTextDatum(TL_DATUM);
  }
  // canvasの出力
  box.pushSprite(x, y);
}

// エンコーダーを回したらメニューの表示位置(.select)を変更する
int DinMeterUI::encoderChanged(MenuDef* menu, bool looped, bool reverse) {
  int dir = 0;
  int encpos = DinMeter.Encoder.read();
  if (encpos != _lastEncPos && encpos % 2 == 0) { // エンコーダーが変化した場合
    //dir = (_lastEncPos - encpos) / 2;
    dir = -1 * (_lastEncPos < encpos) ? 1 : -1;
    if (reverse) dir = -dir;
    menu->select += dir;
    if (looped) {
      if (menu->select >= (int)menu->lists.size()) menu->select = 0;
      else if (menu->select < 0) menu->select = (int)menu->lists.size() - 1;
    } else {
      if (menu->select >= (int)menu->lists.size()) {
        menu->select = (int)menu->lists.size() - 1;
        dir = 0;
      } else if (menu->select < 0) {
        menu->select = 0;
        dir = 0;
      }
    }
    if (_debug) spf("encpos=%d select=%d\n",encpos,menu->select);
    if (_callbackEnc != nullptr) {
      _callbackEnc();   // コールバック関数を実行
    } 
    _lastEncPos = encpos;
  }
  return dir;
}

// エンコーダーのゴミデータをクリアする（encoderChanged()以外を使った場合）
void DinMeterUI::encoderCacheClear() {
  _lastEncPos = DinMeter.Encoder.read();
}

// リスト形式のメニューを選択する
int DinMeterUI::selectMenuList(MenuDef* menu, int orig, int boxnum, String description, int desch) {
  int dir;

  // 初期表示
  // menu->select = 0;
  // menu->selected = -1;
  // int idx = 0;
  // int cur = 0;
  int curmax = boxnum - 1;
  int listmax = (int)menu->lists.size() - 1;
  drawMainPanel_vselect(menu, orig, boxnum, description, desch);   // UI中央描画

  // リスト形式のメニューの表示と選択
  while (menu->selected == -1) {
    // エンコーダー
    dir = encoderChanged(menu, false);  // エンコーダーの変化あり?
    if (dir != 0) {
      if (dir == 1) {
        if (menu->cur < curmax) menu->cur++;
        else if (menu->idx+curmax < listmax) menu->idx++;
      } else if (dir == -1) {
        if (menu->cur > 0) menu->cur--;
        else if (menu->idx > 0) menu->idx--;
      }
      drawMainPanel_vselect(menu, orig, boxnum, description, desch);   // UI中央描画
    }
    // ボタン押下
    M5.update();
    if (m5BtnAwasReleased()) {
      menu->selected = menu->select;
      if (_debug) spf("Menu %d selected.\n", menu->selected);
      break;
    }
    delay(5);
  }
  return menu->selected;
}

// ダイアログ形式のメニューを選択する
int DinMeterUI::selectDialog(const std::vector<String>& texts, int orig, String title, String description, int desch, bool skipPress) {
  int dir;

  // メニュー変数の作成
  MenuDef menu = {
    .title = title,
    .select = 0,
    .selected = -1,
    .idx = 0,
    .cur = 0,
  };
  for (int i=0; i<texts.size(); i++) {
    menu.lists.push_back({ (uint8_t)i, 0, texts[i], nullptr, "" });
    //menu.lists.emplace_back((uint8_t)i, 0, texts[i], nullptr, "");
  }

  // 初期表示
  // menu.select = 0;
  // menu.selected = -1;
  // menu.cur = 0;
  // menu.idx = 0;
  int listmax = (int)menu.lists.size() - 1;
  drawMainPanel_dialog(&menu, orig, description, desch);   // UI中央描画

  // リスト形式のメニューの表示と選択（skipPress=trueのときはすぐに抜ける）
  while (menu.selected == -1) {
    if (skipPress) break;
    // エンコーダー
    dir = encoderChanged(&menu, false, true);  // エンコーダーの変化あり?
    if (dir != 0) {
      if (dir == 1) {
        if (menu.cur < listmax) menu.cur++;
      } else if (dir == -1) {
        if (menu.cur > 0) menu.cur--;
      }
      drawMainPanel_dialog(&menu, orig, description, desch);   // UI中央描画
    }
    // ボタン押下
    M5.update();
    if (m5BtnAwasReleased()) {
      menu.selected = menu.select;
      if (_debug) spf("Dialog %d selected.\n", menu.selected);
      break;
    }
    delay(5);
  }
  return menu.selected;
}

// 1ボタンだけのダイアログボックスを表示する
int DinMeterUI::selectNotice(String btntext, String title, String description, int desch, bool skipPress) {
  std::vector<String> texts = { btntext };
  return selectDialog(texts, 0, title, description, desch, skipPress);
}

// 画像のダイアログボックスを表示する
void DinMeterUI::imageNotice(uint8_t no, String title, bool skipPress) {
  const int btny = 10 + 2;
  // メインのUIの表示
  std::vector<String> texts = { "CANCEL" };
  int desch = mxy.y -btny;
  selectDialog(texts, 0, title, "", desch, true);
  // 画像の表示
  if (no != ICON_none) {
    XYaddress pp;
    pp.x = mxy.x + (mwh.w - imgInfo[no].width) / 2;
    pp.y = mxy.y + (mwh.h - imgInfo[no].height - btny) / 2;
    lockCanvas();
    _dst->pushImage(pp.x,pp.y, imgInfo[no].width, imgInfo[no].height, imgInfo[no].data);
    unlockCanvas();
    // ボタン押下
    while (!skipPress) {
      M5.update();
      if (m5BtnAwasReleased()) return;
      delay(5);
    } 
  }
  return;
}




