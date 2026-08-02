// pwa.hpp —— PWA：manifest + service worker 生成

#ifndef CDOCS_PWA_HPP
#define CDOCS_PWA_HPP

#include "core.hpp"

// PWA：复制 sw.js + icon.svg，并写 manifest.webmanifest（替代外部 gen-pwa.js）
void gen_pwa(const fs::path& out, const SiteConfig& cfg, const std::string& siteName,
             const fs::path& assetsSrc);

#endif  // CDOCS_PWA_HPP
