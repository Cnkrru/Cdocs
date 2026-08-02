// server.hpp —— 本地静态预览服务器 + 文件监听热重载

#ifndef CDOCS_SERVER_HPP
#define CDOCS_SERVER_HPP

#include "core.hpp"

// serve：启动本地静态预览服务器（仅监听 127.0.0.1）
int cmd_serve(fs::path root, fs::path in, int port, bool build, bool watch, bool open);

#endif  // CDOCS_SERVER_HPP
