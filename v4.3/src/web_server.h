// web_server.h — HTTP 路由
#pragma once
#include <ESPAsyncWebServer.h>

extern AsyncWebServer server;
void setupRoutes();

// 延迟系统动作：HTTP 回调只置位，实际执行在 loop()，
// 避免在 async_tcp 任务中 ESP.restart()（响应发不完）或切换 WiFi 模式（LwIP 重入风险）
void processPendingAction();
