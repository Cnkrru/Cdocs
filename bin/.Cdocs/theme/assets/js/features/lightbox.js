// 图片灯箱（PhotoSwipe 5）：正文图片点击全屏查看
// 能力：图集导航（←/→ 与滑动）、捏合缩放（双击/滚轮）、计数、caption、键盘无障碍、
//       从缩略图位置放大进入的过渡动画（photoSwipe 内建 zoom 动画）。
// 库文件懒加载（assets/deps/photoswipe*.esm.min.js + photoswipe.min.css + pswp-theme.css），
// 首次点击才加载，不拖慢首屏；与 mermaid/katex 同策略，不进 sw CORE 预缓存。
// 链接内图片（<a><img></a>）不触发灯箱；hover 放大镜提示由 .lb-wrap 提供。
import { loadCSSOnce } from '../core/util.js';

// 模块懒加载缓存（Promise，仅加载一次）
let pswpPromise = null;
function loadPswp() {
  if (!pswpPromise) {
    loadCSSOnce('assets/deps/photoswipe.min.css');
    loadCSSOnce('assets/css/pswp-theme.css');
    pswpPromise = Promise.all([
      import('../../deps/photoswipe-lightbox.esm.min.js'),
      import('../../deps/photoswipe.esm.min.js'),
    ]).then(function (mods) {
      return { Lightbox: mods[0].default, Pswp: mods[1].default };
    });
  }
  return pswpPromise;
}

export function initLightbox() {
  const content = document.querySelector('.content');
  if (!content) return;

  // 可点击图片包一层 .lb-wrap，供 CSS 显示 hover「可放大」提示（链接内图片不包）
  content.querySelectorAll('img').forEach(function (im) {
    if (im.closest('a')) return;
    const w = document.createElement('span');
    w.className = 'lb-wrap';
    im.parentNode.insertBefore(w, im);
    w.appendChild(im);
  });

  // 收集当前页可点击图片（排除链接内的），等加载完成取真实尺寸
  async function collectItems() {
    const imgs = [];
    content.querySelectorAll('img').forEach(function (im) {
      if (im.closest('a')) return;
      imgs.push(im);
    });
    const items = [];
    for (const im of imgs) {
      if (!im.complete && typeof im.decode === 'function') {
        try { await im.decode(); } catch (e) { /* 忽略解码失败，走兜底尺寸 */ }
      }
      items.push({
        src: im.currentSrc || im.src,
        width: im.naturalWidth || im.width || 1200,
        height: im.naturalHeight || im.height || 800,
        alt: im.alt || '',
      });
    }
    return { items: items, imgs: imgs };
  }

  content.addEventListener('click', function (e) {
    const t = e.target;
    // 点击图片本身或 .lb-wrap 包装器（hover 遮罩/放大镜可能成为实际命中目标）都触发
    const img = t && t.tagName === 'IMG'
      ? t
      : (t && t.classList && t.classList.contains('lb-wrap') ? t.querySelector('img') : null);
    if (img && !img.closest('a')) {
      e.preventDefault();
      openLightbox(img);
    }
  });

  async function openLightbox(clicked) {
    try {
      const { items, imgs } = await collectItems();
      if (!items.length) return;
      const index = Math.max(0, imgs.indexOf(clicked));
      const { Lightbox, Pswp } = await loadPswp();

      const lightbox = new Lightbox({
        pswpModule: Pswp,
        bgOpacity: 0.92,
        showHideAnimationType: 'zoom',
      });

      // caption：显示图片 alt（PhotoSwipe 5 无内建 caption，手动注册底部说明条）
      lightbox.on('init', function () {
        const pswp = lightbox.pswp;
        const cap = document.createElement('div');
        cap.className = 'pswp-caption';
        pswp.element.appendChild(cap);
        lightbox.on('change', function () {
          const d = pswp.currSlide.data;
          if (cap) {
            cap.textContent = d.alt || '';
            cap.classList.toggle('pswp-caption-empty', !d.alt);
          }
        });
      });

      lightbox.loadAndOpen({ dataSource: items, index: index });
    } catch (err) {
      // 库加载失败：退回原生行为（不阻断页面）
      console.error('[Cdocs lightbox] PhotoSwipe 加载失败：', err);
    }
  }
}
