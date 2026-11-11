# -*- coding: utf-8 -*-
"""把 2026-11-12-geology-pipeline 目录下的 md 合成单页 HTML（手机阅读优先）。"""
import os, re, html, io

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "docs", "superpowers", "specs", "2026-11-12-geology-pipeline")
OUT = os.path.join(ROOT, "Saved", "Docs", "geology-pipeline.html")

ORDER = [
    ("README.md", "00 · 总览"),
    ("00-pipeline.md", "01 · 流水线总表"),
    ("01-sphere.md", "02 · 阶段1 球体基底"),
    ("02-plates.md", "03 · 阶段2 板块与陆块"),
    ("03-terrain.md", "04 · 阶段3 地形"),
    ("04-magmatism.md", "05 · 阶段4 岩浆"),
    ("05-climate.md", "06 · 阶段5 气候"),
    ("06-erosion.md", "07 · 阶段6 侵蚀"),
    ("07-weathering.md", "08 · 阶段7 风化"),
    ("10-density-field.md", "09 · 密度场契约"),
    ("11-material-field.md", "10 · 材质场契约"),
    ("12-primitives.md", "11 · 形态原语"),
    ("rules.md", "12 · 撒矿规则"),
    ("20-architecture.md", "13 · 代码架构"),
    ("minerals/README.md", "14 · 矿物索引"),
    ("minerals/sulfur.md", "15 · 硫磺"),
    ("minerals/porphyry-copper.md", "16 · 斑岩铜"),
    ("minerals/epithermal-au-ag.md", "17 · 浅成低温金银"),
    ("minerals/alluvial-gold.md", "18 · 砂金"),
    ("open-questions.md", "19 · 待拍板"),
]


def inline(t):
    t = html.escape(t)
    t = re.sub(r'`([^`]+)`', r'<code>\1</code>', t)
    t = re.sub(r'\*\*([^*]+)\*\*', r'<strong>\1</strong>', t)
    t = re.sub(r'(?<![*\w])\*([^*\n]+)\*(?!\w)', r'<em>\1</em>', t)
    t = re.sub(r'\[([^\]]+)\]\(([^)]+)\)', r'<a href="\2">\1</a>', t)
    t = re.sub(r'&lt;(https?://[^&]+?)&gt;', r'<a href="\1">\1</a>', t)
    return t


def split_row(line):
    line = line.strip()
    if line.startswith('|'):
        line = line[1:]
    if line.endswith('|') and not line.endswith('\\|'):
        line = line[:-1]
    cells = [c.replace('\\|', '|').strip() for c in re.split(r'(?<!\\)\|', line)]
    return cells


def render(md):
    lines = md.replace('\r\n', '\n').split('\n')
    out, i, n = [], 0, len(lines)
    while i < n:
        ln = lines[i]
        # 代码块
        if ln.strip().startswith('```'):
            buf = []
            i += 1
            while i < n and not lines[i].strip().startswith('```'):
                buf.append(lines[i]); i += 1
            i += 1
            out.append('<pre><code>' + html.escape('\n'.join(buf)) + '</code></pre>')
            continue
        # 表格
        if ln.strip().startswith('|') and i + 1 < n and re.match(r'^\s*\|[\s:|-]+\|\s*$', lines[i + 1]):
            head = split_row(ln)
            i += 2
            body = []
            while i < n and lines[i].strip().startswith('|'):
                body.append(split_row(lines[i])); i += 1
            t = ['<div class="tw"><table><thead><tr>']
            for h in head:
                t.append('<th>' + inline(h) + '</th>')
            t.append('</tr></thead><tbody>')
            for r in body:
                t.append('<tr>' + ''.join('<td>' + inline(c) + '</td>' for c in r) + '</tr>')
            t.append('</tbody></table></div>')
            out.append(''.join(t))
            continue
        # 标题
        m = re.match(r'^(#{1,6})\s+(.*)$', ln)
        if m:
            lvl = len(m.group(1))
            txt = m.group(2).strip()
            aid = 'h%d-%d' % (lvl, i)
            out.append('<h%d id="%s">%s</h%d>' % (lvl, aid, inline(txt), lvl))
            i += 1
            continue
        # 分隔线
        if re.match(r'^\s*(---|\*\*\*|___)\s*$', ln):
            out.append('<hr/>'); i += 1; continue
        # 引用
        if ln.strip().startswith('>'):
            buf = []
            while i < n and lines[i].strip().startswith('>'):
                buf.append(re.sub(r'^\s*>\s?', '', lines[i])); i += 1
            out.append('<blockquote>' + render('\n'.join(buf)) + '</blockquote>')
            continue
        # 列表
        if re.match(r'^\s*([-*+]|\d+\.)\s+', ln):
            ordered = bool(re.match(r'^\s*\d+\.\s+', ln))
            tag = 'ol' if ordered else 'ul'
            items = []
            while i < n:
                cur = lines[i]
                if re.match(r'^\s*([-*+]|\d+\.)\s+', cur):
                    items.append([re.sub(r'^\s*([-*+]|\d+\.)\s+', '', cur)]); i += 1
                elif cur.strip() == '':
                    # 空行后若仍是列表项则继续
                    j = i + 1
                    while j < n and lines[j].strip() == '':
                        j += 1
                    if j < n and re.match(r'^\s*([-*+]|\d+\.)\s+', lines[j]):
                        i = j; continue
                    break
                elif re.match(r'^\s{2,}\S', cur) and items:
                    items[-1].append(cur.strip()); i += 1
                else:
                    break
            body = ''.join('<li>' + inline(' '.join(it)) + '</li>' for it in items)
            out.append('<%s>%s</%s>' % (tag, body, tag))
            continue
        # 空行
        if ln.strip() == '':
            i += 1; continue
        # 段落
        buf = []
        while i < n and lines[i].strip() != '' and not re.match(r'^(#{1,6})\s', lines[i]) \
                and not lines[i].strip().startswith('```') and not lines[i].strip().startswith('|') \
                and not re.match(r'^\s*([-*+]|\d+\.)\s+', lines[i]) and not lines[i].strip().startswith('>') \
                and not re.match(r'^\s*(---|\*\*\*|___)\s*$', lines[i]):
            buf.append(lines[i].strip()); i += 1
        if buf:
            out.append('<p>' + inline('\n'.join(buf)) + '</p>')
    return '\n'.join(out)


def main():
    sections, toc = [], []
    total_lines = 0
    for path, label in ORDER:
        fp = os.path.join(SRC, path.replace('/', os.sep))
        with io.open(fp, encoding='utf-8') as f:
            md = f.read()
        total_lines += md.count('\n') + (0 if md.endswith('\n') else 1)
        body = render(md)
        sid = 'sec-' + re.sub(r'[^a-z0-9]+', '-', path.lower())
        sections.append('<section id="%s" data-label="%s">%s</section>' % (sid, html.escape(label), body))
        toc.append('<a href="#%s">%s</a>' % (sid, html.escape(label)))

    total = sum(len(s) for s in sections)
    doc = """<!DOCTYPE html>
<html lang="zh-CN"><head><meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"/>
<title>地质生成流水线 · StellarEra</title>
<style>
:root{--bg:#f7f6f4;--panel:#fffefb;--ink:#1c1a17;--dim:#6b665e;--line:#e2ded6;--accent:#8a5a2b;--code:#f1ede6;}
*{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--ink);
 font:16px/1.78 -apple-system,BlinkMacSystemFont,"PingFang SC","Hiragino Sans GB","Microsoft YaHei",sans-serif;
 padding-bottom:60px;}
header{position:sticky;top:0;z-index:20;background:rgba(247,246,244,.94);backdrop-filter:blur(12px);
 border-bottom:1px solid var(--line);padding:10px 16px;display:flex;align-items:center;gap:10px}
header .t{font-size:15px;font-weight:700;letter-spacing:.02em;flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
header button{flex:none;border:1px solid var(--line);background:var(--panel);color:var(--ink);
 font-size:13px;padding:7px 12px;border-radius:9px;font-weight:600}
#nav{display:none;position:fixed;inset:0;top:0;background:var(--bg);z-index:30;overflow-y:auto;padding:14px 16px 60px}
#nav.on{display:block}
#nav h4{margin:0 0 12px;font-size:13px;color:var(--dim);font-weight:600;letter-spacing:.06em;text-transform:uppercase}
#nav a{display:block;padding:13px 12px;color:var(--ink);text-decoration:none;font-size:15px;
 border-bottom:1px solid var(--line);border-radius:8px}
#nav a:active{background:#efece5}
main{max-width:800px;margin:0 auto;padding:22px 16px 40px}
section{background:var(--panel);border:1px solid var(--line);border-radius:16px;padding:20px 18px;margin:0 0 18px;
 box-shadow:0 1px 2px rgba(0,0,0,.03)}
section::before{content:attr(data-label);display:block;font-size:12px;font-weight:700;letter-spacing:.08em;
 color:var(--accent);text-transform:uppercase;margin-bottom:14px;padding-bottom:10px;border-bottom:1px dashed var(--line)}
h1{font-size:22px;line-height:1.4;margin:.9em 0 .5em;font-weight:800;letter-spacing:-.01em}
h2{font-size:18.5px;margin:1.6em 0 .55em;font-weight:750;letter-spacing:-.01em}
h3{font-size:16.5px;margin:1.35em 0 .45em;font-weight:700;color:#2e2a24}
h4,h5,h6{font-size:15px;margin:1.2em 0 .4em;font-weight:700;color:var(--dim)}
section>:first-child{margin-top:0}
section h1:first-of-type{font-size:20px;margin-top:0}
p{margin:.72em 0}
ul,ol{margin:.7em 0;padding-left:1.35em}
li{margin:.32em 0}
li>ul,li>ol{margin:.25em 0}
strong{font-weight:700;color:#12100e}
code{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:.87em;background:var(--code);
 padding:.13em .38em;border-radius:5px;word-break:break-word}
pre{background:#23201c;color:#e9e4dc;padding:14px;border-radius:11px;overflow-x:auto;margin:1em 0;
 font-size:12.6px;line-height:1.62;-webkit-overflow-scrolling:touch}
pre code{background:none;color:inherit;padding:0;font-size:inherit;white-space:pre}
.tw{overflow-x:auto;margin:1em 0;border:1px solid var(--line);border-radius:11px;-webkit-overflow-scrolling:touch}
table{border-collapse:collapse;width:100%;font-size:13.6px;min-width:100%}
th,td{padding:9px 11px;text-align:left;border-bottom:1px solid var(--line);vertical-align:top}
th{background:#f4f1ea;font-weight:700;white-space:nowrap;position:sticky;top:0}
tbody tr:last-child td{border-bottom:none}
td code{font-size:.85em}
blockquote{margin:1em 0;padding:2px 0 2px 14px;border-left:3px solid var(--accent);color:#4a453d;background:transparent}
hr{border:none;border-top:1px solid var(--line);margin:1.6em 0}
a{color:#7a4a1e;text-decoration:none;border-bottom:1px solid rgba(122,74,30,.3)}
#top{position:fixed;right:14px;bottom:18px;z-index:25;width:46px;height:46px;border-radius:50%;
 border:1px solid var(--line);background:var(--panel);color:var(--dim);font-size:19px;
 box-shadow:0 4px 14px rgba(0,0,0,.1);display:none;align-items:center;justify-content:center}
#top.on{display:flex}
</style></head><body>
<header><div class="t">地质生成流水线 · StellarEra</div><button id="btn">目录</button></header>
<nav id="nav"><h4>19 份文档 · 共 @@LINES@@ 行</h4>@@TOC@@</nav>
<main>@@SEC@@</main>
<a id="top" href="#">↑</a>
<script>
var nav=document.getElementById('nav'),btn=document.getElementById('btn'),top=document.getElementById('top');
btn.onclick=function(){nav.classList.toggle('on');btn.textContent=nav.classList.contains('on')?'关闭':'目录';};
nav.onclick=function(e){if(e.target.tagName==='A'){nav.classList.remove('on');btn.textContent='目录';}};
window.addEventListener('scroll',function(){top.classList.toggle('on',window.scrollY>600);});
</script>
</body></html>"""
    doc = doc.replace('@@LINES@@', str(total_lines)).replace('@@TOC@@', ''.join(toc)).replace('@@SEC@@', '\n'.join(sections))

    with io.open(OUT, 'w', encoding='utf-8') as f:
        f.write(doc)
    print('OK ->', OUT, len(doc), 'bytes')


if __name__ == '__main__':
    main()
