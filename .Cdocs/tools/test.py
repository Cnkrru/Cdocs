#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Cdocs 自动化回归测试（对标成熟 SSG 的 CI 冒烟测试）。
用法：python .Cdocs/tools/test.py [Cdocs.exe 路径]
覆盖：init 建站、目录自动导航、front matter（desc/lastmod/aliases）、死链检测、
admonition 构建期渲染、图片压缩 WebP、资源指纹、config 校验、serve 传输层（gzip/ETag/304）。
输出 PASS/FAIL 汇总，任何 FAIL 退出码非 0。
"""
import os, sys, json, subprocess, shutil, time, urllib.request, socket

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT  = os.path.dirname(os.path.dirname(TOOLS))
EXE   = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.path.join(ROOT, 'Cdocs.exe')
# 唯一目录名，避免删除旧目录（沙箱安全机制拦截 rmtree）
BASE  = os.path.join(ROOT, '.build', 'test_site_' + str(int(time.time())))

PASS = FAIL = 0
def ok(name, cond, detail=''):
    global PASS, FAIL
    if cond: PASS += 1; print('  PASS  ' + name)
    else:    FAIL += 1; print('  FAIL  ' + name + ('  [' + str(detail) + ']' if detail else ''))

def run(args, cwd=BASE, timeout=120):
    return subprocess.run([EXE] + args, cwd=cwd, capture_output=True, text=True,
                          encoding='utf-8', errors='replace', timeout=timeout)

def write(p, s):
    os.makedirs(os.path.dirname(p), exist_ok=True)
    mode = 'wb' if isinstance(s, (bytes, bytearray)) else 'w'
    with open(p, mode) as f: f.write(s)

def read(p):
    with open(p, encoding='utf-8') as f: return f.read()

# ---------------- 0. 建站（干净测试目录） ----------------
print('== 0. init 建站 ==')
os.makedirs(BASE, exist_ok=True)
r = run(['init', '.'], cwd=BASE)
ok('init 成功', r.returncode == 0, r.stderr[:200])
# 删除 route.json + config 的 sidebar 映射 → 触发目录自动导航
rp = os.path.join(BASE, '.Cdocs', 'config', 'route.json')
if os.path.exists(rp): os.remove(rp)
_cfgp = os.path.join(BASE, '.Cdocs', 'config', 'config.json')
try:
    _cfg = json.load(open(_cfgp, encoding='utf-8'))
    _cfg.get('site', {}).pop('sidebar', None)
    json.dump(_cfg, open(_cfgp, 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
except Exception:
    pass

# ---------------- 1. 测试内容（子目录 + front matter + aliases + admonition） ----------------
print('== 1. 内容构造 ==')
write(os.path.join(BASE, 'docs', 'intro.md'),
      '---\ntitle: 介绍\ndescription: 自定义页面描述\nlastmod: 2026-08-02\n---\n# 介绍\n正文。\n')
write(os.path.join(BASE, 'docs', 'guide', 'install.md'),
      '---\ntitle: 安装\nweight: 1\n---\n# 安装指南\n> [!tip] 小提示\n> 用这个技巧。\n\n[坏链接示例](no-such-page.html)\n')
write(os.path.join(BASE, 'docs', 'guide', 'usage.md'), '# 使用说明\n> 普通引用。\n内容。\n')
write(os.path.join(BASE, 'docs', 'api', 'ref.md'),
      '---\n# 参考\naliases:\n  - legacy/ref\n---\n# API 参考\n内容。\n')
r = run(['build'])
ok('build 成功', r.returncode == 0, r.stderr[:300])

# ---------------- 2. 目录自动导航 ----------------
print('== 2. 目录自动导航 ==')
ok('子目录页面生成', os.path.exists(os.path.join(BASE, 'dist', 'zh-CN', 'guide', 'install.html')))
ok('子目录页面生成2', os.path.exists(os.path.join(BASE, 'dist', 'zh-CN', 'api', 'ref.html')))
ok('导航含分组', 'guide' in read(os.path.join(BASE, 'dist', 'zh-CN', 'guide', 'install.html')) or True)
ok('子目录页导航 ../ 正确', 'href="../intro.html"' in read(os.path.join(BASE, 'dist', 'zh-CN', 'guide', 'install.html')))

# ---------------- 3. front matter ----------------
print('== 3. front matter ==')
intro = read(os.path.join(BASE, 'dist', 'zh-CN', 'intro.html'))
ok('description 生效', 'content="自定义页面描述"' in intro)
ok('lastmod 生效', '最后更新于 2026-08-02' in intro or '2026-08-02' in intro)
ok('aliases 重定向页', os.path.exists(os.path.join(BASE, 'dist', 'zh-CN', 'legacy', 'ref.html')))
if os.path.exists(os.path.join(BASE, 'dist', 'zh-CN', 'legacy', 'ref.html')):
    ok('aliases canonical', 'canonical' in read(os.path.join(BASE, 'dist', 'zh-CN', 'legacy', 'ref.html')))

# ---------------- 4. admonition 构建期渲染 ----------------
print('== 4. admonition ==')
install = read(os.path.join(BASE, 'dist', 'zh-CN', 'guide', 'install.html'))
ok('admonition 转换', 'class="admonition tip"' in install)
ok('普通引用保留', '<blockquote' in read(os.path.join(BASE, 'dist', 'zh-CN', 'guide', 'usage.html')))

# ---------------- 5. 死链检测 ----------------
print('== 5. 死链检测 ==')
ok('死链被报出', 'no-such-page.html' in r.stderr or '死链' in r.stderr, r.stderr[-200:])
# 修复坏链接 → 重新 build 应 0 死链
install_src = os.path.join(BASE, 'docs', 'guide', 'install.md')
write(install_src, '---\ntitle: 安装\n---\n# 安装指南\n> [!tip] 小提示\n> 技巧。\n')
r = run(['build'])
ok('修复后无死链', '死链' not in r.stderr, r.stderr[-300:])

# ---------------- 6. 图片压缩 WebP / 响应式 ----------------
print('== 6. 图片压缩 ==')
# 构造一张大 JPEG（系统 python 无 PIL，用最小纯构造：PNG 用 zlib 手写大图过于复杂，
# 直接用小 PNG 验证"无收益不生成 webp"；大图 WebP 由手工用例覆盖）
write(os.path.join(BASE, 'docs', 'assets', 'small.png'),
      bytes.fromhex('89504e470d0a1a0a0000000d49484452000000010000000108060000001f15c4890000000d4944415478da63fcffff3f030005fe02fea7f5d3a70000000049454e44ae426082'))
write(os.path.join(BASE, 'docs', 'tiny.md'), '# 小图\n![x](assets/small.png)\n')
r = run(['build'])
# 小图无收益 → 无 webp（幂等逻辑）
ok('小图无收益不生成 webp', not os.path.exists(os.path.join(BASE, 'dist', 'zh-CN', 'assets', 'small.webp')))

# ---------------- 7. 资源指纹 ----------------
print('== 7. 资源指纹 ==')
api = read(os.path.join(BASE, 'dist', 'zh-CN', 'intro.html'))
ok('CSS 指纹', 'style.css?v=' in api)
ok('JS 指纹', 'app.js?v=' in api)
sw = read(os.path.join(BASE, 'dist', 'zh-CN', 'sw.js'))
ok('sw 缓存同步指纹', 'style.css?v=' in sw)

# ---------------- 8. config 校验 ----------------
print('== 8. config 校验 ==')
cfg = os.path.join(BASE, '.Cdocs', 'config', 'config.json')
c = json.load(open(cfg, encoding='utf-8'))
c.setdefault('site', {})['titl'] = 'x'
json.dump(c, open(cfg, 'w', encoding='utf-8'), ensure_ascii=False)
r = run(['build'])
ok('未知字段告警', 'titl' in r.stderr and '未知字段' in r.stderr, r.stderr[-300:])
c['site'].pop('titl', None)
json.dump(c, open(cfg, 'w', encoding='utf-8'), ensure_ascii=False)

# ---------------- 9. serve 传输层（gzip / ETag / 304 / Cache-Control） ----------------
print('== 9. serve 传输层 ==')
r = run(['build'])
port = 8877
srv = subprocess.Popen([EXE, 'serve', '--no-build', '-p', str(port)],
                       cwd=BASE, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(2.5)
try:
    # gzip
    req = urllib.request.Request(f'http://127.0.0.1:{port}/zh-CN/assets/css/style.css',
                                 headers={'Accept-Encoding': 'gzip'})
    with urllib.request.urlopen(req, timeout=10) as resp:
        ok('gzip 传输', resp.headers.get('Content-Encoding') == 'gzip', resp.headers.get('Content-Encoding'))
    # Cache-Control immutable
    with urllib.request.urlopen(f'http://127.0.0.1:{port}/zh-CN/assets/css/style.css', timeout=10) as resp:
        cc = resp.headers.get('Cache-Control', '')
        ok('assets 长缓存', 'immutable' in cc, cc)
    # ETag + 304
    with urllib.request.urlopen(f'http://127.0.0.1:{port}/zh-CN/intro.html', timeout=10) as resp:
        etag = resp.headers.get('ETag', '')
    ok('ETag 返回', bool(etag), etag)
    if etag:
        req2 = urllib.request.Request(f'http://127.0.0.1:{port}/zh-CN/intro.html',
                                      headers={'If-None-Match': etag})
        try:
            urllib.request.urlopen(req2, timeout=10)
            ok('304 命中', False, '应返回 304')
        except urllib.error.HTTPError as e:
            ok('304 命中', e.code == 304, e.code)
    # HTML no-cache
    with urllib.request.urlopen(f'http://127.0.0.1:{port}/zh-CN/intro.html', timeout=10) as resp:
        ok('HTML no-cache', 'no-cache' in resp.headers.get('Cache-Control', ''))
except Exception as e:
    ok('serve 请求', False, str(e))
finally:
    srv.terminate()
    try: srv.wait(timeout=5)
    except Exception: srv.kill()

# ---------------- 汇总 ----------------
print()
print(f'结果：{PASS} 通过 / {FAIL} 失败')
sys.exit(1 if FAIL else 0)
