// PWA：注册 Service Worker（离线可用）。仅 http/https 注册，file:// 自动跳过。
export function initPWA() {
  if (!('serviceWorker' in navigator)) return;
  if (!location.protocol.indexOf) return;
  if (location.protocol.indexOf('http') !== 0) return;   // file:// 不支持 SW
  window.addEventListener('load', function () {
    var ml = document.querySelector('link[rel="manifest"]');
    var sw = ml ? new URL('./sw.js', ml.href).pathname : './sw.js';
    navigator.serviceWorker.register(sw).catch(function (e) { console.warn('SW 注册失败', e); });
  });
}
