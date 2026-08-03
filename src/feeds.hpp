// feeds.hpp —— RSS 2.0 + JSON Feed 1.1 生成

#ifndef CDOCS_FEEDS_HPP
#define CDOCS_FEEDS_HPP

#include "core.hpp"

// RSS 2.0 + JSON Feed 生成（每语言一份，i18n 站点另在站点根生成默认语言版）
// pages 只应收订阅流（博客文章）；为空时跳过生成（纯文档站无订阅流）。
void gen_feeds(const fs::path& out, const std::string& loc,
               const std::vector<Page>& pages, const SiteConfig& cfg,
               const I18nDict& dict, bool multi, bool silent = false);

// feed 内容签名（博客集 + 相关配置）——增量构建时比对，未变则跳过重算
std::string feed_sig(const std::vector<Page>& posts, const SiteConfig& cfg,
                     const std::string& loc, bool multi);

#endif  // CDOCS_FEEDS_HPP
