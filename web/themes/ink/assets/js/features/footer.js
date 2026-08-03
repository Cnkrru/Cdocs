// 页脚：RSS 入口 + 打印按钮
import { T } from '../core/i18n.js';

export function initFooter() {
  const foot = document.querySelector('.site-footer .footer-inner');
  if (!foot) return;
  const tools = document.createElement('div');
  tools.className = 'footer-tools';

  const rss = document.createElement('a');
  rss.className = 'footer-rss';
  rss.href = 'rss.xml';
  rss.innerHTML = '<span class="icon icon-rss" aria-hidden="true"></span>' + T('rssLabel', 'RSS');
  rss.setAttribute('aria-label', T('rssLabel', 'RSS'));
  tools.appendChild(rss);

  const print = document.createElement('button');
  print.className = 'footer-print';
  print.type = 'button';
  print.innerHTML = '<span class="icon icon-printer" aria-hidden="true"></span>' + T('printPage', '打印');
  print.setAttribute('aria-label', T('printPage', '打印'));
  print.addEventListener('click', function () { window.print(); });
  tools.appendChild(print);

  foot.appendChild(tools);
}
