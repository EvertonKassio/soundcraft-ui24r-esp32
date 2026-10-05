#include <Arduino.h>
#include <lvgl.h>
#include "display.h"
#include "mixer.h"
#include "ui_text_icon.h"

static lv_obj_t *rootScreen, *tabview;
static constexpr int TAB_HEIGHT = 60;
static lv_obj_t *screen, *statusLabel, *cells[24], *groupButtons[6], *soloButton;
static lv_obj_t *wifiIcon, *mixerIcon;
static lv_obj_t *wifiButton, *networkButton, *passField, *hostField, *keyboard;
static lv_obj_t *editorOverlay, *editorField, *editedField;
static lv_obj_t *connectionOverlay, *connectionLabel, *displayIPLabel, *gatewayLabel, *mixerIPLabel, *retryButton;
static lv_obj_t *networkRows[6], *networkPager, *previousNetworks, *nextNetworks, *scanButton, *emptyNetworks;
static String chosenSSID, draftPassword, draftHost;
static int networkPage = 0;
static String formNotice, visibilityNotice;
static int networkReturnPage = 0;
static int page = 0;
static constexpr int PAGE_MARGIN = 12;
static constexpr int CONTENT_WIDTH = 480 - 2 * PAGE_MARGIN;
static bool keepPassword = true;
static lv_obj_t *auxButtons[10];
static uint16_t draftAux;
static int colorChannel = 0;
static uint32_t draftColor, draftVisible;
static uint8_t draftGroupMask;
static int groupSettingsMode = 0;
static int contentTop = TAB_HEIGHT;
static int visibilityMode = 1;
static lv_obj_t *channelGroups[6];
static bool compactGrid;
static lv_obj_t *groupNameField, *channelSettings[24];
static lv_obj_t *colorOptions[8];
static const uint32_t palette[] = {0xB23535,0xB29D35,0x5FB235,0x35B274,0x3588B2,0x4A35B2,0xB235B2,0x7F7F7F};
static const char *ipKeys[] = {"1", "2", "3", "\n", "4", "5", "6", "\n", "7", "8", "9", "\n", ".", "0", LV_SYMBOL_BACKSPACE, ""};
static const lv_buttonmatrix_ctrl_t ipKeyControls[] = {(lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1, (lv_buttonmatrix_ctrl_t)1};
static void show(int next);
static void refresh();
static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y) {
  lv_obj_t *o = lv_label_create(parent); lv_label_set_text(o, text); lv_obj_set_pos(o, x, parent == screen ? y - contentTop : y); return o;
}
static void pressed(lv_event_t *e) {
  int id = (intptr_t)lv_event_get_user_data(e);
  if (page == 5 && id < 100) {
    if (id < 10) draftAux ^= 1 << id;
    else if (id == 40 && saveSoloAuxMask(draftAux)) { show(2); return; }
    refresh(); return;
  }
  if (page == 7 && id < 100) {
    if (id < 24) {
      if (visibilityMode) {
        int bit = visibilityMode == 2 ? id + 24 : id;
        if (bit >= 30 || (visibilityMode == 2 && id >= 6)) return;
        uint32_t target = draftVisible ^ (1UL << bit);
        if (__builtin_popcount(target) <= 24) { draftVisible = target; visibilityNotice = ""; lv_obj_set_hidden(statusLabel, true); }
        else { visibilityNotice = "Limite de 24 botoes: canais + grupos"; lv_obj_set_pos(statusLabel, 12, 408 - contentTop); lv_obj_set_hidden(statusLabel, false); }
        refresh();
      }
      else { colorChannel = id; show(6); }
    } else if (id >= 50 && id <= 52) { visibilityMode = id - 50; show(7); }
    else if (id == 40) {
      if (saveVisibleChannelMask(draftVisible)) show(3);
      else { visibilityNotice = "Nao foi possivel salvar: confira selecao e solos"; lv_obj_set_pos(statusLabel, 12, 408 - contentTop); lv_obj_set_hidden(statusLabel, false); refresh(); }
    }
    return;
  }
  if (page == 8 && id < 100) {
    if (id < 6) {
      if (!groupSettingsMode) { draftGroupMask ^= 1 << id; show(8); }
      else { colorChannel = id; show(9); }
    } else if (id == 40) {
      if (saveGroupDisplayMask(draftGroupMask)) show(3);
      else { lv_label_set_text(statusLabel, "Selecione pelo menos um grupo"); lv_obj_set_hidden(statusLabel, false); }
    }
    return;
  }
  if ((page == 6 || page == 9) && id < 100) {
    if (id < 8) { draftColor = palette[id]; refresh(); }
    else if (id == 40) {
      bool saved = page == 9 ? saveGroupName(colorChannel, lv_textarea_get_text(groupNameField)) && saveGroupColor(colorChannel, draftColor) : saveChannelColor(colorChannel, draftColor);
      if (saved) show(page == 9 ? 8 : 7);
    }
    return;
  }
  if (id >= 100) { show(id - 100); return; }
  if (page == 0) muteGroup(id);
  if (page == 1) { if (id >= 60 && id < 66) muteGroup(id - 60); else muteChannel(id); }
  if (page == 2) {
    if (id < 24) toggleSolo(id);
  }
  refresh();
}
static void networkStatusPressed(lv_event_t *) {
  if (page == 3) { show(networkReturnPage); return; }
  if (page < 3) networkReturnPage = page;
  show(3);
}
static lv_obj_t *button(const char *text, int x, int y, int w, int h, int id) {
  lv_obj_t *o = lv_button_create(screen);
  lv_obj_set_pos(o, x, y - contentTop); lv_obj_set_size(o, w, h);
  lv_obj_set_style_radius(o, 6, 0);
  lv_obj_add_event_cb(o, pressed, LV_EVENT_CLICKED, (void *)(intptr_t)id);
  lv_obj_t *t = lv_label_create(o); lv_label_set_text(t, text);
  lv_obj_set_width(t, w - 12); lv_label_set_long_mode(t, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0); lv_obj_center(t);
  return o;
}
static void setText(lv_obj_t *b, const String &s) {
  lv_obj_t *text = lv_obj_get_child(b, 0);
  if (strcmp(lv_label_get_text(text), s.c_str()) != 0) lv_label_set_text(text, s.c_str());
}
static void color(lv_obj_t *b, uint32_t c) {
  lv_color_t target = lv_color_hex(c);
  if (!lv_color_eq(lv_obj_get_style_bg_color(b, LV_PART_MAIN), target)) lv_obj_set_style_bg_color(b, target, 0);
}
static void updateConnectionButton() {
  if (!wifiIcon || !mixerIcon) return;
  uint32_t wifiTint = wifiConnected() ? 0x34D399 : connectionAttempting() ? 0xFBBF24 : 0xF87171;
  uint32_t mixerTint = mixerReady() ? 0x34D399 : connectionAttempting() && wifiConnected() ? 0xFBBF24 : 0xF87171;
  lv_color_t wifiColor = lv_color_hex(wifiTint);
  lv_color_t mixerColor = lv_color_hex(mixerTint);
  if (!lv_color_eq(lv_obj_get_style_text_color(wifiIcon, LV_PART_MAIN), wifiColor))
    lv_obj_set_style_text_color(wifiIcon, wifiColor, 0);
  if (!lv_color_eq(lv_obj_get_style_image_recolor(mixerIcon, LV_PART_MAIN), mixerColor))
    lv_obj_set_style_image_recolor(mixerIcon, mixerColor, 0);
}
static void disabled(lv_obj_t *b, bool yes) {
  if (yes) lv_obj_add_state(b, LV_STATE_DISABLED); else lv_obj_clear_state(b, LV_STATE_DISABLED);
}
static void dismissConnectionOverlay() {
  if (connectionOverlay) lv_obj_delete(connectionOverlay);
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
    connectionOverlay = lv_obj_create(lv_display_get_layer_top(nullptr));
    lv_obj_set_size(connectionOverlay, 480, 480); lv_obj_align(connectionOverlay, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_scrollable(connectionOverlay, false);
    lv_obj_set_style_pad_all(connectionOverlay, 0, 0); lv_obj_set_style_border_width(connectionOverlay, 0, 0);
    lv_obj_set_style_radius(connectionOverlay, 0, 0);
    lv_obj_set_style_bg_color(connectionOverlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(connectionOverlay, LV_OPA_60, 0);
    lv_obj_t *panel = lv_obj_create(connectionOverlay);
    lv_obj_set_size(panel, 424, 172); lv_obj_center(panel);
    lv_obj_set_scrollable(panel, false); lv_obj_set_style_pad_all(panel, 12, 0);
    connectionLabel = label(panel, "", 0, 8);
    label(panel, "Voce pode cancelar e corrigir os dados", 0, 40);
    lv_obj_t *b = lv_button_create(panel); lv_obj_set_size(b, 396, 48); lv_obj_align(b, LV_ALIGN_BOTTOM_MID, 0, 0);
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
  lv_obj_delete(editorOverlay);
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
  if ((page != 3 && page != 9) || editorOverlay) return;
  editedField = (lv_obj_t *)lv_event_get_user_data(e);
  // Use a visible editor on the top layer, independent of screen focus and clipping.
  editorOverlay = lv_obj_create(lv_display_get_layer_top(nullptr));
  lv_obj_set_pos(editorOverlay, 0, 0); lv_obj_set_size(editorOverlay, 480, 480);
  lv_obj_set_scrollable(editorOverlay, false);
  lv_obj_set_style_pad_all(editorOverlay, 0, 0);
  lv_obj_set_style_border_width(editorOverlay, 0, 0);
  lv_obj_set_style_radius(editorOverlay, 0, 0);
  lv_obj_set_style_bg_color(editorOverlay, lv_color_hex(0x111827), 0);
  lv_obj_set_style_bg_opa(editorOverlay, LV_OPA_COVER, 0);
  label(editorOverlay, page == 9 ? "Nome do grupo" : editedField == passField ? "Editar senha" : "Editar IP da mesa", 12, 14);
  editorField = lv_textarea_create(editorOverlay);
  lv_obj_set_pos(editorField, 12, 50); lv_obj_set_size(editorField, 456, 52);
  lv_textarea_set_one_line(editorField, true);
  lv_textarea_set_max_length(editorField, page == 9 ? 32 : editedField == passField ? 63 : 15);
  if (editedField == hostField) lv_textarea_set_accepted_chars(editorField, "0123456789.");
  lv_textarea_set_password_mode(editorField, editedField == passField);
  lv_textarea_set_text(editorField, lv_textarea_get_text(editedField));
  keyboard = lv_keyboard_create(editorOverlay);
  lv_obj_set_size(keyboard, 456, 280);
  // Keyboard's constructor uses BOTTOM_MID; reset alignment, not just its offset.
  lv_obj_align(keyboard, LV_ALIGN_TOP_LEFT, 12, 125);
  if (editedField == hostField) {
    lv_keyboard_set_map(keyboard, LV_KEYBOARD_MODE_USER_1, ipKeys, ipKeyControls);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_USER_1);
  }
  lv_keyboard_set_textarea(keyboard, editorField);
  lv_obj_add_event_cb(keyboard, keyboardEvent, LV_EVENT_ALL, nullptr);
  for (int i = 0; i < 2; ++i) {
    lv_obj_t *b = lv_button_create(editorOverlay);
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
  lv_obj_t *o = lv_textarea_create(screen); lv_obj_set_pos(o, 112, y - TAB_HEIGHT); lv_obj_set_size(o, 356, 46);
  lv_textarea_set_one_line(o, true); lv_textarea_set_max_length(o, maxLen);
  lv_textarea_set_password_mode(o, secret); lv_textarea_set_text(o, value.c_str());
  // A transparent button handles touch like the working navigation buttons.
  // The textarea underneath keeps the exact same field appearance.
  lv_obj_set_clickable(o, false); lv_obj_set_scrollable(o, false);
  lv_obj_t *hit = lv_button_create(screen); lv_obj_remove_style_all(hit);
  lv_obj_set_pos(hit, 112, y - TAB_HEIGHT); lv_obj_set_size(hit, 356, 46);
  lv_obj_set_scrollable(hit, false);
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
  lv_obj_set_pos(row, 12, 94 + index * 48 - TAB_HEIGHT); lv_obj_set_size(row, 456, 46);
  lv_obj_set_clickable(row, true); lv_obj_set_scrollable(row, false);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(0x192334), 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(0x263749), LV_STATE_PRESSED);
  lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(row, 1, 0);
  lv_obj_set_style_border_color(row, lv_color_hex(0x334155), 0);
  lv_obj_set_style_text_color(row, lv_color_hex(0xF1F5F9), 0);
  lv_obj_t *name = label(row, "", 38, 5); lv_obj_set_width(name, 375); lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_t *detail = label(row, "", 38, 26); lv_obj_set_width(detail, 375);
  lv_obj_set_style_text_color(detail, lv_color_hex(0x94A3B8), 0);
  label(row, LV_SYMBOL_WIFI, 10, 17);
  label(row, LV_SYMBOL_RIGHT, 430, 17);
  lv_obj_add_event_cb(row, networksAction, LV_EVENT_CLICKED, (void *)(intptr_t)index);
  return row;
}
static int pendingTab = -1;
static void switchTab(void *) {
  int target = pendingTab; pendingTab = -1;
  if (target >= 0) show(target);
}
static void tabChanged(lv_event_t *e) {
  int target = lv_tabview_get_tab_active((lv_obj_t *)lv_event_get_target(e));
  if (page >= 3) {
    const int targets[] = {3, 5, 7, 8};
    target = targets[target];
  }
  if (target == page || (target == 3 && page == 4) || (target == 7 && page == 6) || (target == 8 && page == 9)) return;
  if (target == 3 && page < 3) networkReturnPage = page;
  pendingTab = target;
  lv_async_call(switchTab, nullptr);
}
static void innerTabChanged(lv_event_t *e) {
  int index = lv_tabview_get_tab_active((lv_obj_t *)lv_event_get_target(e));
  if (page == 7) { const int modes[] = {1, 2, 0}; visibilityMode = modes[index]; }
  else groupSettingsMode = index;
  pendingTab = page;
  lv_async_call(switchTab, nullptr);
}
static void settingsSubview(bool channels) {
  lv_obj_t *nested = lv_tabview_create(screen);
  lv_obj_set_pos(nested, 0, 6); lv_obj_set_size(nested, 480, 414);
  lv_tabview_set_tab_bar_size(nested, 42);
  lv_obj_set_style_pad_all(nested, 0, 0); lv_obj_set_style_border_width(nested, 0, 0);
  lv_obj_set_style_pad_row(nested, 0, 0);
  lv_obj_t *bar = lv_tabview_get_tab_bar(nested);
  lv_obj_set_style_pad_all(bar, 0, 0); lv_obj_set_style_pad_column(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x1F2937), 0);
  const char *channelTabs[] = {"Visibilidade", "Vis. Grupos", "Cores"};
  const char *groupTabs[] = {"Visibilidade", "Editar"};
  int active = channels ? (visibilityMode == 1 ? 0 : visibilityMode == 2 ? 1 : 2) : groupSettingsMode;
  for (int i = 0; i < (channels ? 3 : 2); ++i) {
    lv_obj_t *pane = lv_tabview_add_tab(nested, channels ? channelTabs[i] : groupTabs[i]);
    lv_obj_set_style_pad_all(pane, 0, 0); lv_obj_set_style_border_width(pane, 0, 0);
    lv_obj_set_style_bg_color(pane, lv_color_hex(0x111827), 0); lv_obj_set_scrollable(pane, false);
    if (i == active) screen = pane;
    lv_obj_t *tab = lv_obj_get_child(bar, i);
    lv_obj_set_style_radius(tab, 0, 0); lv_obj_set_style_pad_all(tab, 4, 0);
    lv_obj_set_style_bg_color(tab, lv_color_hex(0x1F2937), 0);
    lv_obj_set_style_bg_color(tab, lv_color_hex(0x334155), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tab, lv_color_white(), LV_STATE_ANY);
  }
  lv_obj_t *content = lv_tabview_get_content(nested);
  lv_obj_set_style_pad_all(content, 0, 0); lv_obj_set_style_pad_column(content, 0, 0);
  lv_obj_set_scrollable(content, false);
  lv_tabview_set_active(nested, active, LV_ANIM_OFF);
  lv_obj_add_event_cb(nested, innerTabChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  contentTop = 108;
  lv_obj_set_parent(statusLabel, screen);
  lv_obj_set_pos(statusLabel, 12, 408 - contentTop);
}
static void show(int next) {
  if (next >= 3 && page < 3) networkReturnPage = page;
  dismissConnectionOverlay();
  closeEditor(false);
  if (page == 3 && passField && hostField) {
    draftPassword = lv_textarea_get_text(passField);
    draftHost = lv_textarea_get_text(hostField);
  }
  if (next == 3 && page != 3 && page != 4) {
    chosenSSID = wifiSSID(); draftPassword = ""; draftHost = mixerHost(); keepPassword = true; formNotice = "";
  }
  if (next == 7 && page != 6 && page != 7) draftVisible = visibleChannelMask() | ((uint32_t)visibleGroupMask() << 24);
  if (next == 8 && page != 8 && page != 9) draftGroupMask = groupDisplayMask();
  if (next == 6) draftColor = channelColor(colorChannel);
  if (next == 9) draftColor = groupColor(colorChannel);
  page = next; contentTop = TAB_HEIGHT;
  lv_obj_t *old = rootScreen;
  retryButton = nullptr;
  keyboard = nullptr; passField = nullptr; hostField = nullptr;
  rootScreen = lv_obj_create(nullptr);
  lv_obj_set_style_pad_all(rootScreen, 0, 0);
  lv_obj_set_style_bg_color(rootScreen, lv_color_hex(0x111827), 0);
  tabview = lv_tabview_create(rootScreen);
  lv_obj_set_size(tabview, 480, 480);
  lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
  lv_tabview_set_tab_bar_size(tabview, TAB_HEIGHT);
  lv_obj_set_style_pad_all(tabview, 0, 0);
  lv_obj_set_style_border_width(tabview, 0, 0);
  lv_obj_set_style_pad_row(tabview, 0, 0);
  lv_obj_t *bar = lv_tabview_get_tab_bar(tabview);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_set_style_pad_column(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x1F2937), 0);
  const bool settings = page >= 3;
  if (settings) lv_obj_set_style_pad_right(bar, 48, 0);
  const char *mainTabs[] = {"Grupos", "Canais", "Solo", "Config."};
  const char *settingsTabs[] = {"Wi-Fi", "Aux solo", "Canais", "Grupos"};
  const char **tabs = settings ? settingsTabs : mainTabs;
  int activeTab = settings ? (page <= 4 ? 0 : page == 5 ? 1 : page <= 7 ? 2 : 3) : page;
  for (int i = 0; i < 4; ++i) {
    lv_obj_t *pane = lv_tabview_add_tab(tabview, tabs[i]);
    lv_obj_set_style_pad_all(pane, 0, 0);
    lv_obj_set_style_border_width(pane, 0, 0);
    lv_obj_set_style_bg_color(pane, lv_color_hex(0x111827), 0);
    lv_obj_set_scrollable(pane, false);
    if (i == activeTab) screen = pane;
    lv_obj_t *tab = lv_obj_get_child(bar, i);
    lv_obj_set_style_text_font(tab, &lv_font_montserrat_20, 0);
    lv_obj_set_style_pad_all(tab, 4, 0);
    lv_obj_set_style_radius(tab, 0, 0);
    lv_obj_set_style_bg_color(tab, lv_color_hex(0x1F2937), 0);
    lv_obj_set_style_bg_color(tab, lv_color_hex(0x334155), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(tab, lv_color_white(), LV_STATE_ANY);
    lv_obj_set_style_border_color(tab, lv_color_hex(0x60A5FA), LV_STATE_CHECKED);
    lv_obj_set_style_border_side(tab, LV_BORDER_SIDE_BOTTOM, LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tab, 3, LV_STATE_CHECKED);
  }
  lv_obj_t *content = lv_tabview_get_content(tabview);
  lv_obj_set_style_pad_all(content, 0, 0);
  lv_obj_set_style_pad_column(content, 0, 0);
  lv_obj_set_scrollable(content, false);
  lv_tabview_set_active(tabview, activeTab, LV_ANIM_OFF);
  lv_obj_add_event_cb(tabview, tabChanged, LV_EVENT_VALUE_CHANGED, nullptr);
  wifiIcon = nullptr; mixerIcon = nullptr;
  if (!settings) {
  wifiButton = lv_obj_get_child(bar, 3);
  lv_obj_t *caption = lv_obj_get_child(wifiButton, 0);
  lv_obj_align(caption, LV_ALIGN_TOP_MID, 0, -2);
  lv_obj_t *icons = lv_obj_create(wifiButton);
  lv_obj_remove_style_all(icons);
  lv_obj_set_scrollable(icons, false); lv_obj_set_clickable(icons, false);
  wifiIcon = label(icons, LV_SYMBOL_WIFI, 0, 0);
  lv_obj_update_layout(wifiIcon);
  lv_obj_set_size(icons, lv_obj_get_width(wifiIcon) + 18 + 43, 26);
  lv_obj_align(icons, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_align(wifiIcon, LV_ALIGN_LEFT_MID, 0, 0);
  mixerIcon = lv_image_create(icons);
  lv_image_set_src(mixerIcon, &uiTextIcon);
  lv_image_set_scale(mixerIcon, 320); // 125% of the original size (256).
  lv_obj_update_layout(wifiIcon);
  lv_obj_align(mixerIcon, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_set_style_image_recolor(mixerIcon, lv_color_white(), 0);
  lv_obj_set_style_image_recolor_opa(mixerIcon, LV_OPA_COVER, 0);
  lv_obj_set_clickable(wifiIcon, false);
  lv_obj_set_clickable(mixerIcon, false);
  } else {
    lv_obj_t *back = lv_button_create(rootScreen);
    lv_obj_set_pos(back, 432, 0); lv_obj_set_size(back, 48, TAB_HEIGHT);
    lv_obj_set_style_radius(back, 0, 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x334155), 0);
    lv_obj_t *text = lv_label_create(back); lv_label_set_text(text, LV_SYMBOL_LEFT); lv_obj_set_style_text_font(text, &lv_font_montserrat_20, 0); lv_obj_center(text);
    lv_obj_add_event_cb(back, pressed, LV_EVENT_CLICKED, (void *)(intptr_t)(100 + networkReturnPage));
  }
  statusLabel = label(screen, "", 12, page == 3 ? 78 : page >= 5 ? 114 : 66); lv_obj_set_width(statusLabel, page == 3 ? 260 : 456);
  lv_label_set_long_mode(statusLabel, LV_LABEL_LONG_MODE_DOTS);
  if (page == 3) {
    lv_obj_set_x(statusLabel, 88);
    lv_obj_set_height(statusLabel, 42);
    lv_obj_set_style_pad_top(statusLabel, (42 - lv_font_get_line_height(lv_obj_get_style_text_font(statusLabel, LV_PART_MAIN))) / 2, 0);
  }
  if (page < 3 || page >= 5) lv_obj_set_hidden(statusLabel, true);
  if (page == 0) {
    int rows = (__builtin_popcount(groupDisplayMask()) + 1) / 2;
    int height = (354 - (rows - 1) * 8) / rows;
    int slot = 0;
    for (int i = 0; i < 6; ++i) {
      groupButtons[i] = nullptr;
      if (!(groupDisplayMask() & (1 << i))) continue;
      groupButtons[i] = button(groupName(i).c_str(), 12 + (slot % 2) * 232, 66 + (slot / 2) * (height + 8), 224, height, i);
      ++slot;
    }
  } else if (page == 1 || page == 2) {
    int count = __builtin_popcount(visibleChannelMask());
    if (page == 1) count += __builtin_popcount(visibleGroupMask());
    compactGrid = count <= 20;
    int columns = count <= 4 ? 2 : count <= 9 ? 3 : count <= 16 ? 4 : 5;
    int rows = (count + columns - 1) / columns;
    int width = (456 - (columns - 1) * 6) / columns;
    int height = ((compactGrid ? 350 : 394) - (rows - 1) * 6) / rows;
    int slot = 0;
    for (int i = 0; i < 24; ++i) {
      cells[i] = nullptr;
      if (!(visibleChannelMask() & (1UL << i))) continue;
      cells[i] = button("", PAGE_MARGIN + (slot % columns) * (width + 6), 66 + (slot / columns) * (height + 6), width, height, i);
      ++slot;
      lv_obj_set_style_pad_all(cells[i], 3, 0);
      lv_obj_set_style_radius(cells[i], 7, 0);
      lv_obj_set_style_shadow_width(cells[i], 0, 0);
      lv_obj_remove_event_cb(cells[i], pressed);
      lv_obj_add_event_cb(cells[i], pressed, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
    }

    for (int i = 0; i < 6; ++i) {
      channelGroups[i] = nullptr;
      if (page != 1 || !(visibleGroupMask() & (1 << i))) continue;
      channelGroups[i] = button(groupName(i).c_str(), 12 + (slot % columns) * (width + 6), 66 + (slot / columns) * (height + 6), width, height, 60 + i);
      ++slot;
    }
  } else if (page == 3) {
    wifiIcon = label(screen, LV_SYMBOL_WIFI, 12, 88);
    lv_obj_set_style_text_font(wifiIcon, &lv_font_montserrat_20, 0);
    mixerIcon = lv_image_create(screen); lv_image_set_src(mixerIcon, &uiTextIcon);
    lv_image_set_scale(mixerIcon, 320);
    lv_obj_set_pos(mixerIcon, 44, 86 - TAB_HEIGHT);
    lv_obj_set_style_image_recolor_opa(mixerIcon, LV_OPA_COVER, 0);
    networkButton = field("Rede", chosenSSID, 132, 32, false);
    lv_textarea_set_placeholder_text(networkButton, "Selecionar rede");
    lv_obj_t *networkHit = lv_obj_get_child(screen, -1);
    lv_obj_remove_event_cb(networkHit, edit);
    lv_obj_add_event_cb(networkHit, openNetworks, LV_EVENT_CLICKED, nullptr);
    passField = field("Senha", draftPassword, 184, 63, true);
    lv_textarea_set_placeholder_text(passField, "Vazio sem editar: manter senha");
    lv_obj_add_event_cb(passField, passwordEdited, LV_EVENT_VALUE_CHANGED, nullptr);
    hostField = field("IP da mesa", draftHost, 236, 15, false);
    lv_obj_t *b = button("Salvar e conectar", 12, 426, 456, 42, 99);
    color(b, 0x047857);
    lv_obj_remove_event_cb(b, pressed); lv_obj_add_event_cb(b, save, LV_EVENT_CLICKED, nullptr);
    mixerIPLabel = label(screen, ("IP da mesa: " + (mixerReady() ? mixerHost() : String("Verifique o IP"))).c_str(), 12, 300);
    displayIPLabel = label(screen, "", 12, 324);
    gatewayLabel = label(screen, "", 12, 348);

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
  if (page == 5) {
    label(screen, "Reproduzir solo em:", 18, 78);
    draftAux = soloAuxMask();
    for (int i = 0; i < 10; ++i) {
      auxButtons[i] = lv_checkbox_create(screen);
      lv_checkbox_set_text(auxButtons[i], ("Aux." + String(i + 1)).c_str());
      lv_obj_set_pos(auxButtons[i], 18 + (i / 5) * 232, 110 + (i % 5) * 54 - TAB_HEIGHT);
      lv_obj_set_size(auxButtons[i], 212, 46);
      lv_obj_set_style_text_font(auxButtons[i], &lv_font_montserrat_14, 0);
      lv_obj_set_style_text_color(auxButtons[i], lv_color_white(), 0);
      lv_obj_set_style_pad_all(auxButtons[i], 8, 0);
      lv_obj_add_event_cb(auxButtons[i], pressed, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
    }

  } else if (page == 7) {
    settingsSubview(true);
    for (int i = 0; i < (visibilityMode == 2 ? 6 : 24); ++i)
      channelSettings[i] = button(String(i + 1).c_str(), visibilityMode == 2 ? 12 + (i % 2) * 234 : 12 + (i % 6) * 77,
        visibilityMode == 2 ? 116 + (i / 2) * 96 : 116 + (i / 6) * 73,
        visibilityMode == 2 ? 222 : 71, visibilityMode == 2 ? 90 : 67, i);
  } else if (page == 8) {
    lv_obj_set_hidden(statusLabel, true);
    settingsSubview(false);
    for (int i = 0; i < 6; ++i) {
      lv_obj_t *b = button((String(i + 1) + "  " + groupName(i)).c_str(), 12 + (i % 2) * 234, 116 + (i / 2) * 96, 222, 90, i);
      bool visible = draftGroupMask & (1 << i);
      color(b, groupSettingsMode || visible ? groupColor(i) : 0x111827);
      lv_obj_set_style_border_width(b, 2, 0);
      lv_obj_set_style_border_color(b, lv_color_hex(groupColor(i)), 0);
      if (!groupSettingsMode) setText(b, groupName(i) + (visible ? "\nVisivel" : "\nOculto"));
    }
  } else if (page == 6 || page == 9) {
    if (page == 9) {
      label(screen, ("Nome do grupo " + String(colorChannel + 1)).c_str(), 12, 112);
      groupNameField = lv_textarea_create(screen);
      lv_obj_set_pos(groupNameField, 12, 144 - TAB_HEIGHT); lv_obj_set_size(groupNameField, 456, 40);
      lv_textarea_set_one_line(groupNameField, true); lv_textarea_set_max_length(groupNameField, 32);
      lv_textarea_set_text(groupNameField, groupName(colorChannel).c_str());
      lv_obj_set_clickable(groupNameField, false);
      lv_obj_t *hit = lv_button_create(screen); lv_obj_remove_style_all(hit);
      lv_obj_set_pos(hit, 12, 144 - TAB_HEIGHT); lv_obj_set_size(hit, 456, 40);
      lv_obj_add_event_cb(hit, edit, LV_EVENT_CLICKED, groupNameField);
    } else label(screen, ((page == 9 ? "Cor do grupo " : "Cor do canal ") + String(colorChannel + 1)).c_str(), 12, 148);
    for (int i = 0; i < 8; ++i) {
      colorOptions[i] = button("", 12 + (i % 4) * 117, (page == 9 ? 196 : 180) + (i / 4) * 76, 105, 66, i);
      color(colorOptions[i], palette[i]);
    }
  }
  if (page >= 5) color(button("Voltar", 246, 426, 222, 42, page == 6 ? 107 : page == 9 ? 108 : 103), 0x475569);
  if (page >= 5) color(button("Salvar", 12, 426, 222, 42, 40), 0x047857);
  else if (page == 3) {
    retryButton = button("Tentar\nnovamente", 360, 78, 108, 42, 99);
    lv_obj_remove_event_cb(retryButton, pressed);
    lv_obj_add_event_cb(retryButton, connectionAction, LV_EVENT_CLICKED, (void *)1);
  } else if (page != 4) {
    retryButton = button(page == 1 || page == 2 ? LV_SYMBOL_REFRESH "\nConexao" : "Tentar conexao novamente",
      (page == 1 || page == 2) && !compactGrid ? 380 : page >= 5 ? 246 : 12,
      (page == 1 || page == 2) && !compactGrid ? 386 : 426,
      (page == 1 || page == 2) && !compactGrid ? 88 : page >= 5 ? 222 : 456,
      (page == 1 || page == 2) && !compactGrid ? 74 : 42, 99);
    lv_obj_remove_event_cb(retryButton, pressed);
    lv_obj_add_event_cb(retryButton, connectionAction, LV_EVENT_CLICKED, (void *)1);
  }
  if (page == 4) button("Voltar as configuracoes", 12, 438, 456, 30, 103);
  updateConnectionButton();
  lv_screen_load(rootScreen);
  if (old) lv_obj_delete(old);
}
static void refresh() {
  refreshConnectionOverlay();
  lv_label_set_text(statusLabel, (page == 7 && visibilityNotice.length() ? visibilityNotice : page == 4 ? wifiScanStatus() : page == 3 && formNotice.length() ? formNotice : mixerStatus()).c_str());
  updateConnectionButton();
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
    : page == 3 ? (connectionAttempting() ? "Cancelar\ntentativa" : "Tentar\nnovamente") : connectionAttempting() ? "Cancelar tentativa" : "Tentar conexao novamente");
  if (page == 3) {
    lv_label_set_text(mixerIPLabel, ("IP da mesa: " + (mixerReady() ? mixerHost() : String("Verifique o IP"))).c_str());
    lv_label_set_text(displayIPLabel, ("IP do display: " + displayIP()).c_str());
    lv_label_set_text(gatewayLabel, ("Gateway: " + gatewayIP()).c_str());
  }
  if (page == 5) {
    for (int i = 0; i < 10; ++i) {
      if (draftAux & (1 << i)) lv_obj_add_state(auxButtons[i], LV_STATE_CHECKED);
      else lv_obj_clear_state(auxButtons[i], LV_STATE_CHECKED);
      disabled(auxButtons[i], soloActive || mixerBusy());
    }
  }
  if (page == 7) {
    for (int i = 0; i < (visibilityMode == 2 ? 6 : 24); ++i) {
      uint32_t tint = visibilityMode == 2 ? groupColor(i) : channelColor(i);
      bool shown = draftVisible & (1UL << (visibilityMode == 2 ? i + 24 : i));
      color(channelSettings[i], !visibilityMode || shown ? tint : 0x111827);
      lv_obj_set_style_border_width(channelSettings[i], 2, 0);
      lv_obj_set_style_border_color(channelSettings[i], lv_color_hex(tint), 0);
      setText(channelSettings[i], (visibilityMode == 2 ? groupName(i) : String(i + 1)) + (visibilityMode ? shown ? "\nVisivel" : "\nOculto" : ""));
    }
  }
  if (page == 0) {
    for (int i = 0; i < 6; ++i) {
      if (!groupButtons[i]) continue;
      bool muted = mixerValue("mgmask").toInt() & (1 << i);
      setText(groupButtons[i], String(i + 1) + "  " + groupName(i) + "\n" + (locked ? "AGUARDE" : muted ? "FECHADO" : "ABERTO"));
      color(groupButtons[i], muted ? 0x111827 : groupColor(i));
      lv_obj_set_style_border_width(groupButtons[i], 3, 0);
      lv_obj_set_style_border_color(groupButtons[i], lv_color_hex(groupColor(i)), 0);
      disabled(groupButtons[i], !mixerReady());
    }
  }
  if (page == 1) for (int i = 0; i < 6; ++i) if (channelGroups[i]) {
    bool muted = mixerValue("mgmask").toInt() & (1 << i);
    setText(channelGroups[i], "G" + String(i + 1) + "\n" + groupName(i));
    color(channelGroups[i], muted ? 0x111827 : groupColor(i));
    lv_obj_set_style_border_width(channelGroups[i], 3, 0);
    lv_obj_set_style_border_color(channelGroups[i], lv_color_hex(groupColor(i)), 0);
    disabled(channelGroups[i], !mixerReady());
  }
  if (page == 1 || page == 2) {
    for (int i = 0; i < 24; ++i) {
      if (!cells[i]) continue;
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
      if (lv_obj_get_style_border_width(cells[i], LV_PART_MAIN) != 3) lv_obj_set_style_border_width(cells[i], 3, 0);
      if (!lv_color_eq(lv_obj_get_style_border_color(cells[i], LV_PART_MAIN), lv_color_hex(tint)))
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
        lv_obj_set_hidden(networkRows[i], false);
        const auto &n = nets[idx];
        lv_label_set_text(lv_obj_get_child(networkRows[i], 0), n.ssid.c_str());
        String detail = String(n.secured ? "Protegida" : "Aberta") + "  |  " + n.rssi + " dBm";
        if (n.ssid == chosenSSID) detail += "  |  Selecionada";
        lv_label_set_text(lv_obj_get_child(networkRows[i], 1), detail.c_str());
        color(networkRows[i], n.ssid == chosenSSID ? 0x163A32 : 0x192334);
      } else lv_obj_set_hidden(networkRows[i], true);
    }
    if (nets.empty()) {
      lv_obj_set_hidden(emptyNetworks, false);
      lv_label_set_text(emptyNetworks, wifiScanning() ? "Buscando redes Wi-Fi...\nAguarde a varredura dos canais." : "Nenhuma rede disponivel.\nToque em Buscar redes para repetir.");
    } else lv_obj_set_hidden(emptyNetworks, true);
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
