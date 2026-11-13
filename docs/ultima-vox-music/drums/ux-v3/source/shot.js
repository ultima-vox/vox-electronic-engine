// Renders every html/*.html to png/*.png at 2x and reports clipped panel content.
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const dir = path.join(__dirname, 'html'), out = path.join(__dirname, 'png');
  fs.mkdirSync(out, { recursive: true });
  const only = process.argv[2];
  const browser = await chromium.launch();
  for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.html') && (!only || f.includes(only))).sort()) {
    const page = await browser.newPage({ viewport: { width: 1700, height: 400 }, deviceScaleFactor: 2 });
    await page.goto('file://' + path.join(dir, f));
    const w = await page.evaluate(() => document.body.scrollWidth);
    await page.setViewportSize({ width: w, height: 400 });
    const clipped = await page.evaluate(() => {
      const res = [];
      document.querySelectorAll('.pb, .ph').forEach(el => {
        if (el.scrollHeight > el.clientHeight + 2 || el.scrollWidth > el.clientWidth + 2) {
          const p = el.closest('.pnl'); const t = p ? (p.querySelector('.ttl, .tabs i.on') || {}).textContent : '?';
          res.push(`${el.className}:${t}:${el.scrollWidth - el.clientWidth},${el.scrollHeight - el.clientHeight}`);
        }
      });
      document.querySelectorAll('.app *').forEach(el => {
        if (el.matches('.pb, .ph, .pnl, .app')) return;
        const cs = getComputedStyle(el);
        if (cs.overflow !== 'hidden' && cs.overflowX !== 'hidden') return;
        if (el.scrollWidth > el.clientWidth + 1 || el.scrollHeight > el.clientHeight + 2) {
          const t = (el.textContent || '').trim().replace(/\s+/g, ' ').slice(0, 28);
          res.push(`inner:${t}:${el.scrollWidth - el.clientWidth},${el.scrollHeight - el.clientHeight}`);
        }
      });
      return res;
    });
    await page.screenshot({ path: path.join(out, f.replace('.html', '.png')), fullPage: true });
    if (f === '01-beatmaking.html') {
      const p1 = await browser.newPage({ viewport: { width: w, height: 400 }, deviceScaleFactor: 1 });
      await p1.goto('file://' + path.join(dir, f));
      await p1.screenshot({ path: path.join(out, '01-beatmaking@1x.png'), fullPage: true });
      await p1.close();
    }
    console.log(f, clipped.length ? 'CLIPPED ' + clipped.join(' | ') : 'ok');
    await page.close();
  }
  await browser.close();
})();
