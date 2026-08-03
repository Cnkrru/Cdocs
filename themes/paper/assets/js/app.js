// app.js —— paper 主题引导（精简：主题切换）
(function () {
  'use strict';
  var toggle = document.getElementById('theme-toggle');
  var map = {
    sun: '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>',
    moon: '<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z"/></svg>'
  };
  function current() {
    return document.documentElement.getAttribute('data-theme') === 'dark' ? 'sun' : 'moon';
  }
  if (toggle) {
    toggle.innerHTML = map[current()] || '';
    toggle.addEventListener('click', function () {
      var next = current() === 'sun' ? 'light' : 'dark';
      document.documentElement.setAttribute('data-theme', next);
      try { localStorage.setItem('paper-theme', next); } catch (e) {}
      toggle.innerHTML = map[current()] || '';
    });
  }
})();
