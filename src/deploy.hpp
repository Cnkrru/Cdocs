// deploy.hpp —— 部署子命令（内置，对标 mkdocs gh-deploy / hexo deploy）
#ifndef CDOCS_DEPLOY_HPP
#define CDOCS_DEPLOY_HPP

#include <string>
#include "core.hpp"

// deploy：构建站点并发布。
//   remote  ：远端 URL（空 → 读 config site.deploy.remote → 已有 origin → 从 site.url 推断）
//   branch  ：目标分支（空 → config site.deploy.branch → "gh-pages"）
//   message ：提交信息（空 → config site.deploy.message → "Deploy Cdocs site"）
//   force   ：构建前清空输出目录（默认仅在 dist 无 .git 时清空，避免丢部署历史）
//   toVercel：true 时发布到 Vercel（构建后调 vercel CLI：vercel --prod --yes <dest>），
//             忽略 remote/branch/message（无需 git 仓库）。
// 返回退出码：0=成功，1=运行错误，2=用法错误。
int cmd_deploy(fs::path source, fs::path dest,
               std::string remote, std::string branch,
               std::string message, bool force, bool toVercel = false);

#endif  // CDOCS_DEPLOY_HPP
