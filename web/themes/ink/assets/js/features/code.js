// 代码块增强：文件名标题栏 / 行号 / 行高亮 / 复制按钮
// 解析围栏 info 串（md4c 原样保留进 class）："cpp:foo.cpp{1,3-5}" → lang/file/行集合
import { T } from '../core/i18n.js';
import { escapeHtml } from '../core/util.js';

export function parseCodeMeta(classAttr) {
  const m = (classAttr || '').match(/language-([^\s"']*)/);
  let meta = m ? m[1] : '';
  let lang = meta, file = '', hl = '';
  const b1 = meta.indexOf('{'), b2 = meta.indexOf('}');
  if (b1 >= 0 && b2 > b1) { hl = meta.slice(b1 + 1, b2); lang = meta.slice(0, b1); }
  const c = lang.indexOf(':');
  if (c >= 0) { file = lang.slice(c + 1); lang = lang.slice(0, c); }
  // 解析高亮行范围：1,3-5,8
  const set = new Set();
  hl.split(',').forEach(function (part) {
    part = part.trim(); if (!part) return;
    const d = part.indexOf('-');
    if (d > 0) {
      const a = parseInt(part.slice(0, d), 10), z = parseInt(part.slice(d + 1), 10);
      if (!isNaN(a) && !isNaN(z)) for (let i = a; i <= z; i++) set.add(i);
    } else { const n = parseInt(part, 10); if (!isNaN(n)) set.add(n); }
  });
  return { lang: lang, file: file, hl: set };
}

export function initCodeBlocks() {
  const blocks = document.querySelectorAll('.content pre');
  blocks.forEach(function (pre) {
    const code = pre.querySelector('code');
    if (!code) return;
    const meta = parseCodeMeta(code.className);
    const lang = meta.lang;
    if (lang === 'mermaid') return;   // mermaid 由 diagrams 模块渲染
    const raw = code.textContent;
    let lines = raw.split('\n');
    // md4c 在代码块末尾补一个换行 → 末行是空串，去掉，避免多出一行行号
    if (lines.length && lines[lines.length - 1].trim() === '') lines.pop();

    // 逐行包裹：<span class="code-line"><span class="ln"></span><span class="lc">内容</span></span>
    let html = '';
    lines.forEach(function (ln, i) {
      const n = i + 1;
      const cls = meta.hl.has(n) ? ' class="code-line hl"' : ' class="code-line"';
      html += '<span' + cls + ' data-ln="' + n + '"><span class="ln"></span><span class="lc">'
            + escapeHtml(ln) + '</span></span>\n';
    });
    code.innerHTML = html;

    // 逐行高亮（保持行结构，行号不受影响）
    const known = (typeof hljs !== 'undefined') && lang && hljs.getLanguage && hljs.getLanguage(lang);
    code.querySelectorAll('.code-line').forEach(function (span) {
      const lc = span.querySelector('.lc');
      const t = lc.textContent;
      let out;
      if (known) { try { out = hljs.highlight(t, { language: lang, ignoreIllegals: true }).value; } catch (e) { out = escapeHtml(t); } }
      else out = escapeHtml(t);
      lc.innerHTML = out;
    });

    // 外层包裹 + 文件名标题栏
    const wrapper = document.createElement('div');
    wrapper.className = 'code-block';
    pre.parentNode.insertBefore(wrapper, pre);
    wrapper.appendChild(pre);
    if (meta.lang || meta.file) {
      const bar = document.createElement('div');
      bar.className = 'code-file';
      let html = '';
      if (lang) html += '<span class="code-lang">' + escapeHtml(lang) + '</span>';
      if (meta.file) html += '<span class="code-name">' + escapeHtml(meta.file) + '</span>';
      bar.innerHTML = html;
      wrapper.insertBefore(bar, pre);
    }

    // 复制按钮（复制内容取自 code.textContent，自动剔除行号）
    const btn = document.createElement('button');
    btn.className = 'copy-btn';
    btn.type = 'button';
    btn.setAttribute('aria-label', T('copyCode', '复制代码'));
    btn.innerHTML = '<span class="icon icon-copy" aria-hidden="true"></span>' + T('copy', '复制');
    btn.addEventListener('click', function () {
      const text = code.textContent;
      const done = function () {
        btn.innerHTML = '<span class="icon icon-check" aria-hidden="true"></span>' + T('copied', '已复制');
        btn.classList.add('copied');
        setTimeout(function () {
          btn.innerHTML = '<span class="icon icon-copy" aria-hidden="true"></span>' + T('copy', '复制');
          btn.classList.remove('copied');
        }, 1500);
      };
      const fallback = function () {
        const ta = document.createElement('textarea');
        ta.value = text; ta.style.position = 'fixed'; ta.style.opacity = '0';
        document.body.appendChild(ta); ta.select();
        try { document.execCommand('copy'); done(); } catch (e) {}
        document.body.removeChild(ta);
      };
      if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(text).then(done).catch(fallback);
      } else { fallback(); }
    });
    pre.appendChild(btn);
  });
}
