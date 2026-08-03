// i18n 核心：读取服务端注入的 window.__I18N__（{{key}}+json 标准的客户端部分）
// 缺失时回退到中文硬编码（兼容未开启 i18n 的部署）。
export const I18N = window.__I18N__ || {};

export function T(key, fallback) {
  return Object.prototype.hasOwnProperty.call(I18N, key) ? I18N[key] : fallback;
}

// 当前语言（<html lang> 由构建时注入，如 zh-CN / en）
export const PAGE_LOCALE = (document.documentElement.getAttribute('lang') || 'zh-CN').toLowerCase();
export const IS_ZH = PAGE_LOCALE.indexOf('zh') === 0;
