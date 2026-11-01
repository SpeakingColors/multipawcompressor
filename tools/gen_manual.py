#!/usr/bin/env python3
"""Build docs/manual.html from tools/layout.json (control names and info-bubble texts of the mockup),
tools/params.json (ranges, defaults) and the two window screenshots. CI turns it into a PDF.
Run from the repo root after refreshing docs/screenshot.png (page 1) and docs/screenshot2.png (page 2)."""
import json, html, re
L = json.load(open('tools/layout.json'))
P = {p['id']: p for p in json.load(open('tools/params.json'))}
e = html.escape
def rng(pid):
    p = P.get(pid)
    if not p: return ''
    f = lambda v: ('%g' % v)
    return '%s to %s' % (f(p['min']), f(p['max']))
def dfl(pid):
    p = P.get(pid)
    return '%g' % p['def'] if p else ''
def rows(page):
    out, seen = [], set()
    for c in page['ctl']:
        pid = c['pid']
        m = re.match(r'b(\d)\.(.*)', pid)
        if m and m.group(1) != '0': continue            # one band stands for all four
        name = c.get('label') or c.get('text') or pid
        key = (name, c['tip'])
        if key in seen: continue
        seen.add(key)
        out.append((name, c['tip'], rng(pid), dfl(pid)))
    for o in page['other']:
        if o.get('band', -1) > 0: continue
        name = o.get('text') or o.get('id') or ''
        if not name or not o.get('tip'): continue
        key = (name, o['tip'])
        if key in seen: continue
        seen.add(key); out.append((name, o['tip'], '', ''))
    return out
def table(page):
    return '<table><tr><th>Control</th><th>What it does</th><th>Range</th><th>Default</th></tr>' + ''.join(
        '<tr><th>%s</th><td>%s</td><td>%s</td><td>%s</td></tr>' % (e(n), e(t), e(r), e(d)) for n, t, r, d in rows(page)) + '</table>'
page = '''<!doctype html>
<html lang="en"><head><meta charset="utf-8"><title>MultiPaw Compressor manual</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
:root{--bg:#0b0b0d;--card:#16171a;--line:#33363d;--ink:#e8e9ee;--dim:#a9adb8;--teal:#02EBBA;--pink:#FF7BB1}
@media print{:root{--bg:#fff;--card:#f4f4f6;--line:#c9cbd3;--ink:#16171a;--dim:#4a4d55;--teal:#007a62;--pink:#b0114f}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:16px/1.5 "Barlow Condensed","Arial Narrow",Arial,sans-serif;padding:24px max(16px,env(safe-area-inset-left,0px))}
main{max-width:980px;margin:0 auto}
h1{font-size:40px;margin:0 0 4px;font-style:italic}
h2{font-size:26px;margin:36px 0 8px;color:var(--teal);letter-spacing:1px;text-transform:uppercase;break-after:avoid}
a{color:var(--pink)}
img{max-width:100%;height:auto;border:1px solid var(--line);border-radius:6px;display:block;margin:8px 0}
table{border-collapse:collapse;width:100%;font-size:15px;display:block;overflow-x:auto}
td,th{border-bottom:1px solid var(--line);padding:5px 8px;text-align:left;vertical-align:top}
th{white-space:nowrap}
code,kbd{font-family:"JetBrains Mono",Consolas,monospace;font-size:13px;background:var(--card);border:1px solid var(--line);border-radius:3px;padding:0 4px}
.dim{color:var(--dim)}
@media print{body{padding:0}tr{break-inside:avoid}img{break-inside:avoid}}
</style></head><body><main>
<h1>MultiPaw Compressor</h1>
<p class="dim">Manual for version @VER@. Studio 87. Free and open source (AGPL-3.0-or-later). Collects nothing, never uses the network.</p>
<p>A four-band compressor with linear-phase crossovers, a per-band limiter and dynamic pattern curves (DPC), plus a two-band sidechain page for pumping and ducking.</p>

<h2>Quick start</h2>
<ol>
<li>Put MultiPaw on a track. Choose how many bands (1 to 4) and drag the crossover lines in the big display.</li>
<li>Pull a band's <b>Threshold</b> slider left until the gain reduction bar moves. Ratio, Attack and Release shape the feel.</li>
<li>Compare with <b>Bypass</b> (B). The dry signal is delayed to match, so level jumps are only from the processing.</li>
<li>For parallel compression lower the band or master <b>Mix</b>. Press <b>P</b> to flip to the Sidechain page.</li>
</ol>

<h2>Page 1: Compressor</h2>
<img src="screenshot.png" alt="MultiPaw page 1">
@T1@

<h2>Page 2: Sidechain</h2>
<img src="screenshot2.png" alt="MultiPaw page 2">
@T2@

<h2>Latency</h2>
<p>With linear-phase crossovers on, the plugin reports its latency to the host; it depends on the Filter slope setting (shown under the Pre meter). Turn linear phase off in System for the lowest latency with classic Linkwitz-Riley crossovers. The limiter lookahead adds to it.</p>

<h2>Keyboard and mouse</h2>
<table><tr><td><kbd>B</kbd></td><td>Bypass on or off</td></tr><tr><td><kbd>P</kbd></td><td>Flip between the compressor and sidechain pages</td></tr><tr><td>Knobs</td><td>Drag up or down, mouse wheel. Hold Shift for fine steps. Double-click to reset.</td></tr><tr><td>Window</td><td>Drag a corner; it keeps its proportions.</td></tr></table>

<h2>Install and uninstall</h2>
<ul>
<li><b>Linux:</b> unpack the zip, run <code>install.sh</code> (copies the VST3 to <code>~/.vst3</code>). <code>uninstall.sh</code> removes it.</li>
<li><b>Windows:</b> run the installer. It adds an uninstaller. Close your DAW first.</li>
<li><b>macOS:</b> open the package. The build is not notarised, so macOS may ask you to allow it in System Settings, Privacy and Security.</li>
</ul>
<p>After installing, rescan plugins in your host.</p>

<h2>Licence and thanks</h2>
<p>AGPL-3.0-or-later. Built with JUCE (AGPLv3). Fonts: Barlow Condensed and JetBrains Mono, SIL Open Font Licence 1.1. Source: <a href="https://github.com/SpeakingColors/multipawcompressor">github.com/SpeakingColors/multipawcompressor</a>.</p>
</main></body></html>
'''
open('docs/manual.html', 'w').write(page.replace('@VER@', '0.1.0').replace('@T1@', table(L[0])).replace('@T2@', table(L[1])))
print('manual ok', len(rows(L[0])), len(rows(L[1])))
