# -*- coding: utf-8 -*-
"""不蒜子访问统计插件（Cdocs 插件架构：外部脚本 + JSON 文件交换）。

钩子：on_config —— 构建配置阶段调用一次，返回按语言的「正文末尾注入片段」。
引擎只提供通用注入能力（on_config 输出 inject: {语言: HTML}），不感知统计细节。

数据来源：不蒜子（busuanzi.cc，永久免费）。请求 POST https://cdn.busuanzi.cc/api.php
  body: {"url": 当前页, "referrer": 来源} → 返回 {site_pv, site_uv, page_pv, page_uv}：
    site_pv 站点总访问量 / site_uv 站点独立访客 / page_pv 本页浏览量 / page_uv 本页访客数

UI 设计（完全自定义，不用官方 id 填充）：
  - 页脚（.site-footer .footer-inner）末尾注入「统计」小按钮（内嵌 SVG 图标）
  - 点击弹出浮层面板：遮罩 + 卡片，四项数据千分位展示 + 更新时间
  - 样式全部内嵌（复用主题 CSS 变量 var(--panel)/var(--accent)/var(--border)/var(--muted)），
    自动适配明暗主题与 ink 水墨风格；不依赖任何第三方前端库
  - 关闭：点遮罩 / 右上 × / Esc

启用：.Cdocs/config/config.json 的 center.busuanzi 存在即启用（可为 {} 空对象）。
"""
import json
import os
import sys

# ---- 注入片段模板（占位符用 __XXX__，避免与 JS 的 {} 冲突）----
STYLE = """
<style>
.bsz-btn {
  display: inline-flex; align-items: center; gap: 6px;
  background: none; border: 1px solid var(--border);
  color: var(--muted); border-radius: 999px;
  padding: 4px 14px; font-size: 13px; font-family: inherit;
  cursor: pointer; transition: all .2s ease; line-height: 1.4;
}
.bsz-btn:hover { color: var(--accent); border-color: var(--accent); }
.bsz-overlay {
  position: fixed; inset: 0; z-index: 999;
  background: rgba(0,0,0,.35);
  display: flex; align-items: center; justify-content: center;
  animation: bsz-fade .18s ease;
}
.bsz-panel {
  width: 320px; max-width: calc(100vw - 40px);
  background: var(--panel); color: var(--fg);
  border: 1px solid var(--border); border-radius: 14px;
  box-shadow: 0 16px 48px rgba(0,0,0,.22);
  overflow: hidden; animation: bsz-pop .2s ease;
}
.bsz-head {
  display: flex; align-items: center; justify-content: space-between;
  padding: 14px 18px; border-bottom: 1px solid var(--border);
}
.bsz-title { font-weight: 700; font-size: 15px; }
.bsz-close {
  background: none; border: none; color: var(--muted);
  font-size: 20px; line-height: 1; cursor: pointer;
  padding: 2px 6px; border-radius: 6px;
}
.bsz-close:hover { color: var(--accent); }
.bsz-body { padding: 12px 18px 16px; }
.bsz-row {
  display: flex; justify-content: space-between; align-items: baseline;
  padding: 9px 0; border-bottom: 1px dashed var(--border);
}
.bsz-row:last-of-type { border-bottom: none; }
.bsz-row .lbl { color: var(--muted); font-size: 13px; }
.bsz-row .val {
  font-weight: 700; font-size: 18px; color: var(--accent);
  font-variant-numeric: tabular-nums;
}
.bsz-updated { margin-top: 10px; text-align: right; color: var(--muted); font-size: 12px; }
@keyframes bsz-fade { from { opacity: 0 } to { opacity: 1 } }
@keyframes bsz-pop {
  from { opacity: 0; transform: scale(.96) translateY(8px) }
  to { opacity: 1; transform: none }
}
</style>
"""

SCRIPT = """
<script>
(function () {
  if (window.__bszInjected) return; window.__bszInjected = true;
  var I = { btn: "__BTN__", title: "__TITLE__", pv: "__PV__", uv: "__UV__",
            ppv: "__PPV__", puv: "__PUV__", updated: "__UPDATED__",
            fail: "__FAIL__", empty: "__EMPTY__" };
  var sent = false;

  function fmt(n) {
    if (n === null || n === undefined || n === "") return I.empty;
    var v = Number(n);
    if (isNaN(v)) return String(n);
    return v.toLocaleString();
  }
  function fillAll(r) {
    var el = document.querySelectorAll("[data-bsz]");
    for (var i = 0; i < el.length; i++) {
      var e = el[i], k = e.getAttribute("data-bsz");
      if (!r) continue;
      var v = r[k];
      if (v === undefined && k.indexOf("busuanzi_") === 0) v = r[k.slice(9)];  // 兼容无前缀键
      if (v !== undefined) e.textContent = fmt(v);
    }
    var upd = document.querySelector("[data-bsz-update]");
    if (upd) upd.textContent = r ? I.updated + " " + new Date().toLocaleTimeString() : I.fail;
  }
  function send() {
    if (sent) return; sent = true;
    try {
      var u = new URL("https://cdn.busuanzi.cc/");
      fetch(u.protocol + "//" + u.host + "/api.php", {
        method: "POST",
        body: JSON.stringify({ url: location.href, referrer: document.referrer })
      }).then(function (r) { return r.json(); })
        .then(function (r) { window.__bszData = r; fillAll(r); })
        .catch(function () { fillAll(null); });
    } catch (e) { fillAll(null); }
  }
  function onKey(e) { if (e.key === "Escape") close(); }
  function close() {
    var o = document.getElementById("bsz-overlay");
    if (o) o.remove();
    document.removeEventListener("keydown", onKey);
  }
  function row(lbl, key) {
    return '<div class="bsz-row"><span class="lbl">' + lbl +
           '</span><span class="val" data-bsz="' + key + '">' + I.empty + '</span></div>';
  }
  function openPanel() {
    var exist = document.getElementById("bsz-overlay");
    if (exist) { close(); return; }
    send();
    var o = document.createElement("div");
    o.id = "bsz-overlay"; o.className = "bsz-overlay";
    o.innerHTML =
      '<div class="bsz-panel" role="dialog" aria-label="' + I.title + '">' +
        '<div class="bsz-head"><span class="bsz-title">' + I.title + '</span>' +
        '<button type="button" class="bsz-close" aria-label="close">&times;</button></div>' +
        '<div class="bsz-body">' +
          row(I.pv, "busuanzi_site_pv") + row(I.uv, "busuanzi_site_uv") +
          row(I.ppv, "busuanzi_page_pv") + row(I.puv, "busuanzi_page_uv") +
          '<div class="bsz-updated" data-bsz-update>' + I.empty + '</div>' +
        '</div>' +
      '</div>';
    o.addEventListener("click", function (e) { if (e.target === o) close(); });
    o.querySelector(".bsz-close").addEventListener("click", close);
    document.body.appendChild(o);
    document.addEventListener("keydown", onKey);
    if (window.__bszData) fillAll(window.__bszData);
  }
  function init() {
    var footer = document.querySelector(".site-footer .footer-inner");
    if (!footer) return;
    var tools = document.querySelector(".footer-tools");
    if (!tools) {   // footer.js 未运行（异常兜底）时自建容器，保证按钮入组
      tools = document.createElement("div");
      tools.className = "footer-tools";
      footer.appendChild(tools);
    }
    var b = document.createElement("button");
    b.type = "button"; b.className = "bsz-btn"; b.title = I.title;
    b.innerHTML =
      '<svg viewBox="0 0 24 24" width="14" height="14" aria-hidden="true" style="flex:none">' +
      '<path fill="currentColor" d="M4 13h2v7H4zm6-9h2v16h-2zm6 6h2v10h-2z"/></svg>' +
      '<span>' + I.btn + '</span>';
    b.addEventListener("click", openPanel);
    tools.insertBefore(b, tools.firstChild);   // 统计按钮放到 RSS 左侧
  }
  if (document.readyState === "loading")
    document.addEventListener("DOMContentLoaded", init);
  else init();
})();
</script>
"""

# 文案（按语言；默认中文，en 前缀用英文）
I18N = {
    "zh": {"btn": "站点统计", "title": "站点统计",
           "pv": "站点访问量", "uv": "站点访客",
           "ppv": "本页浏览", "puv": "本页访客",
           "updated": "更新时间", "fail": "统计服务暂不可用", "empty": "—"},
    "en": {"btn": "Stats", "title": "Site Stats",
           "pv": "Total Views", "uv": "Unique Visitors",
           "ppv": "Page Views", "puv": "Page Visitors",
           "updated": "Updated", "fail": "Stats unavailable", "empty": "—"},
}


def load_json(path):
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def write_out(path, obj):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, ensure_ascii=False, indent=2)
    return 0


def build_html(loc):
    """按语言生成注入片段（样式 + 自包含脚本）。"""
    lang = "en" if loc.lower().startswith("en") else "zh"
    t = I18N[lang]
    script = SCRIPT
    for key, val in t.items():
        script = script.replace("__{}__".format(key.upper()), val)
    return "\n<!-- busuanzi 访问统计（插件注入） -->\n" + STYLE + script


def main():
    if len(sys.argv) < 3:
        return 1
    ctx_path, out_path = sys.argv[1], sys.argv[2]
    try:
        ctx = load_json(ctx_path)
        engine = ctx.get("engine", "")
        cfg_path = os.path.join(engine, "config", "config.json")
        if not os.path.exists(cfg_path):
            return write_out(out_path, {"ok": True, "message": "无引擎配置，跳过"})
        cfg = load_json(cfg_path)
        site = cfg.get("site", cfg) if isinstance(cfg, dict) else {}
        center = cfg.get("center", {}) if isinstance(cfg, dict) else {}

        # center.busuanzi 存在（可为 {}）即启用；显式 false 关闭
        bsz = center.get("busuanzi")
        if bsz is False or bsz is None:
            return write_out(out_path, {"ok": True, "message": "未配置 center.busuanzi，跳过"})

        locales = list((site.get("i18n", {}).get("locales", {}) or {}).keys()) or ["zh-CN"]
        inject = {loc: build_html(loc) for loc in locales}
        return write_out(out_path, {
            "ok": True,
            "message": "已注入不蒜子统计（{}）".format(", ".join(inject.keys())),
            "inject": inject,
        })
    except Exception as e:  # 失败隔离：任何异常只报 warning，不阻断构建
        try:
            return write_out(out_path, {"ok": False, "message": "busuanzi.py: {}".format(e)})
        except Exception:
            return 1


if __name__ == "__main__":
    sys.exit(main())
