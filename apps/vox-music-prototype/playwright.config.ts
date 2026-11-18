import { defineConfig, devices } from '@playwright/test';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { createRequire } from 'node:module';
import { existsSync, mkdirSync, readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { brotliDecompressSync } from 'node:zlib';

const bundled = process.env.VOX_BUNDLED_CHROMIUM === '1';
const baseURL = process.env.VOX_BASE_URL ?? 'http://127.0.0.1:4180';
const capture = process.env.VOX_VISUAL_CAPTURE === '1';
async function chromiumOptions() {
  if (!bundled) return {};
  if (process.platform !== 'linux' || process.arch !== 'x64') throw new Error('The npm Chromium fallback requires Linux x64. Use the regular Playwright browser installation.');
  const chromium = (await import('@sparticuz/chromium')).default;
  const require = createRequire(import.meta.url);
  const bin = join(dirname(require.resolve('@sparticuz/chromium')), '..', 'bin');
  const scratch = join(tmpdir(), 'vox-chromium-149');
  mkdirSync(scratch, { recursive: true });
  const lib = join(scratch,'lib');
  if (!existsSync(join(lib,'libnspr4.so'))) execFileSync('tar', ['-xf','-','-C',scratch], { input: brotliDecompressSync(readFileSync(join(bin,'al2023.tar.br'))) });
  return {
    executablePath: await chromium.executablePath(),
    args: ['--no-sandbox','--disable-gpu'],
    env: { ...process.env, LD_LIBRARY_PATH: [lib, process.env.LD_LIBRARY_PATH].filter(Boolean).join(':') },
  };
}
export default defineConfig({
  testDir: './e2e',
  timeout: 32000,
  expect: { timeout: 5000 },
  workers: 2,
  reporter: 'list',
  use: { ...devices['Desktop Chrome'], browserName: 'chromium', baseURL, viewport: { width: 1440, height: 900 }, trace: capture ? 'retain-on-failure' : 'off', screenshot: capture ? 'only-on-failure' : 'off', launchOptions: await chromiumOptions() },
  webServer: process.env.VOX_BASE_URL ? undefined : { command: 'npm run dev -- --host 127.0.0.1 --port 4180 --strictPort', url: baseURL, timeout: 24000, reuseExistingServer: false },
});
