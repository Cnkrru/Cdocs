// 顶部阅读进度条：页面顶部 2px 细条随滚动显示阅读进度（VitePress 风格）
export function initScrollProgress() {
  const bar = document.createElement('div');
  bar.id = 'scroll-progress';
  bar.setAttribute('aria-hidden', 'true');
  document.body.appendChild(bar);

  function onScroll() {
    const max = document.documentElement.scrollHeight - window.innerHeight;
    const p = max > 0 ? Math.min(1, window.scrollY / max) : 0;
    bar.style.width = (p * 100).toFixed(2) + '%';
  }
  window.addEventListener('scroll', onScroll, { passive: true });
  window.addEventListener('resize', onScroll, { passive: true });
  onScroll();
}
