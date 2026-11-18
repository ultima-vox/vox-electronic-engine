import { readFile } from 'node:fs/promises';
import { test, expect } from '@playwright/test';
import type { Locator, Page } from '@playwright/test';

const region = (page: Page, name: string) => page.getByRole('region', { name, exact: true });
const enter = async (field: Locator, value: string) => { await field.fill(value); await field.press('Enter'); };
const peak = (meter: Locator) => expect.poll(async () => Number(await meter.locator('..').getAttribute('data-value')));

test('A: calculated browser contrast and searchable exact DS-00 CSV stay in the Console', async ({page}) => {
  await page.goto('./#system');
  await page.getByRole('button', { name: 'Tokens', exact: true }).click();
  const contrast = page.getByRole('table', { name: 'Browser token contrast' });
  const ratio = contrast.getByRole('row', { name: /Product accent Panel/ }).getByRole('cell').nth(2);
  const drums = await ratio.textContent();
  expect(drums).toMatch(/^\d+\.\d\d:1$/);
  await page.getByRole('group', { name: 'Product accent theme' }).getByRole('button', { name: 'FX', exact: true }).click();
  await expect(ratio).not.toHaveText(drums!);
  await expect(contrast.getByRole('row', { name: /Border Panel/ })).toContainText('Below target');
  await page.getByRole('button', { name: 'Binding', exact: true }).click();
  const audit = region(page, 'DS-00 binding audit');
  const table = audit.getByRole('table', { name: 'DS-00 native binding rows' });
  await expect(audit).toContainText('285 of 285 rows · 1–24');
  await audit.getByRole('button', { name: 'Next rows' }).click();
  await expect(audit).toContainText('285 of 285 rows · 25–48');
  const query = audit.getByRole('searchbox', { name: 'Search DS-00 bindings' });
  await query.fill('drm.p01.level');
  await expect(table.getByRole('cell', { name: 'drm.p01.level', exact: true })).toBeVisible();
  await audit.getByRole('combobox', { name: 'Binding product' }).selectOption('fx');
  await expect(audit).toContainText('0 of 285 rows');
  await expect(table).toContainText('No rows match.');
  const download = page.waitForEvent('download');
  await audit.getByRole('button', { name: 'Download full CSV' }).click();
  const file = await download;
  expect(file.suggestedFilename()).toBe('UI_BINDING_MATRIX.csv');
  expect(await readFile((await file.path())!)).toEqual(await readFile(new URL('../../../docs/ui/vox-music/UI_BINDING_MATRIX.csv', import.meta.url)));
  await page.getByRole('tab', { name: 'VOX Drums', exact: true }).click();
  await expect(table).toBeHidden();
});

test('A: typed Hz/kHz and s/ms entry changes the existing shared controls; invalid units and Escape do not', async ({page}) => {
  await page.goto('./#drums');
  await page.getByRole('combobox', { name: 'Primary editor' }).selectOption('sound');
  const sound = region(page, 'Sound Editor');
  const cutoff = sound.getByRole('textbox', { name: 'Cutoff value (Hz)', exact: true });
  const dial = sound.getByRole('slider', { name: 'Cutoff', exact: true });
  await enter(cutoff, '2,5 kHz');
  await expect(dial).toHaveAttribute('aria-valuenow', '2500');
  await enter(cutoff, '12 ms');
  await expect(dial).toHaveAttribute('aria-valuenow', '2500');
  await cutoff.fill('500 Hz'); await cutoff.press('Escape');
  await expect(dial).toHaveAttribute('aria-valuenow', '2500');
  await enter(sound.getByRole('textbox', { name: 'Decay value (s)', exact: true }), '105 ms');
  await expect(region(page, 'Inspector').getByRole('slider', { name: 'Decay', exact: true })).toHaveAttribute('aria-valuenow', '0.105');
  await page.getByRole('button', { name: 'Undo musical edit' }).click();
  await expect(sound.getByRole('slider', { name: 'Decay', exact: true })).toHaveAttribute('aria-valuenow', '0.38');
});

test('B: pattern tools use real Song edit targets and preserve linkage on no-op and Undo', async ({page}) => {
  await page.goto('./');
  await page.locator('.workspace-menu summary').click();
  await page.getByRole('button', { name: 'Composition', exact: true }).click();
  await page.getByRole('group', { name: 'Drums mode' }).getByRole('button', { name: 'Song', exact: true }).click();
  const song = region(page, 'Song Arranger');
  const bar1 = song.getByRole('button', { name: /^Bar 1:/ });
  const bar2 = song.getByRole('button', { name: /^Bar 2:/ });
  const slot = song.getByRole('combobox', { name: 'Song bar source slot' });
  await slot.selectOption('3');
  await song.getByRole('button', { name: 'Edit This Bar' }).click();
  const step = region(page, 'Step Sequencer');
  const tools = step.getByRole('combobox', { name: 'Pattern tools' });
  await tools.selectOption('reverse');
  await expect(bar1).toHaveAccessibleName(/P04.+Linked/);
  const kick = step.getByRole('button', { name: 'Kick, step 1', exact: true });
  await expect(kick).toHaveAttribute('aria-pressed', 'false');
  await tools.selectOption('invert');
  await expect(bar1).toHaveAccessibleName(/P04.+Unique/);
  await expect(bar2).toHaveAccessibleName(/Linked/);
  await expect(kick).toHaveAttribute('aria-pressed', 'true');
  await page.getByRole('button', { name: 'Undo musical edit' }).click();
  await expect(bar1).toHaveAccessibleName(/P04.+Linked/);
  await expect(kick).toHaveAttribute('aria-pressed', 'false');
  await page.getByRole('button', { name: 'Undo musical edit' }).click();
  await expect(slot).toHaveValue('0'); // No invisible history entry for reverse of an empty bar.
});

type BufferStart = { when: number; offset: number; rate: number; length: number; first: number; loop: boolean; loopStart: number; loopEnd: number; stopped?: number };
declare global { interface Window { voxTestBuffers: BufferStart[]; voxTestGainRamps: number[]; voxTestDelay?: DelayNode } }

test('B: donor sample selection, Normalize, Reverse and bounded Loop feed real Web Audio independently for A/B', async ({page}) => {
  await page.addInitScript(() => {
    const starts: BufferStart[] = [];
    window.voxTestBuffers = starts;
    window.voxTestGainRamps = [];
    const ramp = AudioParam.prototype.linearRampToValueAtTime;
    AudioParam.prototype.linearRampToValueAtTime = function (this: AudioParam, value: number, time: number) {
      window.voxTestGainRamps.push(value);
      return ramp.call(this, value, time);
    };
    const byNode = new WeakMap<AudioBufferSourceNode, BufferStart>();
    const start = AudioBufferSourceNode.prototype.start;
    const stop = AudioBufferSourceNode.prototype.stop;
    AudioBufferSourceNode.prototype.start = function (this: AudioBufferSourceNode, ...args: Parameters<AudioBufferSourceNode['start']>) {
      if (this.buffer) {
        const info = { when: args[0] ?? 0, offset: args[1] ?? 0, rate: this.buffer.sampleRate, length: this.buffer.length, first: this.buffer.getChannelData(0)[0], loop: this.loop, loopStart: this.loopStart, loopEnd: this.loopEnd };
        byNode.set(this, info); starts.push(info);
      }
      return start.apply(this, args);
    };
    AudioBufferSourceNode.prototype.stop = function (this: AudioBufferSourceNode, ...args: Parameters<AudioBufferSourceNode['stop']>) {
      const info = byNode.get(this); if (info) info.stopped = args[0];
      return stop.apply(this, args);
    };
  });
  await page.goto('./');
  const editor = page.getByRole('combobox', { name: 'Primary editor' });
  await editor.selectOption('sample');
  const sample = region(page, 'Sample Editor');
  const library = sample.getByRole('combobox', { name: 'Procedural demo sample' });
  await expect(library.locator('option')).toHaveCount(25);
  await library.selectOption('0:0');
  await expect(sample.locator('.sample-filename')).toHaveText('Kick 01 - Deep');
  await expect(page.getByText('Ready to play', { exact: true })).toBeVisible();
  const players = sample.getByRole('group', { name: 'Sample player' });
  await players.getByRole('button', { name: 'B', exact: true }).click();
  await expect(sample).toContainText('Player B is empty');
  await library.selectOption('0:1');
  await expect(sample.locator('.sample-filename')).toHaveText('Kick 02 - Punch');
  await sample.getByRole('button', { name: 'Normalize peak', exact: true }).click();
  await sample.getByRole('button', { name: 'Reverse', exact: true }).click();
  await sample.getByRole('button', { name: 'Loop · 600 ms', exact: true }).click();
  await enter(sample.getByRole('textbox', { name: 'Sample start value', exact: true }), '.25');
  await editor.selectOption('sound');
  await region(page, 'Sound Editor').getByRole('combobox', { name: 'Pad source' }).selectOption('Sample');
  await editor.selectOption('sample');
  await sample.getByRole('button', { name: 'Audition pad', exact: true }).click();
  await peak(region(page, 'Pad Rack').getByRole('meter', { name: 'Kick local post-fader peak', exact: true })).toBeGreaterThan(.006);
  const [event] = await page.evaluate(() => window.voxTestBuffers);
  expect(event).toMatchObject({ rate: 44100, length: 18522, first: 0, offset: 0, loop: true, loopStart: 0 });
  expect(event.loopEnd).toBeCloseTo(.315, 3);
  expect(event.stopped! - event.when).toBeCloseTo(.604, 4);
  await page.getByRole('button', { name: 'Panic', exact: true }).click();
  await peak(region(page, 'Pad Rack').getByRole('meter', { name: 'Kick local post-fader peak', exact: true })).toBeLessThan(.001);
  const ramps = await page.evaluate(() => window.voxTestGainRamps);
  expect(Math.max(...ramps)).toBeCloseTo(1 / .9, 5); // Actual sample-only Web Audio envelope, not the waveform UI.
  await players.getByRole('button', { name: 'A', exact: true }).click();
  await expect(sample.locator('.sample-filename')).toHaveText('Kick 01 - Deep');
  await expect(sample.getByRole('button', { name: 'Normalize peak', exact: true })).toHaveAttribute('aria-pressed', 'false');
  await expect(sample.getByRole('button', { name: 'Loop · 600 ms', exact: true })).toHaveAttribute('aria-pressed', 'false');
});

test('B: editable local delay division and feedback drive the real send/return, not the UI clock', async ({page}) => {
  await page.addInitScript(() => {
    const create = AudioContext.prototype.createDelay;
    AudioContext.prototype.createDelay = function (this: AudioContext, ...args: Parameters<AudioContext['createDelay']>) {
      const node = create.apply(this, args); window.voxTestDelay = node; return node;
    };
  });
  await page.goto('./');
  const mixer = region(page, 'Mixer Dock');
  await mixer.getByRole('button', { name: 'Expanded', exact: true }).click();
  await mixer.getByText('Delay settings', { exact: true }).click();
  await enter(page.getByRole('spinbutton', { name: 'Browser preview tempo BPM' }), '120');
  const division = mixer.getByRole('combobox', { name: 'Delay division' });
  await division.selectOption('1/8 dotted');
  await expect(mixer.getByText('375 ms · local tempo', { exact: true })).toBeVisible();
  await enter(mixer.getByRole('textbox', { name: 'Delay Feedback value', exact: true }), '0');
  await mixer.getByRole('slider', { name: 'Mixer Kick Delay Send', exact: true }).press('End');
  await mixer.getByRole('combobox', { name: 'Kick mix output', exact: true }).selectOption('1');
  await region(page, 'Pad Rack').locator('[data-pad-audition="0"]').click();
  await expect.poll(() => page.evaluate(() => window.voxTestDelay?.delayTime.value)).toBeCloseTo(.375, 2);
  await peak(mixer.getByRole('meter', { name: 'Delay local return peak', exact: true })).toBeGreaterThan(.004);
  await expect(mixer.getByText(/FX returns · to Main/)).toBeVisible();
});
