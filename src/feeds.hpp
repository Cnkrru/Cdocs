// feeds.hpp —— RSS 2.0 + JSON Feed 1.1 生成

#ifndef CDOCS_FEEDS_HPP
#define CDOCS_FEEDS_HPP

#include "core.hpp"

// RSS 2.0 + JSON Feed 生成（每语言一份，i18n 站点另在站点根生成默认语言版）
void gen_feeds(const fs::path& out, const std::string& loc,
               const std::vector<Page>& pages, const SiteConfig& cfg,
               const I18nDict& dict, bool multi, bool silent = false);

#endif  // CDOCS_FEEDS_HPP
