// 搜索跳转定位 / 锚点闪烁高亮：
// 点搜索结果跳到目标页后，滚动到首个命中位置并朱砂闪烁；标题锚点（TOC）点击同款闪烁。
export function initJumpLocate() {
  function flashTo(q) {
    const content = document.querySelector('.content');
    if (!content || !q) return;
    const ql = q.toLowerCase();
    const walker = document.createTreeWalker(content, NodeFilter.SHOW_TEXT, null);
    let target = null;
    while (walker.nextNode()) {
      const node = walker.currentNode;
      if (!node.nodeValue || node.nodeValue.toLowerCase().indexOf(ql) < 0) continue;
      if (!node.parentElement || !content.contains(node.parentElement)) continue;
      target = node.parentElement.closest('h1,h2,h3,h4,p,li,td,code,pre') || node.parentElement;
      break;
    }
    if (!target) return;
    target.classList.add('search-flash');
    try { target.scrollIntoView({ block: 'center', behavior: 'smooth' }); }
    catch (e) { target.scrollIntoView(); }
    setTimeout(function () { target.classList.remove('search-flash'); }, 2000);
  }

  try {
    const q = sessionStorage.getItem('docsgen_jump');
    if (q) {
      sessionStorage.removeItem('docsgen_jump');
      setTimeout(function () { flashTo(q); }, 400);
    }
  } catch (e) {}

  if (location.hash) {
    const el = document.getElementById(location.hash.slice(1));
    if (el) setTimeout(function () {
      el.classList.add('search-flash');
      setTimeout(function () { el.classList.remove('search-flash'); }, 2000);
    }, 300);
  }
}
