// wifi_store.cpp — 多 WiFi 凭证存储（Preferences/NVS）
#include "wifi_store.h"
#include "settings.h"   // 旧单网络 savedSSID/savedPWD（用于一次性迁移）
#include <Preferences.h>

WifiNet wifiNets[MAX_WIFI_NETS];
int wifiNetCount = 0;

static Preferences prefs;
static const char* NS = "wifi";

static String keyS(int i) { return "s" + String(i); }
static String keyP(int i) { return "p" + String(i); }

static void persist() {
  prefs.begin(NS, false);
  prefs.putInt("count", wifiNetCount);
  for (int i = 0; i < wifiNetCount; i++) {
    prefs.putString(keyS(i).c_str(), wifiNets[i].ssid);
    prefs.putString(keyP(i).c_str(), wifiNets[i].pwd);
  }
  // 清掉尾部残留的旧键，避免删除后读到脏数据
  for (int i = wifiNetCount; i < MAX_WIFI_NETS; i++) {
    prefs.remove(keyS(i).c_str());
    prefs.remove(keyP(i).c_str());
  }
  prefs.end();
}

void wifiStoreLoad() {
  prefs.begin(NS, false);
  if (!prefs.isKey("count")) {
    // 首次：从旧的单网络（EEPROM）迁移
    wifiNetCount = 0;
    if (savedSSID.length() > 0) {
      wifiNets[0].ssid = savedSSID;
      wifiNets[0].pwd  = savedPWD;
      wifiNetCount = 1;
      Serial.printf("[WiFiStore] Migrated legacy network: %s\n", savedSSID.c_str());
    }
    prefs.putInt("count", wifiNetCount);
    for (int i = 0; i < wifiNetCount; i++) {
      prefs.putString(keyS(i).c_str(), wifiNets[i].ssid);
      prefs.putString(keyP(i).c_str(), wifiNets[i].pwd);
    }
  } else {
    wifiNetCount = prefs.getInt("count", 0);
    if (wifiNetCount > MAX_WIFI_NETS) wifiNetCount = MAX_WIFI_NETS;
    if (wifiNetCount < 0) wifiNetCount = 0;
    for (int i = 0; i < wifiNetCount; i++) {
      wifiNets[i].ssid = prefs.getString(keyS(i).c_str(), "");
      wifiNets[i].pwd  = prefs.getString(keyP(i).c_str(), "");
    }
  }
  prefs.end();
  Serial.printf("[WiFiStore] Loaded %d network(s)\n", wifiNetCount);
}

bool wifiStoreAdd(const String& ssid, const String& pwd) {
  if (ssid.length() == 0) return false;
  // 已存在则更新密码
  for (int i = 0; i < wifiNetCount; i++) {
    if (wifiNets[i].ssid == ssid) {
      wifiNets[i].pwd = pwd;
      persist();
      return true;
    }
  }
  if (wifiNetCount >= MAX_WIFI_NETS) return false;
  wifiNets[wifiNetCount].ssid = ssid;
  wifiNets[wifiNetCount].pwd = pwd;
  wifiNetCount++;
  persist();
  return true;
}

bool wifiStoreRemove(const String& ssid) {
  int idx = -1;
  for (int i = 0; i < wifiNetCount; i++) if (wifiNets[i].ssid == ssid) { idx = i; break; }
  if (idx < 0) return false;
  for (int i = idx; i < wifiNetCount - 1; i++) wifiNets[i] = wifiNets[i + 1];
  wifiNetCount--;
  persist();
  return true;
}

String jsonEscape(const String& s) {
  String o = "";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n' || c == '\r' || c == '\t') o += ' ';
    else o += c;
  }
  return o;
}

String wifiStoreListJson() {
  String json = "[";
  for (int i = 0; i < wifiNetCount; i++) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + jsonEscape(wifiNets[i].ssid) + "\"}";
  }
  json += "]";
  return json;
}
