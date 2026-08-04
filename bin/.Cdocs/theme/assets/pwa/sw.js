/* Cdocs Service Worker —— 离线可用（PWA）
 * 导航请求：network-first，失败回退缓存，再回退首页（离线兜底）
 * 静态资源：stale-while-revalidate（先用缓存，后台更新）
 * 跨域请求（如外部 RSS）直接放行，不缓存
 * 版本升级（v1→v2）时旧的整块 app.js 缓存会被清掉，改用模块化 ES Module 入口。
 */
const CACHE = 'cdocs-v3';
const CORE = [
  './',
  './index.html',
  './assets/css/style.css',
  './assets/deps/highlight-theme.css',
  './assets/css/custom.css',
  './assets/js/app.js',
  './assets/deps/flexsearch.bundle.min.js',
  './assets/deps/highlight.min.js',
  // 前端模块化后的 ES Module 图（build 后随 assets/ 拷贝进 dist）
  './assets/js/main.js',
  './assets/js/core/i18n.js',
  './assets/js/core/util.js',
  './assets/js/features/theme.js',
  './assets/js/features/code.js',
  './assets/js/features/admonitions.js',
  './assets/js/features/diagrams.js',
  './assets/js/features/nav.js',
  './assets/js/features/search.js',
  './assets/js/features/command-palette.js',
  './assets/js/features/footer.js',
  './assets/js/features/feedback.js',
  './assets/js/features/lightbox.js',
  './assets/js/features/jump.js',
  './assets/js/features/pwa.js'
];

self.addEventListener('install', function (e) {
  e.waitUntil(
    caches.open(CACHE)
      .then(function (c) { return c.addAll(CORE).catch(function () {}); })
      .then(function () { return self.skipWaiting(); })
  );
});

self.addEventListener('activate', function (e) {
  e.waitUntil(
    caches.keys()
      .then(function (keys) {
        return Promise.all(keys.filter(function (k) { return k !== CACHE; })
          .map(function (k) { return caches.delete(k); }));
      })
      .then(function () { return self.clients.claim(); })
  );
});

self.addEventListener('fetch', function (e) {
  const req = e.request;
  if (req.method !== 'GET') return;
  const url = new URL(req.url);
  if (url.origin !== self.location.origin) return; // 跨域不处理

  if (req.mode === 'navigate') {
    e.respondWith(
      fetch(req)
        .then(function (res) {
          const cp = res.clone();
          caches.open(CACHE).then(function (c) { c.put(req.url, cp); });
          return res;
        })
        .catch(function () {
          return caches.match(req).then(function (r) { return r || caches.match('./index.html'); });
        })
    );
    return;
  }

  // 静态资源：stale-while-revalidate
  e.respondWith(
    caches.match(req).then(function (cached) {
      const fetched = fetch(req).then(function (res) {
        const cp = res.clone();
        caches.open(CACHE).then(function (c) { c.put(req.url, cp); });
        return res;
      }).catch(function () { return cached; });
      return cached || fetched;
    })
  );
});
