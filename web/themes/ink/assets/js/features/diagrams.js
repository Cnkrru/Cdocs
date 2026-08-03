// Mermaid 流程图 / 时序图 / 类图（```mermaid）与 KaTeX 数学公式（$$...$$ / \(...\) / $...$）
// 均为第三方库懒加载，仅在页面存在对应内容时才注入脚本，不拖慢首屏。
import { T, IS_ZH } from '../core/i18n.js';
import { loadMermaid, loadKaTeX, loadAutoRender } from '../core/util.js';

export function initDiagrams() {
  // ---------------- Mermaid ----------------
  const codes = document.querySelectorAll('.content pre > code.language-mermaid');
  if (codes.length) {
    const nodes = [];
    codes.forEach(function (code) {
      const pre = code.parentElement;
      const src = code.textContent;
      const wrap = document.createElement('div');
      wrap.className = 'mermaid-block';
      const inner = document.createElement('div');
      inner.className = 'mermaid';
      inner.textContent = src;
      wrap.appendChild(inner);
      pre.parentNode.insertBefore(wrap, pre);
      pre.parentNode.removeChild(pre);
      nodes.push({ node: inner, src: src });
    });

    function render() {
      const dark = document.documentElement.getAttribute('data-theme') === 'dark';
      loadMermaid().then(function () {
        mermaid.initialize({
          startOnLoad: false,
          theme: dark ? 'dark' : 'default',
          securityLevel: 'loose',
          fontFamily: 'inherit'
        });
        nodes.forEach(function (n) { n.node.innerHTML = ''; n.node.textContent = n.src; });
        return mermaid.run({ nodes: nodes.map(function (n) { return n.node; }) });
      }).catch(function (err) {
        nodes.forEach(function (n) {
          n.node.classList.add('mermaid-error');
          n.node.textContent = T('mermaidError', '图表渲染失败') + ': ' + (err && err.message ? err.message : err);
        });
      });
    }

    render();
    document.addEventListener('themechange', function () { render(); });
  }

  // ---------------- KaTeX ----------------
  function maybeRender() {
    loadKaTeX().then(loadAutoRender).then(function () {
      if (typeof renderMathInElement === 'undefined') throw new Error('auto-render missing');
      renderMathInElement(document.body, {
        delimiters: [
          { left: '$$', right: '$$', display: true },
          { left: '\\[', right: '\\]', display: true },
          { left: '\\(', right: '\\)', display: false },
          { left: '$', right: '$', display: false }
        ],
        ignoredTags: ['script', 'noscript', 'style', 'textarea', 'pre', 'code'],
        ignoredClasses: ['mermaid', 'no-katex'],
        throwOnError: false
      });
    }).catch(function (e) { console.warn('KaTeX 渲染失败', e); });
  }
  // 仅在页面疑似包含公式时才加载，避免无谓的网络请求
  if (/\$|\\\(|\\\[/.test(document.body.textContent)) maybeRender();
}
