// Admonitions 提示框：把 > [!note] 等块引用渲染成带图标+配色的卡片。
import { IS_ZH } from '../core/i18n.js';

const ADM = {
  note:      { iconCls: 'sticky-note',   cls: 'note',     zh: '提示', en: 'Note' },
  info:      { iconCls: 'info',          cls: 'info',     zh: '信息', en: 'Info' },
  tip:       { iconCls: 'lightbulb',     cls: 'tip',      zh: '技巧', en: 'Tip' },
  success:   { iconCls: 'circle-check',  cls: 'success',  zh: '成功', en: 'Success' },
  example:   { iconCls: 'code',          cls: 'success',  zh: '示例', en: 'Example' },
  warning:   { iconCls: 'triangle-alert',cls: 'warning',  zh: '警告', en: 'Warning' },
  caution:   { iconCls: 'octagon-alert', cls: 'warning',  zh: '注意', en: 'Caution' },
  danger:    { iconCls: 'flame',         cls: 'danger',   zh: '危险', en: 'Danger' },
  bug:       { iconCls: 'bug',           cls: 'danger',   zh: '缺陷', en: 'Bug' },
  important: { iconCls: 'circle-alert',  cls: 'danger',   zh: '重要', en: 'Important' },
  question:  { iconCls: 'circle-help',   cls: 'info',     zh: '疑问', en: 'Question' }
};

export function initAdmonitions() {
  const blocks = document.querySelectorAll('.content blockquote');
  blocks.forEach(function (bq) {
    const first = bq.querySelector('p');
    if (!first) return;
    const mm = first.textContent.match(/^\s*\[!(\w+)\]([^\n]*)/);
    if (!mm) return;                       // 普通引用，不动
    const type = mm[1].toLowerCase();
    const inlineTitle = mm[2].trim();      // 仅取标记所在行的剩余文本作为内联标题
    const conf = ADM[type] || ADM.note;

    // 去掉首行标记（[!type] ...），保留其余正文
    let bodyHtml = first.innerHTML;
    const nl = bodyHtml.indexOf('\n');
    bodyHtml = (nl >= 0 ? bodyHtml.slice(nl) : '').replace(/^\s+/, '');
    first.innerHTML = bodyHtml;

    const div = document.createElement('div');
    div.className = 'admonition ' + conf.cls;

    const head = document.createElement('div');
    head.className = 'adm-head';
    const icon = document.createElement('span'); icon.className = 'icon icon-' + conf.iconCls + ' adm-icon'; icon.setAttribute('aria-hidden', 'true');
    const title = document.createElement('span'); title.className = 'adm-title';
    title.textContent = inlineTitle || (IS_ZH ? conf.zh : conf.en);
    head.appendChild(icon); head.appendChild(title);

    const body = document.createElement('div'); body.className = 'adm-body';
    body.appendChild(first);
    while (bq.firstChild) body.appendChild(bq.firstChild);

    div.appendChild(head); div.appendChild(body);
    bq.parentNode.replaceChild(div, bq);
  });
}
