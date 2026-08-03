// 明暗主题切换：记忆到 localStorage；首次访问跟随系统配色。
// 用户切换时派发 themechange 事件，供图表等模块重新着色。
// 主题图标（sun/moon）由 SVG 注入，避免硬编码 emoji。
import { T } from '../core/i18n.js';

export function initTheme() {
  const btn = document.getElementById('theme-toggle');
  const root = document.documentElement;

  // giscus 评论 iframe 主题同步（仅在站点启用了自定义 giscus 主题时生效）：
  // 切换明暗时向 giscus iframe 发 postMessage 更新其主题。URL 从 script 的
  // data-theme 派生（替换 light/dark 部分），保留域名与资源指纹，跨语言一致。
  function syncGiscusTheme(theme) {
    const wrap = document.querySelector('.giscus-wrap[data-plugin="giscus"]');
    if (!wrap) return;
    const sc = wrap.querySelector('script[data-custom-theme]');
    if (!sc) return;
    const frame = wrap.querySelector('iframe.giscus-frame');
    if (!frame) return;
    try {
      const url = sc.getAttribute('data-theme')
        .replace(/giscus-(light|dark)\.css/, 'giscus-' + theme + '.css');
      frame.contentWindow.postMessage({ giscus: { setConfig: { theme: url } } }, 'https://giscus.app');
    } catch (e) {}
  }

  function paint() {
    const dark = root.getAttribute('data-theme') === 'dark';
    if (btn) btn.innerHTML = '<span class="icon ' + (dark ? 'icon-moon' : 'icon-sun') + '" aria-hidden="true"></span>';
    if (btn) btn.setAttribute('aria-label', dark ? T('toggleLight', '切换为浅色') : T('toggleDark', '切换为深色'));
    syncGiscusTheme(dark ? 'dark' : 'light');
  }

  try {
    const saved = localStorage.getItem('theme');
    if (saved) root.setAttribute('data-theme', saved);
    else if (window.matchMedia && window.matchMedia('(prefers-color-scheme: light)').matches)
      root.setAttribute('data-theme', 'light');
  } catch (e) {}

  paint();
  if (btn) btn.addEventListener('click', function () {
    const cur = root.getAttribute('data-theme');
    const next = cur === 'dark' ? 'light' : 'dark';
    root.setAttribute('data-theme', next);
    try { localStorage.setItem('theme', next); } catch (e) {}
    paint();
    document.dispatchEvent(new CustomEvent('themechange', { detail: { theme: next } }));
  });
  document.addEventListener('themechange', paint);
}
