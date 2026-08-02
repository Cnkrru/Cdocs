// 顶栏搜索：FlexSearch 全文检索，下拉展示命中结果。
// 增强（2026-08）：命中上下文摘要（snippet）、↑↓ 键盘导航 + Enter 跳转、输入防抖、标签徽标。
// 点击结果前记录查询词，目标页据此滚动并高亮命中处（见 jump 模块）。
import { T } from '../core/i18n.js';
import { highlight, esc } from '../core/util.js';

export function initSearch() {
  const input = document.getElementById('search');
  if (!input) return;
  if (typeof FlexSearch === 'undefined' || !FlexSearch.Document) {
    console.warn('FlexSearch 未加载，搜索不可用'); return;
  }
  const isFile = (location.protocol === 'file:');

  // box：包裹搜索框 + 放大镜，也是结果卡片的定位包含块（.search-box 有 position: relative）
  const box = document.createElement('div');
  box.className = 'search-box';
  input.parentNode.insertBefore(box, input);
  box.appendChild(input);
  const ico = document.createElement('span');
  ico.className = 'icon icon-search search-ico';
  ico.setAttribute('aria-label', T('searchPlaceholder', '搜索文档…'));
  ico.setAttribute('aria-hidden', 'true');
  box.insertBefore(ico, input);

  const panel = document.createElement('div');
  panel.className = 'search-results';
  // 卡片挂在搜索框包裹层下（而非 .topbar-right）：CSS 以 .search-box 为定位包含块，
  // 结果面板贴合输入框下方、宽度跟随输入框（桌面居中输入 / 移动端抽屉内均正确）。
  box.appendChild(panel);

  // 跳转前记录查询词，目标页据此滚动并高亮命中处
  panel.addEventListener('click', function (e) {
    const a = e.target.closest('a');
    if (!a) return;
    try { sessionStorage.setItem('docsgen_jump', input.value.trim()); } catch (err) {}
  });

  let index = null, data = [], cur = -1;

  function buildIndex() {
    if (index) return Promise.resolve();
    return fetch('assets/search.json')
      .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
      .then(function (items) {
        data = items;
        index = new FlexSearch.Document({
          tokenize: 'forward',
          encode: function (str) { return String(str).replace(/\s+/g, '').split(''); },
          document: {
            id: 'id',
            index: ['title', 'content', 'tags'],           // 标签也可命中
            store: ['title', 'file', 'excerpt', 'tags']
          }
        });
        data.forEach(function (it, i) {
          index.add({ id: i, title: it.title, content: it.content || '',
                      excerpt: it.excerpt, tags: (it.tags || []).join(' '), file: it.file });
        });
      });
  }

  function close() { panel.style.display = 'none'; cur = -1; }

  // 命中上下文摘要：在正文中定位第一个命中词，截取其前后片段
  function snippet(it, q) {
    const text = (it.content || '') + ' ' + (it.excerpt || '');
    const ql = q.toLowerCase();
    const idx = text.toLowerCase().indexOf(ql);
    if (idx < 0) return (it.excerpt || text).replace(/\n+/g, ' ').slice(0, 80);
    const start = Math.max(0, idx - 42);
    const end = Math.min(text.length, idx + ql.length + 62);
    return (start > 0 ? '…' : '') + text.slice(start, end) + (end < text.length ? '…' : '');
  }

  function render(q) {
    const res = index.search(q, { limit: 8 });
    const seen = {}; const results = [];
    res.forEach(function (field) {
      (field.result || []).forEach(function (id) {
        if (!seen[id]) { seen[id] = 1; results.push(data[id]); }
      });
    });
    if (!results.length) {
      panel.innerHTML = '<div class="search-empty">' + T('noResults', '没有匹配「') + esc(q) +
        T('noResultsSuffix', '」的结果') + '</div>';
    } else {
      panel.innerHTML = results.map(function (it, i) {
        const tags = (it.tags || []).map(function (t) {
          return '<span class="sr-tag">' + esc(t) + '</span>';
        }).join('');
        return '<a href="' + it.file + '"' + (i === cur ? ' class="sr-active"' : '') + '>' +
               '<span class="sr-title">' + highlight(it.title, q) + '</span>' +
               '<span class="sr-excerpt">' + highlight(snippet(it, q), q) + '</span>' +
               (tags ? '<span class="sr-tags">' + tags + '</span>' : '') + '</a>';
      }).join('');
    }
    panel.style.display = 'block';
  }

  let debounce = null;
  function doSearch() {
    const q = input.value.trim();
    if (!q) { close(); return; }
    buildIndex().then(function () {
      if (!index) return;
      cur = -1;
      render(q);
    }).catch(function (e) {
      panel.innerHTML = '<div class="search-empty">' + T('searchLoadFailed', '搜索数据加载失败') +
        (isFile ? T('useHttpServer', '：请用本地服务器(http)访问') : '') + '</div>';
      panel.style.display = 'block';
    });
  }

  input.addEventListener('focus', function () { buildIndex().catch(function (e) { console.warn(e); }); });
  input.addEventListener('input', function () {
    clearTimeout(debounce);
    debounce = setTimeout(doSearch, 150);   // 防抖：停止输入 150ms 后再查
  });

  // 键盘导航：↑↓ 移动高亮、Enter 跳转、Escape 关闭
  input.addEventListener('keydown', function (e) {
    if (e.key === 'Escape') { input.value = ''; close(); return; }
    if (panel.style.display !== 'block') return;
    const links = panel.querySelectorAll('a');
    if (!links.length) return;
    if (e.key === 'ArrowDown' || e.key === 'ArrowUp') {
      e.preventDefault();
      cur = (e.key === 'ArrowDown') ? (cur + 1) % links.length
                                    : (cur - 1 + links.length) % links.length;
      links.forEach(function (a, i) {
        a.classList.toggle('sr-active', i === cur);
      });
      if (links[cur]) links[cur].scrollIntoView({ block: 'nearest' });
    } else if (e.key === 'Enter' && cur >= 0 && links[cur]) {
      e.preventDefault();
      try { sessionStorage.setItem('docsgen_jump', input.value.trim()); } catch (err) {}
      links[cur].click();
    }
  });

  document.addEventListener('click', function (e) {
    if (e.target !== input && !panel.contains(e.target)) close();
  });
}
