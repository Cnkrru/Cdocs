// 「本页有帮助吗？」反馈条：选择写入 localStorage（按页面记忆）
// 若 config.feedback.endpoint 已配置（window.__CDOCS_FEEDBACK__），点击时用 sendBeacon 上报统计；
// 未配置则纯本地记忆（不产生任何网络请求）。
import { T } from '../core/i18n.js';

const FEEDBACK_EP = window.__CDOCS_FEEDBACK__ || '';

export function initFeedback() {
  const content = document.querySelector('.content');
  if (!content) return;
  const pageKey = 'fb:' + location.pathname + location.search;

  const wrap = document.createElement('div');
  wrap.className = 'feedback';
  const q = document.createElement('span'); q.className = 'fb-q';
  q.textContent = T('feedbackTitle', '本页有帮助吗？');
  const btns = document.createElement('div'); btns.className = 'fb-btns';
  const yes = document.createElement('button'); yes.type = 'button';
  yes.innerHTML = '<span class="icon icon-thumbs-up" aria-hidden="true"></span>' + T('feedbackYes', '有帮助');
  const no = document.createElement('button'); no.type = 'button';
  no.innerHTML = '<span class="icon icon-thumbs-down" aria-hidden="true"></span>' + T('feedbackNo', '需改进');
  const thanks = document.createElement('span'); thanks.className = 'fb-thanks';
  thanks.textContent = T('feedbackThanks', '感谢你的反馈！'); thanks.style.display = 'none';
  btns.appendChild(yes); btns.appendChild(no);
  wrap.appendChild(q); wrap.appendChild(btns); wrap.appendChild(thanks);

  const ref = document.querySelector('.page-meta');
  if (ref && ref.parentNode) ref.parentNode.insertBefore(wrap, ref.nextSibling);
  else content.appendChild(wrap);

  // 上报统计（仅当配置了端点；sendBeacon 对页面卸载更友好，不可用时退回 fetch+keepalive）
  function report(v) {
    if (!FEEDBACK_EP) return;
    try {
      const data = JSON.stringify({
        page: location.pathname + location.search,
        title: document.title,
        vote: v,
        locale: (document.documentElement.getAttribute('lang') || '').toLowerCase()
      });
      if (navigator.sendBeacon) navigator.sendBeacon(FEEDBACK_EP, data);
      else fetch(FEEDBACK_EP, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: data, keepalive: true }).catch(function () {});
    } catch (e) {}
  }

  let saved = '';
  try { saved = localStorage.getItem(pageKey) || ''; } catch (e) {}
  function mark(v) {
    wrap.classList.add('done'); thanks.style.display = '';
    try { localStorage.setItem(pageKey, v); } catch (e) {}
    report(v);
  }
  if (saved) mark(saved);
  yes.addEventListener('click', function () { mark('yes'); });
  no.addEventListener('click', function () { mark('no'); });
}
