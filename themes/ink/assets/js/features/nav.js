// 页面导航交互：返回顶部 / TOC 滚动高亮 / 移动端侧边栏 / 分组折叠 / 「/」聚焦搜索
export function initNav() {
  // ---------------- 返回顶部（SVG 环形进度 + 箭头） ----------------
  const btn = document.getElementById('back-to-top');
  if (btn) {
    const threshold = parseInt(btn.getAttribute('data-threshold') || '300', 10);
    const ring = btn.querySelector('.btt-progress');
    const CIRC = ring ? 2 * Math.PI * (parseFloat(ring.getAttribute('r')) || 17) : 0;
    if (ring) {
      ring.style.strokeDasharray = CIRC.toFixed(1);
      ring.style.strokeDashoffset = CIRC.toFixed(1);
    }
    function onScroll() {
      if (window.scrollY > threshold) btn.classList.add('show');
      else btn.classList.remove('show');
      if (ring) {
        const max = document.documentElement.scrollHeight - window.innerHeight;
        const p = max > 0 ? Math.min(1, window.scrollY / max) : 0;
        ring.style.strokeDashoffset = (CIRC * (1 - p)).toFixed(1);
      }
    }
    window.addEventListener('scroll', onScroll, { passive: true });
    window.addEventListener('resize', onScroll, { passive: true });
    btn.addEventListener('click', function () { window.scrollTo({ top: 0, behavior: 'smooth' }); });
    onScroll();
  }

  // ---------------- 目录（TOC）高亮：滚动时高亮当前可见章节 ----------------
  const links = Array.prototype.slice.call(document.querySelectorAll('.toc-nav a'));
  if (links.length) {
    const map = links.map(function (a) {
      const id = a.getAttribute('href').slice(1);
      return { a: a, el: document.getElementById(id) };
    }).filter(function (m) { return m.el; });
    if (map.length) {
      function onScroll() {
        const y = window.scrollY + 120;
        let current = null;
        for (const m of map) if (m.el.offsetTop <= y) current = m;
        links.forEach(function (a) { a.classList.remove('active'); });
        if (current) current.a.classList.add('active');
      }
      window.addEventListener('scroll', onScroll, { passive: true });
      onScroll();
    }
  }

  // ---------------- 移动端侧边栏开关（含键盘可访问性） ----------------
  const mbtn = document.getElementById('menu-toggle');
  const nav = document.querySelector('.sidebar.left');
  if (mbtn && nav) {
    if (!nav.id) nav.id = 'sidebar-left';
    mbtn.setAttribute('aria-controls', nav.id);
    mbtn.setAttribute('aria-expanded', 'false');
    let bd = document.getElementById('sidebar-backdrop');
    if (!bd) {
      bd = document.createElement('div');
      bd.id = 'sidebar-backdrop';
      bd.className = 'sidebar-backdrop';
      document.body.appendChild(bd);
    }
    function focusables() {
      return Array.prototype.slice.call(nav.querySelectorAll('a[href], button:not([disabled])'));
    }
    function openNav() {
      nav.classList.add('open'); bd.classList.add('show');
      mbtn.setAttribute('aria-expanded', 'true');
      const f = focusables(); if (f.length) f[0].focus();   // 打开后焦点移入抽屉
    }
    function closeNav() {
      nav.classList.remove('open'); bd.classList.remove('show');
      mbtn.setAttribute('aria-expanded', 'false');
      mbtn.focus();                                          // 关闭后焦点还回汉堡按钮
    }
    mbtn.addEventListener('click', function (e) {
      e.stopPropagation();
      if (nav.classList.contains('open')) closeNav(); else openNav();
    });
    bd.addEventListener('click', closeNav);
    document.addEventListener('keydown', function (e) {
      if (e.key === 'Escape' && nav.classList.contains('open')) { closeNav(); return; }
      // 焦点陷阱：抽屉打开时 Tab / Shift+Tab 在内部循环
      if (e.key === 'Tab' && nav.classList.contains('open')) {
        const f = focusables(); if (!f.length) return;
        const first = f[0], last = f[f.length - 1];
        if (e.shiftKey && document.activeElement === first) { e.preventDefault(); last.focus(); }
        else if (!e.shiftKey && document.activeElement === last) { e.preventDefault(); first.focus(); }
      }
    });
  }

  // ---------------- 侧边栏分组折叠 ----------------
  document.querySelectorAll('.sidebar.left .nav-group').forEach(function (g) {
    const items = g.nextElementSibling;
    if (!items || !items.classList.contains('nav-group-items')) return;
    // 初始化对齐：旧产物折叠组只有内联 display:none、无 collapsed class，
    // 补上 class 使 class 与显示状态一致，否则第一次点击只加 class、第二次才真正展开。
    if (items.style.display === 'none' && !g.classList.contains('collapsed'))
      g.classList.add('collapsed');
    g.addEventListener('click', function () {
      const collapsed = g.classList.toggle('collapsed');
      g.setAttribute('aria-expanded', collapsed ? 'false' : 'true');
      items.style.display = collapsed ? 'none' : '';
    });
  });

  // ---------------- 移动端顶栏：语言切换缩写（中 / EN / 日…） ----------------
  // 顶栏空间有限，窄屏（≤800px）下把语言标签缩成短码，切回宽屏恢复完整文字。
  // 当前语言取 <html lang>，其他语言从 ../<code>/index.html 链接反推。
  (function () {
    const sw = document.querySelector('.locale-switch');
    if (!sw) return;
    const nodes = sw.querySelectorAll('.loc, a');
    nodes.forEach(function (el) { el.setAttribute('data-orig', el.textContent); });
    const ABBR = { zh: '中', ja: '日', ko: '韩', en: 'EN', fr: 'FR', de: 'DE', ru: 'RU', es: 'ES', pt: 'PT', it: 'IT' };
    function codeOf(el) {
      const m = (el.getAttribute('href') || '').match(/\/([a-z]{2}(?:-[A-Z]{2})?)\/index\.html/);
      return m ? m[1].split('-')[0] : (document.documentElement.lang || 'en').split('-')[0];
    }
    function apply() {
      const mobile = window.matchMedia('(max-width: 800px)').matches;
      nodes.forEach(function (el) {
        const code = codeOf(el);
        el.textContent = mobile ? (ABBR[code] || code.slice(0, 2).toUpperCase())
                                : (el.getAttribute('data-orig') || '');
      });
    }
    apply();
    let t = 0;
    window.addEventListener('resize', function () { clearTimeout(t); t = setTimeout(apply, 150); });
  })();

  // ---------------- 快捷键：/ 聚焦搜索框 + 搜索框 Ctrl K 徽标 ----------------
  const searchInput = document.getElementById('search');
  if (searchInput) {
    // 包一层 .search-wrap 并注入键帽徽标（⌘K / Ctrl K），点击徽标聚焦搜索框
    const wrap = document.createElement('span');
    wrap.className = 'search-wrap';
    const kbd = document.createElement('kbd');
    kbd.className = 'search-kbd';
    kbd.textContent = /mac|iphone|ipad/i.test(navigator.platform || '') ? '⌘K' : 'Ctrl K';
    kbd.addEventListener('click', function () { searchInput.focus(); });
    searchInput.parentNode.insertBefore(wrap, searchInput);
    wrap.appendChild(searchInput);
    wrap.appendChild(kbd);
  }
  document.addEventListener('keydown', function (e) {
    if (e.key === '/' && !/^(input|textarea|select)$/i.test(e.target.tagName) && !e.target.isContentEditable) {
      if (searchInput) { e.preventDefault(); searchInput.focus(); }
    }
  });

  // ---------------- 移动端：搜索/语言/GitHub 全部移入抽屉 ----------------
  // header 只保留汉堡 + logo + 主题切换，工具区集中到 .sidebar.left 抽屉顶部
  // （搜索输入 → 完整宽度；语言切换 → 缩写继续生效；GitHub → 留在 header 主题按钮右侧）
  // 跨断点切换用 reload 恢复，避免复杂的 DOM 双向迁移
  (function relocateMobileTools() {
    const sb = document.querySelector('.sidebar.left');
    if (!sb) return;
    const mobile = window.matchMedia('(max-width: 800px)').matches;
    if (!mobile) return;
    // 首页（page-home）无侧边栏：搜索框留在 header、抽屉隐藏，不执行移动
    if (document.body.classList.contains('page-home')) return;
    // 抽屉顶部：仅搜索框（占满宽度），GitHub 图标在 header 主题按钮右侧
    const center = document.querySelector('.topbar-center');
    let wrap = sb.querySelector(':scope > .mobile-tools');
    if (!wrap) {
      wrap = document.createElement('div');
      wrap.className = 'mobile-tools';
      sb.insertBefore(wrap, sb.firstChild);
    }
    if (center && center.parentNode !== wrap) wrap.appendChild(center);
    let last = mobile;
    window.addEventListener('resize', function () {
      const m = window.matchMedia('(max-width: 800px)').matches;
      if (m !== last) location.reload();
      last = m;
    });
  })();
}
