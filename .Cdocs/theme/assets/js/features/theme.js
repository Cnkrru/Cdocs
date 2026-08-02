// 明暗主题切换：记忆到 localStorage；首次访问跟随系统配色。
// 用户切换时派发 themechange 事件，供图表等模块重新着色。
// 主题图标（sun/moon）由 SVG 注入，避免硬编码 emoji。
import { T } from '../core/i18n.js';

export function initTheme() {
  const btn = document.getElementById('theme-toggle');
  const root = document.documentElement;

  function paint() {
    const dark = root.getAttribute('data-theme') === 'dark';
    if (btn) btn.innerHTML = '<span class="icon ' + (dark ? 'icon-moon' : 'icon-sun') + '" aria-hidden="true"></span>';
    if (btn) btn.setAttribute('aria-label', dark ? T('toggleLight', '切换为浅色') : T('toggleDark', '切换为深色'));
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
