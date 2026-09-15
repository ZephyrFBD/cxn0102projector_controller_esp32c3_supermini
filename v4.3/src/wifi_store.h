// wifi_store.h — 多个 WiFi 凭证的持久化（NVS/Preferences，独立于 SPIFFS，刷网页不丢）
#pragma once
#include <Arduino.h>

#define MAX_WIFI_NETS 8

struct WifiNet { String ssid; String pwd; };
extern WifiNet wifiNets[MAX_WIFI_NETS];
extern int wifiNetCount;

void wifiStoreLoad();                                       // 启动载入（含从旧单网络迁移）
bool wifiStoreAdd(const String& ssid, const String& pwd);  // 新增/更新；返回 false=已满
bool wifiStoreRemove(const String& ssid);                  // 删除；返回 false=未找到
String wifiStoreListJson();                                // [{"ssid":"..."}]，不含密码
String jsonEscape(const String& s);                        // JSON 字符串转义（" 和 \）
