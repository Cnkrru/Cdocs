// ⌘K / Ctrl+K 全屏命令面板：复用 FlexSearch + search.json，结果列表 + 键盘选择。
import { T } from '../core/i18n.js';
import { highlight, esc } from '../core/util.js';

export function initCommandPalette() {
  const isMac = /Mac|iPhone|iPod|iPad/.test(navigator.platform || navigator.userAgent);
  const overlay = document.createElement('div');
  overlay.className = 'cmd-overlay';
  overlay.setAttribute('role', 'dialog');
  overlay.setAttribute('aria-modal', 'true');
  overlay.setAttribute('aria-label', T('cmdTitle', '搜索文档'));
  overlay.innerHTML =
    '<div class="cmd-panel">' +
      '<input class="cmd-input" type="text" autocomplete="off" placeholder="' + T('cmdPlaceholder', '搜索文档…') + '" aria-label="' + T('cmdPlaceholder', '搜索文档…') + '">' +
      '<div class="cmd-results"></div>' +
      '<div class="cmd-foot">' +
        '<kbd>' + (isMac ? '⌘' : 'Ctrl') + ' K</kbd> 开关' +
        '<kbd>↑</kbd><kbd>↓</kbd> 选择 <kbd>↵</kbd> 打开 <kbd>Esc</kbd> 关闭' +
      '</div>' +
    '</div>';
  document.body.appendChild(overlay);
  const input = overlay.querySelector('.cmd-input');
  const results = overlay.querySelector('.cmd-results');

  let index = null, data = [], sel = -1, lastFocus = null;

  function ensureFlex() {
    if (typeof FlexSearch !== 'undefined' && FlexSearch.Document) return Promise.resolve();
    return new Promise(function (res, rej) {
      const s = document.createElement('script');
      s.src = 'assets/flexsearch.bundle.min.js';
      s.onload = res; s.onerror = rej;
      document.head.appendChild(s);
    });
  }
  function load() {
    if (index) return Promise.resolve();
    return ensureFlex().then(function () {
      if (typeof FlexSearch === 'undefined' || !FlexSearch.Document) throw new Error('FlexSearch missing');
      return fetch('assets/search.json');
    }).then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
      .then(function (items) {
        data = items;
        index = new FlexSearch.Document({
          tokenize: 'forward',
          encode: function (str) { return String(str).replace(/\s+/g, '').split(''); },
          document: { id: 'id', index: ['title', 'content'], store: ['title', 'file', 'excerpt'] }
        });
        data.forEach(function (it, i) {
          index.add({ id: i, title: it.title, content: it.content || '', excerpt: it.excerpt, file: it.file });
        });
      });
  }
  function open() { lastFocus = document.activeElement; overlay.classList.add('open'); input.value = ''; results.innerHTML = ''; sel = -1; input.focus(); }
  function close() {
    overlay.classList.remove('open'); input.blur();
    if (lastFocus && typeof lastFocus.focus === 'function') lastFocus.focus();  // 还焦到打开前的元素
  }
  function markSel() {
    const els = results.querySelectorAll('.cmd-item');
    els.forEach(function (e, i) { e.classList.toggle('active', i === sel); });
    if (els[sel]) els[sel].scrollIntoView({ block: 'nearest' });
  }
  function render(q) {
    if (!index) return;
    if (!q) { results.innerHTML = '<div class="cmd-empty">' + T('cmdType', '输入关键词开始搜索') + '</div>'; sel = -1; return; }
    const res = index.search(q, { limit: 12 });
    const seen = {}; const list = [];
    res.forEach(function (field) {
      (field.result || []).forEach(function (id) { if (!seen[id]) { seen[id] = 1; list.push(data[id]); } });
    });
    sel = -1;
    if (!list.length) {
      results.innerHTML = '<div class="cmd-empty">' + T('noResults', '没有匹配') + '「' + esc(q) + '」</div>';
      return;
    }
    results.innerHTML = list.map(function (it) {
      const ex = (it.excerpt || '').replace(/\n+/g, ' ');
      return '<a class="cmd-item" data-file="' + esc(it.file) + '">' +
             '<span class="cmd-tt">' + highlight(it.title, q) + '</span>' +
             '<span class="cmd-ex">' + highlight(ex, q).slice(0, 90) + '</span></a>';
    }).join('');
    sel = 0; markSel();
  }

  input.addEventListener('input', function () { render(input.value.trim()); });
  input.addEventListener('keydown', function (e) {
    const els = results.querySelectorAll('.cmd-item');
    if (e.key === 'ArrowDown') { e.preventDefault(); if (els.length) { sel = (sel + 1) % els.length; markSel(); } }
    else if (e.key === 'ArrowUp') { e.preventDefault(); if (els.length) { sel = (sel - 1 + els.length) % els.length; markSel(); } }
    else if (e.key === 'Enter') {
      e.preventDefault();
      const a = els[sel];
      if (a) { try { sessionStorage.setItem('docsgen_jump', input.value.trim()); } catch (e) {} location.href = a.getAttribute('data-file'); }
    }
    else if (e.key === 'Escape') { e.preventDefault(); close(); }
  });
  results.addEventListener('click', function (e) {
    const a = e.target.closest('.cmd-item');
    if (a) { try { sessionStorage.setItem('docsgen_jump', input.value.trim()); } catch (e) {} location.href = a.getAttribute('data-file'); }
  });
  overlay.addEventListener('click', function (e) { if (e.target === overlay) close(); });
  // 焦点陷阱：面板打开时 Tab 在（输入框 + 结果链接）间循环
  overlay.addEventListener('keydown', function (e) {
    if (e.key !== 'Tab') return;
    const f = overlay.querySelectorAll('input.cmd-input, .cmd-item');
    if (!f.length) return;
    const first = f[0], last = f[f.length - 1];
    if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); }
    else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
  });

  document.addEventListener('keydown', function (e) {
    if ((e.key === 'k' || e.key === 'K') && (e.metaKey || e.ctrlKey)) {
      e.preventDefault();
      if (overlay.classList.contains('open')) close();
      else { load().then(open).catch(function (err) { console.warn(err); }); }
    }
  });
  window.__openCmd = open;
}
