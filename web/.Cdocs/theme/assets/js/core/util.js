// 通用工具：HTML 转义、命中词高亮、第三方库（mermaid / KaTeX）懒加载。
// 库文件随 assets/ 一起拷贝进 dist，用到时才注入 <script>/<link>，不拖慢首屏。

export function esc(s) {
  return String(s == null ? '' : s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}
export const escapeHtml = esc;

// 命中词高亮（搜索结果用）
export function highlight(text, q) {
  const safe = esc(text);
  if (!q) return safe;
  const qe = q.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  try {
    return safe.replace(new RegExp(qe, 'gi'), function (m) { return '<mark>' + m + '</mark>'; });
  } catch (e) { return safe; }
}

// ---------------- 第三方库懒加载 ----------------
export function loadScriptOnce(src) {
  return new Promise(function (res, rej) {
    const ex = document.querySelector('script[src="' + src + '"]');
    if (ex) {
      if (ex.dataset.loaded === '1') return res();
      ex.addEventListener('load', function () { res(); });
      ex.addEventListener('error', function () { rej(new Error('load ' + src + ' failed')); });
      return;
    }
    const s = document.createElement('script');
    s.src = src; s.async = true;
    s.onload = function () { s.dataset.loaded = '1'; res(); };
    s.onerror = function () { rej(new Error('load ' + src + ' failed')); };
    document.head.appendChild(s);
  });
}

export function loadCSSOnce(href) {
  if (document.querySelector('link[href="' + href + '"]')) return;
  const l = document.createElement('link');
  l.rel = 'stylesheet'; l.href = href;
  document.head.appendChild(l);
}

// 页面由 C++ 注入 window.__cdocs_asset_root（如 ../assets/ 或 assets/）
function assetPath(rel) { return (window.__cdocs_asset_root || 'assets/') + rel; }

export function loadMermaid() {
  return loadScriptOnce(assetPath('deps/mermaid.min.js')).then(function () {
    if (typeof mermaid === 'undefined') throw new Error('mermaid not loaded');
    return mermaid;
  });
}

export function loadKaTeX() {
  loadCSSOnce(assetPath('deps/katex.min.css'));
  return loadScriptOnce(assetPath('deps/katex.min.js')).then(function () {
    if (typeof katex === 'undefined') throw new Error('katex not loaded');
    return katex;
  });
}

export function loadAutoRender() {
  return loadScriptOnce(assetPath('deps/auto-render.min.js'));
}
