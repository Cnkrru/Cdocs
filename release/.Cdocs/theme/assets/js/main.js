// Cdocs 前端模块化入口
// 由各功能模块（features/*）按职责拆分，这里按序初始化。
// 由 assets/app.js 以动态 import() 加载，从而兼容生成器注入的
// <script src="assets/app.js"></script>（classic script，无需重编译 C++ 生成器）。
import { initTheme } from './features/theme.js';
import { initCodeBlocks } from './features/code.js';
import { initAdmonitions } from './features/admonitions.js';
import { initDiagrams } from './features/diagrams.js';
import { initNav } from './features/nav.js';
import { initScrollProgress } from './features/progress.js';
import { initSearch } from './features/search.js';
import { initCommandPalette } from './features/command-palette.js';
import { initFooter } from './features/footer.js';
import { initFeedback } from './features/feedback.js';
import { initLightbox } from './features/lightbox.js';
import { initJumpLocate } from './features/jump.js';
import { initPWA } from './features/pwa.js';

function boot() {
  initTheme();
  initCodeBlocks();
  initAdmonitions();
  initDiagrams();
  initNav();
  initScrollProgress();
  initSearch();
  initCommandPalette();
  initFooter();
  initFeedback();
  initLightbox();
  initJumpLocate();
  initPWA();
}

// DOM 就绪后初始化（app.js 在 </body> 前注入，通常已就绪；此处仍做守卫）
if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', boot);
else boot();
