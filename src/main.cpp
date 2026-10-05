#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "mixer.h"

static lv_obj_t *screen, *statusLabel, *cells[24], *groupButtons[4], *soloButton;
static lv_obj_t *wifiIcon, *mixerIcon;
static lv_obj_t *wifiButton, *networkButton, *passField, *hostField, *keyboard;
static lv_obj_t *editorOverlay, *editorField, *editedField;
static lv_obj_t *connectionOverlay, *connectionLabel, *displayIPLabel, *gatewayLabel, *retryButton;
static lv_obj_t *networkRows[6], *networkPager, *previousNetworks, *nextNetworks, *scanButton, *emptyNetworks;
static String chosenSSID, draftPassword, draftHost;
static int networkPage = 0;
static String formNotice;
static int page = 0;
static constexpr int PAGE_MARGIN = 12;
static constexpr int CONTENT_WIDTH = 480 - 2 * PAGE_MARGIN;
static bool keepPassword = true;
static lv_obj_t *auxButtons[10];
static uint16_t draftAux;
static int colorChannel = 0;
static uint32_t draftColor;
static lv_obj_t *colorOptions[8];
static const uint32_t palette[] = {0xB23535,0xB29D35,0x5FB235,0x35B274,0x3588B2,0x4A35B2,0xB235B2,0x7F7F7F};
static const char *ipKeys[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n", ".", "0", LV_SYMBOL_BACKSPACE, ""};
static const lv_btnmatrix_ctrl_t ipKeyControls[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
static void show(int next);
static void refresh();
static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y) {
  lv_obj_t *o = lv_label_create(parent); lv_label_set_text(o, text); lv_obj_set_pos(o, x, y); return o;
}
static void pressed(lv_event_t *e) {
  int id = (intptr_t)lv_event_get_user_data(e);
  if (page == 5 && id < 100) {
    if (id < 10) draftAux ^= 1 << id;
    else if (id == 40 && saveSoloAuxMask(draftAux)) { show(2); return; }
    refresh(); return;
  }
  if (page == 7 && id < 24) { colorChannel = id; show(6); return; }
  if (page == 8 && id < 4) { colorChannel = id; show(9); return; }
  if ((page == 7 || page == 8) && id == 40) { show(3); return; }
  if ((page == 6 || page == 9) && id < 100) {
    if (id < 8) { draftColor = palette[id]; refresh(); }
    else if (id == 40) {
      bool saved = page == 9 ? saveGroupColor(colorChannel, draftColor) : saveChannelColor(colorChannel, draftColor);
      if (saved) show(page == 9 ? 8 : 7);
    }
    return;
  }
  if (id >= 100) { show(id - 100); return; }
  if (page == 0) muteGroup(id);
  if (page == 1) muteChannel(id);
  if (page == 2) {
    if (id < 24) toggleSolo(id);
  }
  refresh();
}
static lv_obj_t *button(const char *text, int x, int y, int w, int h, int id) {
  lv_obj_t *o = lv_btn_create(screen);
  lv_obj_set_pos(o, x, y); lv_obj_set_size(o, w, h);
  lv_obj_add_event_cb(o, pressed, LV_EVENT_CLICKED, (void *)(intptr_t)id);
  lv_obj_t *t = lv_label_create(o); lv_label_set_text(t, text);
  lv_obj_set_width(t, w - 12); lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(t);
  return o;
}
static void setText(lv_obj_t *b, const String &s) {
  lv_obj_t *text = lv_obj_get_child(b, 0);
  if (strcmp(lv_label_get_text(text), s.c_str()) != 0) lv_label_set_text(text, s.c_str());
}
static void color(lv_obj_t *b, uint32_t c) {
  lv_color_t target = lv_color_hex(c);
  if (lv_obj_get_style_bg_color(b, LV_PART_MAIN).full != target.full) lv_obj_set_style_bg_color(b, target, 0);
}
static void disabled(lv_obj_t *b, bool yes) {
  if (yes) lv_obj_add_state(b, LV_STATE_DISABLED); else lv_obj_clear_state(b, LV_STATE_DISABLED);
}
static void dismissConnectionOverlay() {
  if (connectionOverlay) lv_obj_del(connectionOverlay);
  connectionOverlay = nullptr; connectionLabel = nullptr;
}
static void connectionAction(lv_event_t *e) {
  if ((intptr_t)lv_event_get_user_data(e) == 1) {
    if (connectionAttempting()) cancelConnection(); else retryConnection();
  } else { cancelConnection(); dismissConnectionOverlay(); show(3); }
}
static void refreshConnectionOverlay() {
  if (!connectionAttempting() || page >= 3 || editorOverlay) { dismissConnectionOverlay(); return; }
  if (!connectionOverlay) {
    connectionOverlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(connectionOverlay, 480, 480); lv_obj_align(connectionOverlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_clear_flag(connectionOverlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(connectionOverlay, 0, 0); lv_obj_set_style_border_width(connectionOverlay, 0, 0);
    lv_obj_set_style_radius(connectionOverlay, 0, 0);
    lv_obj_set_style_bg_color(connectionOverlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(connectionOverlay, LV_OPA_60, 0);
    lv_obj_t *panel = lv_obj_create(connectionOverlay);
    lv_obj_set_size(panel, 424, 172); lv_obj_center(panel);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE); lv_obj_set_style_pad_all(panel, 12, 0);
    connectionLabel = label(panel, "", 0, 8);
    label(panel, "Voce pode cancelar e corrigir os dados", 0, 40);
    lv_obj_t *b = lv_btn_create(panel); lv_obj_set_size(b, 396, 48); lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(b, connectionAction, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *text = lv_label_create(b); lv_label_set_text(text, "Cancelar / corrigir"); lv_obj_center(text);
  }
  lv_label_set_text(connectionLabel, connectionProgress().c_str());
}
static void closeEditor(bool accept) {
  if (!editorOverlay) return;
  if (accept && editedField && String(lv_textarea_get_text(editedField)) != lv_textarea_get_text(editorField))
    lv_textarea_set_text(editedField, lv_textarea_get_text(editorField));
  lv_keyboard_set_textarea(keyboard, nullptr);
  lv_obj_del(editorOverlay);
  editorOverlay = nullptr; editorField = nullptr; editedField = nullptr; keyboard = nullptr;
}
static void editorAction(lv_event_t *e) {
  closeEditor((intptr_t)lv_event_get_user_data(e) == 1);
}
static void passwordEdited(lv_event_t *) { keepPassword = false; }
static void keyboardEvent(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_READY) closeEditor(true);
  else if (lv_event_get_code(e) == LV_EVENT_CANCEL) closeEditor(false);
}
static void edit(lv_event_t *e) {
  if (page != 3 || editorOverlay) return;
  editedField = (lv_obj_t *)lv_event_get_user_data(e);
  // Use a visible editor on the top layer, independent of screen focus and clipping.
  editorOverlay = lv_obj_create(lv_layer_top());
  lv_obj_set_pos(editorOverlay, 0, 0); lv_obj_set_size(editorOverlay, 480, 480);
  lv_obj_clear_flag(editorOverlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(editorOverlay, 0, 0);
  lv_obj_set_style_border_width(editorOverlay, 0, 0);
  lv_obj_set_style_radius(editorOverlay, 0, 0);
  lv_obj_set_style_bg_color(editorOverlay, lv_color_hex(0x111827), 0);
  lv_obj_set_style_bg_opa(editorOverlay, LV_OPA_COVER, 0);
  label(editorOverlay, editedField == passField ? "Editar senha" : "Editar IP da mesa", 12, 14);
  editorField = lv_textarea_create(editorOverlay);
  lv_obj_set_pos(editorField, 12, 50); lv_obj_set_size(editorField, 456, 52);
  lv_textarea_set_one_line(editorField, true);
  lv_textarea_set_max_length(editorField, editedField == passField ? 63 : 15);
  if (editedField != passField) lv_textarea_set_accepted_chars(editorField, "0123456789.");
  lv_textarea_set_password_mode(editorField, editedField == passField);
  lv_textarea_set_text(editorField, lv_textarea_get_text(editedField));
  keyboard = lv_keyboard_create(editorOverlay);
  lv_obj_set_size(keyboard, 456, 280);
  // Keyboard's constructor uses BOTTOM_MID; reset alignment, not just its offset.
  lv_obj_align(keyboard, LV_ALIGN_TOP_LEFT, 12, 125);
  if (editedField != passField) {
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_1, ipKeys, ipKeyControls);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_USER_1);
  }
  lv_keyboard_set_textarea(keyboard, editorField);
  lv_obj_add_event_cb(keyboard, keyboardEvent, LV_EVENT_ALL, nullptr);
  for (int i = 0; i < 2; ++i) {
    lv_obj_t *b = lv_btn_create(editorOverlay);
    lv_obj_set_pos(b, 12 + i * 234, 424); lv_obj_set_size(b, 222, 44);
    lv_obj_add_event_cb(b, editorAction, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *t = lv_label_create(b); lv_label_set_text(t, i ? "Aplicar" : "Cancelar"); lv_obj_center(t);
  }
  lv_obj_move_foreground(editorOverlay);
}
static void save(lv_event_t *) {
  // An untouched blank password field retains the stored secret.
  bool ok = saveNetworkKeepingPassword(chosenSSID, lv_textarea_get_text(passField),
                                       lv_textarea_get_text(hostField), keepPassword);
  if (ok) show(0);
  else formNotice = "Selecione a rede; confira IP e estado do solo";
}
static lv_obj_t *field(const char *caption, const String &value, int y, int maxLen, bool secret) {
  label(screen, caption, 12, y + 12);
  lv_obj_t *o = lv_textarea_create(screen); lv_obj_set_pos(o, 112, y); lv_obj_set_size(o, 356, 46);
  lv_textarea_set_one_line(o, true); lv_textarea_set_max_length(o, maxLen);
  lv_textarea_set_password_mode(o, secret); lv_textarea_set_text(o, value.c_str());
  // A transparent button handles touch like the working navigation buttons.
  // The textarea underneath keeps the exact same field appearance.
  lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t *hit = lv_btn_create(screen); lv_obj_remove_style_all(hit);
  lv_obj_set_pos(hit, 112, y); lv_obj_set_size(hit, 356, 46);
  lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(hit, edit, LV_EVENT_CLICKED, o);
  return o;
}
static void openNetworks(lv_event_t *) { show(4); }
static void networksAction(lv_event_t *e) {
  int id = (intptr_t)lv_event_get_user_data(e);
  if (id == 10) { networkPage = 0; wifiStartScan(); return; }
  if (id == 11) { if (networkPage > 0) --networkPage; return; }
  if (id == 12) { if ((networkPage + 1) * 6 < (int)wifiNetworks().size()) ++networkPage; return; }
  int index = networkPage * 6 + id;
  if (wifiScanning() || index < 0 || index >= (int)wifiNetworks().size()) return;
  String picked = wifiNetworks()[index].ssid;
  if (picked != chosenSSID) { keepPassword = false; draftPassword = ""; }
  chosenSSID = picked; formNotice = ""; show(3);
}
static lv_obj_t *networkControl(const char *text, int x, int y, int w, int h, int id) {
  lv_obj_t *o = button(text, x, y, w, h, 99);
  lv_obj_remove_event_cb(o, pressed);
  lv_obj_add_event_cb(o, networksAction, LV_EVENT_CLICKED, (void *)(intptr_t)id);
  return o;
}
static lv_obj_t *networkRow(int index) {
  lv_obj_t *row = lv_obj_create(screen);
  lv_obj_remove_style_all(row);
  lv_obj_set_pos(row, 12, 94 + index * 48); lv_obj_set_size(row, 456, 46);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(0x192334), 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(0x263749), LV_STATE_PRESSED);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, lv_color_hex(0x334155), 0);
  lv_obj_set_style_text_color(row, lv_color_hex(0xF1F5F9), 0);
  lv_obj_t *name = label(row, "", 38, 5); lv_obj_set_width(name, 375); lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
  lv_obj_t *detail = label(row, "", 38, 26); lv_obj_set_width(detail, 375);
  lv_obj_set_style_text_color(detail, lv_color_hex(0x94A3B8), 0);
  label(row, LV_SYMBOL_WIFI, 10, 17);
  label(row, LV_SYMBOL_RIGHT, 430, 17);
  lv_obj_add_event_cb(row, networksAction, LV_EVENT_CLICKED, (void *)(intptr_t)index);
  return row;
}
static void show(int next) {
  dismissConnectionOverlay();
  closeEditor(false);
  if (page == 3 && passField && hostField) {
    draftPassword = lv_textarea_get_text(passField);
    draftHost = lv_textarea_get_text(hostField);
  }
  if (next == 3 && page != 3 && page != 4) {
    chosenSSID = wifiSSID(); draftPassword = ""; draftHost = mixerHost(); keepPassword = true; formNotice = "";
  }
  if (next == 6) draftColor = channelColor(colorChannel);
  if (next == 9) draftColor = groupColor(colorChannel);
  page = next;
  lv_obj_t *old = screen;
  retryButton = nullptr;
  keyboard = nullptr; passField = nullptr; hostField = nullptr;
  screen = lv_obj_create(nullptr); lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(screen, 0, 0);
  lv_obj_set_style_border_width(screen, 0, 0);
  lv_obj_set_style_bg_color(screen, lv_color_hex(0x111827), 0);
  const char *tabs[] = {"Grupos", "Canais", "Solo"};
  int activeTab = page == 0 ? 0 : page == 1 ? 1 : page == 2 ? 2 : -1;
  for (int i = 0; i < 3; ++i) {
    lv_obj_t *tab = button(tabs[i], 12 + i * 110, 12, 102, 42, 100 + i);
    color(tab, activeTab == i ? 0x2563EB : 0x334155);
    lv_obj_set_style_radius(tab, 6, 0);
  }
  wifiButton = button("", 342, 12, 126, 42, 103);
  lv_obj_set_style_radius(wifiButton, 6, 0);
  lv_obj_set_style_pad_all(wifiButton, 0, 0);
  wifiIcon = label(wifiButton, LV_SYMBOL_WIFI, 22, 12);
  mixerIcon = label(wifiButton, LV_SYMBOL_AUDIO, 86, 12);
  lv_obj_clear_flag(wifiIcon, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_clear_flag(mixerIcon, LV_OBJ_FLAG_CLICKABLE);
  statusLabel = label(screen, "", 12, page >= 3 && page != 4 ? 110 : 66); lv_obj_set_width(statusLabel, 456);
  lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_DOT);
  if (page < 3 || page == 7) lv_obj_add_flag(statusLabel, LV_OBJ_FLAG_HIDDEN);
  if (page == 0) {
    const char *names[] = {"1  VOCAL", "2  INSTRUMENTOS", "3  BATERIA", "4  SEM FIO"};
    for (int i = 0; i < 4; ++i) groupButtons[i] = button(names[i], 12 + (i % 2) * 234, 66 + (i / 2) * 180, 222, 174, i);

  } else if (page == 1 || page == 2) {
    for (int i = 0; i < 24; ++i) {
      cells[i] = button("", PAGE_MARGIN + (i % 5) * 92, 66 + (i / 5) * 80, 88, 74, i);
      lv_obj_set_style_pad_all(cells[i], 3, 0);
      lv_obj_set_style_radius(cells[i], 7, 0);
      lv_obj_set_style_shadow_width(cells[i], 0, 0);
      lv_obj_remove_event_cb(cells[i], pressed);
      lv_obj_add_event_cb(cells[i], pressed, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
    }

  } else if (page == 3) {
    networkButton = field("Rede", chosenSSID, 136, 32, false);
    lv_textarea_set_placeholder_text(networkButton, "Selecionar rede");
    lv_obj_t *networkHit = lv_obj_get_child(screen, -1);
    lv_obj_remove_event_cb(networkHit, edit);
    lv_obj_add_event_cb(networkHit, openNetworks, LV_EVENT_CLICKED, nullptr);
    passField = field("Senha", draftPassword, 188, 63, true);
    lv_textarea_set_placeholder_text(passField, "Vazio sem editar: manter senha");
    lv_obj_add_event_cb(passField, passwordEdited, LV_EVENT_VALUE_CHANGED, nullptr);
    hostField = field("IP", draftHost, 240, 15, false);
    lv_obj_t *b = button("Salvar e conectar", 12, 296, 456, 42, 99);
    lv_obj_remove_event_cb(b, pressed); lv_obj_add_event_cb(b, save, LV_EVENT_CLICKED, nullptr);
    label(screen, "IP da mesa: --", 12, 352);
    displayIPLabel = label(screen, "", 12, 376);
    gatewayLabel = label(screen, "", 12, 400);

  } else if (page == 4) {
    networkPage = 0;
    for (int i = 0; i < 6; ++i) networkRows[i] = networkRow(i);
    emptyNetworks = label(screen, "", 20, 182); lv_obj_set_width(emptyNetworks, 440);
    lv_obj_set_style_text_align(emptyNetworks, LV_TEXT_ALIGN_CENTER, 0);
    previousNetworks = networkControl(LV_SYMBOL_LEFT, 12, 390, 42, 42, 11);
    networkPager = label(screen, "", 60, 402); lv_obj_set_width(networkPager, 94);
    lv_obj_set_style_text_align(networkPager, LV_TEXT_ALIGN_CENTER, 0);
    nextNetworks = networkControl(LV_SYMBOL_RIGHT, 160, 390, 42, 42, 12);
    scanButton = networkControl(LV_SYMBOL_REFRESH " Buscar redes", 216, 390, 252, 42, 10);
    for (lv_obj_t *arrow : {previousNetworks, nextNetworks}) {
      lv_obj_set_style_radius(arrow, 6, 0); lv_obj_set_style_pad_all(arrow, 0, 0);
      lv_obj_set_style_bg_color(arrow, lv_color_hex(0x334155), 0);
    }
    wifiStartScan();
  }
  if (page >= 3 && page != 4) {
    const char *names[] = {"Wi-Fi", "Aux solo", "Cor canal", "Cor grupo"};
    const int targets[] = {3, 5, 7, 8};
    int tab = page == 3 ? 0 : page == 5 ? 1 : (page == 6 || page == 7) ? 2 : 3;
    for (int i = 0; i < 4; ++i)
      color(button(names[i], 12 + i * 116, 66, 108, 36, 100 + targets[i]), i == tab ? 0x2563EB : 0x334155);
  }
  if (page == 5) {
    draftAux = soloAuxMask();
    for (int i = 0; i < 10; ++i) auxButtons[i] = button(("AUX " + String(i + 1)).c_str(), 12 + (i % 5) * 93, 152 + (i / 5) * 94, 84, 84, i);
    label(screen, "Selecionados: escuta nos fones", 12, 344);
    button("Salvar", 12, 374, 456, 40, 40);
  } else if (page == 7) {
    for (int i = 0; i < 24; ++i) {
      lv_obj_t *b = button(String(i + 1).c_str(), 12 + (i % 6) * 77, 148 + (i / 6) * 66, 71, 60, i);
      color(b, channelColor(i));
    }
  } else if (page == 8) {
    const char *names[] = {"VOCAL", "INSTRUMENTOS", "BATERIA", "SEM FIO"};
    for (int i = 0; i < 4; ++i) color(button(names[i], 12 + (i % 2) * 234, 148 + (i / 2) * 128, 222, 116, i), groupColor(i));
  } else if (page == 6 || page == 9) {
    label(screen, ((page == 9 ? "Cor do grupo " : "Cor do canal ") + String(colorChannel + 1)).c_str(), 12, 148);
    for (int i = 0; i < 8; ++i) {
      colorOptions[i] = button("", 12 + (i % 4) * 117, 180 + (i / 4) * 76, 105, 66, i);
      color(colorOptions[i], palette[i]);
    }
  }
  if (page >= 5) button("Voltar", 12, 426, 222, 42, page == 6 ? 107 : page == 9 ? 108 : 103);
  if (page == 6 || page == 7 || page == 8 || page == 9) button("Salvar", 246, 426, 222, 42, 40);
  else if (page != 4) {
    retryButton = button(page == 1 || page == 2 ? LV_SYMBOL_REFRESH "\nConexao" : "Tentar conexao novamente",
      page == 1 || page == 2 ? 380 : page >= 5 ? 246 : 12,
      page == 1 || page == 2 ? 386 : 426,
      page == 1 || page == 2 ? 88 : page >= 5 ? 222 : 456,
      page == 1 || page == 2 ? 74 : 42, 99);
    lv_obj_remove_event_cb(retryButton, pressed);
    lv_obj_add_event_cb(retryButton, connectionAction, LV_EVENT_CLICKED, (void *)1);
  }
  if (page == 4) button("Voltar as configuracoes", 12, 438, 456, 30, 103);
  lv_scr_load(screen);
  if (old) lv_obj_del(old);
}
static void refresh() {
  refreshConnectionOverlay();
  lv_label_set_text(statusLabel, (page == 4 ? wifiScanStatus() : page == 3 && formNotice.length() ? formNotice : mixerStatus()).c_str());
  uint32_t wifiTint = wifiConnected() ? 0x047857 : 0xB91C1C;
  uint32_t mixerTint = mixerReady() ? 0x047857 : connectionAttempting() && wifiConnected() ? 0xB45309 : 0xB91C1C;
  color(wifiButton, wifiTint);
  if (lv_obj_get_style_bg_grad_color(wifiButton, 0).full != lv_color_hex(mixerTint).full)
    lv_obj_set_style_bg_grad_color(wifiButton, lv_color_hex(mixerTint), 0);
  lv_obj_set_style_bg_grad_dir(wifiButton, LV_GRAD_DIR_HOR, 0);
  lv_obj_set_style_bg_main_stop(wifiButton, 100, 0);
  lv_obj_set_style_bg_grad_stop(wifiButton, 155, 0);
  if (page == 6 || page == 9) {
    for (int i = 0; i < 8; ++i) {
      setText(colorOptions[i], draftColor == palette[i] ? LV_SYMBOL_OK : "");
      lv_obj_set_style_border_width(colorOptions[i], draftColor == palette[i] ? 3 : 0, 0);
      lv_obj_set_style_border_color(colorOptions[i], lv_color_white(), 0);
    }
  }
  bool locked = !mixerReady() || mixerBusy();
  if (retryButton) setText(retryButton, page == 1 || page == 2
    ? connectionAttempting() ? "Cancelar" : LV_SYMBOL_REFRESH "\nConexao"
    : connectionAttempting() ? "Cancelar tentativa" : "Tentar conexao novamente");
  if (page == 3) {
    lv_label_set_text(displayIPLabel, ("IP do display: " + displayIP()).c_str());
    lv_label_set_text(gatewayLabel, ("Gateway: " + gatewayIP()).c_str());
  }
  if (page == 5) {
    for (int i = 0; i < 10; ++i) {
      color(auxButtons[i], draftAux & (1 << i) ? 0x047857 : 0x334155);
      disabled(auxButtons[i], soloActive || mixerBusy());
    }
  }
  if (page == 0) {
    const char *names[] = {"VOCAL", "INSTRUMENTOS", "BATERIA", "SEM FIO"};
    for (int i = 0; i < 4; ++i) {
      bool muted = mixerValue("mgmask").toInt() & (1 << i);
      setText(groupButtons[i], String(i + 1) + "  " + names[i] + "\n" + (locked ? "AGUARDE" : muted ? "FECHADO" : "ABERTO"));
      color(groupButtons[i], muted ? 0x111827 : groupColor(i));
      lv_obj_set_style_border_width(groupButtons[i], 3, 0);
      lv_obj_set_style_border_color(groupButtons[i], lv_color_hex(groupColor(i)), 0);
      disabled(groupButtons[i], !mixerReady());
    }
  }
  if (page == 1 || page == 2) {
    for (int i = 0; i < 24; ++i) {
      String base = "i." + String(i) + ".";
      String name = mixerValue(base + "name");
      if (!name.length()) name = values.find(base + "name") == values.end() ? "..." : "Sem nome";
      String m = mixerValue(base + "mute"), g = mixerValue(base + "mgmask"), f = mixerValue(base + "forceunmute");
      bool known = m.length() && g.length() && f.length();
      bool closed = m.toInt() || ((g.toInt() & mixerValue("mgmask").toInt()) && !f.toInt());
      setText(cells[i], String(i + 1) + "\n" + name);
      lv_obj_set_height(lv_obj_get_child(cells[i], 0), 50);
      uint32_t tint = channelColor(i);
      bool filled = page == 2 ? selected[i] : known && !closed;
      color(cells[i], filled ? tint : 0x111827);
      if (lv_obj_get_style_border_width(cells[i], 0) != 3) lv_obj_set_style_border_width(cells[i], 3, 0);
      if (lv_obj_get_style_border_color(cells[i], 0).full != lv_color_hex(tint).full)
        lv_obj_set_style_border_color(cells[i], lv_color_hex(tint), 0);
      disabled(cells[i], page == 2 ? !mixerReady() : !mixerReady() || !known || selected[i]);
    }
  }
  if (page == 4) {
    const auto &nets = wifiNetworks();
    int pages = max(1, ((int)nets.size() + 5) / 6);
    networkPage = min(networkPage, pages - 1);
    lv_label_set_text(networkPager, (String(networkPage + 1) + " / " + pages).c_str());
    for (int i = 0; i < 6; ++i) {
      int idx = networkPage * 6 + i;
      bool available = idx < (int)nets.size();
      disabled(networkRows[i], wifiScanning());
      if (available) {
        lv_obj_clear_flag(networkRows[i], LV_OBJ_FLAG_HIDDEN);
        const auto &n = nets[idx];
        lv_label_set_text(lv_obj_get_child(networkRows[i], 0), n.ssid.c_str());
        String detail = String(n.secured ? "Protegida" : "Aberta") + "  |  " + n.rssi + " dBm";
        if (n.ssid == chosenSSID) detail += "  |  Selecionada";
        lv_label_set_text(lv_obj_get_child(networkRows[i], 1), detail.c_str());
        color(networkRows[i], n.ssid == chosenSSID ? 0x163A32 : 0x192334);
      } else lv_obj_add_flag(networkRows[i], LV_OBJ_FLAG_HIDDEN);
    }
    if (nets.empty()) {
      lv_obj_clear_flag(emptyNetworks, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(emptyNetworks, wifiScanning() ? "Buscando redes Wi-Fi...\nAguarde a varredura dos canais." : "Nenhuma rede disponivel.\nToque em Buscar redes para repetir.");
    } else lv_obj_add_flag(emptyNetworks, LV_OBJ_FLAG_HIDDEN);
    disabled(previousNetworks, wifiScanning() || networkPage == 0);
    disabled(nextNetworks, wifiScanning() || networkPage >= pages - 1);
    disabled(scanButton, wifiScanning());
  }
}
void setup() { Serial.begin(115200); display_init(); mixerInit(); show(0); }
void loop() {
  static uint32_t previous = millis(), painted = 0;
  uint32_t now = millis(); lv_tick_inc(now - previous); previous = now;
  mixerLoop();
  if (!display_sleeping() && now - painted >= 200) { refresh(); painted = now; }
  lv_timer_handler();
  display_idle(mixerReady());
  mixerStandby(display_sleeping());
  delay(display_sleeping() ? 50 : 5);
}
