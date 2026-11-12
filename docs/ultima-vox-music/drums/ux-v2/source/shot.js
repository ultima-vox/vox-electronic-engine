const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
(async () => {
  const dir = path.join(__dirname, 'html'), out = path.join(__dirname, 'png');
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch();
  for (const f of fs.readdirSync(dir).filter(f => f.endsWith('.html')).sort()) {
    const compact = f.includes('1280x720');
    const page = await browser.newPage({ viewport: compact ? { width: 1280, height: 720 } : { width: 1440, height: 900 }, deviceScaleFactor: 2 });
    await page.goto('file://' + path.join(dir, f));
    const overflow = await page.evaluate(() => { const a = document.querySelector('.app'); return [a.scrollWidth - a.clientWidth, a.scrollHeight - a.clientHeight]; });
    await page.screenshot({ path: path.join(out, f.replace('.html', '.png')) });
    console.log(f, 'overflow', overflow.join(','));
    await page.close();
  }
  await browser.close();
})();
