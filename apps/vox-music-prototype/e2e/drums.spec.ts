import { readFile } from 'node:fs/promises';
import { test, expect } from '@playwright/test';
import type { Locator, Page } from '@playwright/test';
import { testWav } from '../src/shared-test-utils/fixtures';

const panel = (page: Page, name: string) => page.getByRole('region', { name, exact: true });
const grid = (page: Page) => page.getByRole('group',{name:'16-track step sequencer'});
const hit = (page: Page, name = 'Kick, step 1') => grid(page).getByRole('button',{name,exact:true});
const levelAt = (node: Locator) => expect.poll(async () => Number(await node.getAttribute('data-value')));
const setValue = async (field: Locator, value: string) => { await field.fill(value); await field.press('Enter'); };
const bounded = (value: string | null) => Number(value ?? 0);

async function showPanel(page: Page, option: string) {
  await page.locator('.panel-menu summary').click();
  await page.getByRole('checkbox',{name:new RegExp(option,'i')}).check();
  await page.locator('.panel-menu summary').click();
}
async function workspace(page: Page, name: string) {
  await page.locator('.workspace-menu summary').click();
  await page.getByRole('button',{name,exact:true}).click();
}

test.beforeEach(async ({ page }) => { await page.goto('./'); await expect(page.getByRole('heading',{name:'VOX Drums',exact:true})).toBeVisible(); });

test('Grid/List selection is non-audible, independently of pad audition and Inspector pin', async ({page}) => {
  const rack = panel(page,'Pad Rack'); const inspector = panel(page,'Inspector');
  await expect(rack.getByRole('button',{ name:/^Audition / })).toHaveCount(16);
  await rack.getByRole('button',{ name:/^Select(?:ed)? Snare, pad 2$/i }).focus(); await page.keyboard.press('Enter');
  await expect(inspector.getByRole('heading',{name:'Snare',exact:true})).toBeVisible();
  await expect(rack).toContainText('Last hit · —');
  await expect(page.getByText('Ready to play',{exact:true})).toBeVisible();
  await inspector.getByRole('button',{name:'Pin Inspector'}).click();
  await rack.getByRole('button',{ name:'List', exact:true }).click();
  await expect(rack.locator('.pad-list-row')).toHaveCount(16);
  await expect(rack.getByRole('button',{name:/^Select(?:ed)? Snare, pad 2$/i})).toHaveAttribute('aria-pressed','true');
  await rack.getByRole('button',{name:/^Select(?:ed)? Clap, pad 3$/i}).click();
  await rack.getByRole('button',{name:/^Audition .*Kick/}).click();
  await expect(rack).toContainText('Last hit · Pad 01 / velocity 100');
  await expect(inspector.getByRole('heading',{name:'Snare',exact:true})).toBeVisible();
  await expect(rack.getByRole('button',{name:/^Select(?:ed)? Clap, pad 3$/i})).toHaveAttribute('aria-pressed','true');
  await inspector.getByRole('button',{name:'Unpin Inspector'}).click();
  await expect(inspector.getByRole('heading',{name:'Clap',exact:true})).toBeVisible();
});

test('All sixteen local voices produce measured signal; sequence preview and Panic work', async ({page}) => {
  const rack = panel(page,'Pad Rack');
  const meter = rack.locator('.rack-readback .meter');
  // Capture actual meter frames, including short attacks that can end between Playwright polls.
  await meter.evaluate(el => { const observer = new MutationObserver(() => {
    const peak = Math.max(Number(el.getAttribute('data-test-peak')),Number(el.getAttribute('data-value'))); el.setAttribute('data-test-peak',String(peak));
  }); observer.observe(el,{attributes:true,attributeFilter:['data-value']}); });
  for (let pad = 0; pad < 16; pad++) await test.step(`pad ${pad + 1}: audition and measured peak`, async () => {
    await rack.locator(`[data-pad-select="${pad}"]`).click();
    await meter.evaluate(el => el.setAttribute('data-test-peak','0'));
    await rack.locator(`[data-pad-audition="${pad}"]`).click();
    await expect.poll(async () => Number(await meter.getAttribute('data-test-peak'))).toBeGreaterThan(.003);
  });
  await page.getByRole('button',{name:'Play preview',exact:true}).click();
  await expect(page.getByRole('button',{name:'Stop preview',exact:true})).toBeVisible();
  await expect(page.locator('.step-ruler [data-playing=true]')).toBeVisible();
  await expect(page.locator('.step-ruler [data-playing=true]')).not.toHaveText('01');
  await page.getByRole('button',{name:'Panic',exact:true}).click();
  await expect(page.getByRole('button',{name:'Play preview',exact:true})).toBeVisible();
  await expect(page.locator('.step-ruler [data-playing=true]')).toHaveCount(0);
});

test('Unified rotary/fader/numeric bindings, exact reset, fine drag, keyboard focus and undo', async ({page}) => {
  const inspector = panel(page,'Inspector');
  const knob = inspector.getByRole('slider',{name:'Level',exact:true});
  const input = inspector.getByRole('textbox',{name:'Level value',exact:true});
  const fader = panel(page,'Mixer Dock').getByRole('slider',{name:'Mixer Kick Level',exact:true});
  await setValue(input,'0.4');
  await expect(knob).toHaveAttribute('aria-valuenow','0.4'); await expect(fader).toHaveAttribute('aria-valuenow','0.4');
  await inspector.getByRole('button',{name:'Open Sound Editor'}).click();
  await expect(panel(page,'Sound Editor').getByRole('slider',{name:'Level',exact:true})).toHaveAttribute('aria-valuenow','0.4');
  await page.getByRole('button',{name:'Undo musical edit'}).click();
  await expect(fader).toHaveAttribute('aria-valuenow','0.85');
  await page.getByRole('button',{name:'Redo musical edit'}).click();
  await expect(fader).toHaveAttribute('aria-valuenow','0.4');
  await knob.dblclick(); await expect(knob).toHaveAttribute('aria-valuenow','0.85');
  await setValue(input,'0.4');
  const b = (await knob.boundingBox())!;
  const drag = async (fine: boolean) => {
    await page.mouse.move(b.x+b.width/2,b.y+b.height/2);
    if (fine) await page.keyboard.down('Shift');
    await page.mouse.down(); await page.mouse.move(b.x+b.width/2,b.y+b.height/2-32,{steps:6}); await page.mouse.up();
    if (fine) await page.keyboard.up('Shift');
    return bounded(await knob.getAttribute('aria-valuenow'));
  };
  const coarse = await drag(false); await expect(fader).toHaveAttribute('aria-valuenow',String(coarse));
  expect(coarse).toBeGreaterThan(.58);
  await page.getByRole('button',{name:'Undo musical edit'}).click(); await expect(knob).toHaveAttribute('aria-valuenow','0.4');
  const fine = await drag(true); expect(fine).toBeGreaterThan(.41); expect(fine).toBeLessThan(coarse-.1);
  await knob.press('Tab'); await expect(input).toBeFocused();
  await page.keyboard.press('Shift+Tab'); await expect(knob).toBeFocused();
  expect(await knob.evaluate(n => getComputedStyle(n).outlineStyle)).toBe('solid');
  await knob.press('Home'); await expect(knob).toHaveAttribute('aria-valuenow','0');
  await knob.press('ArrowUp'); await expect(knob).toHaveAttribute('aria-valuenow','0.02');
});

test('Releasing Shift during a held arrow gesture leaves one Undo entry', async ({page}) => {
  const knob = panel(page,'Inspector').getByRole('slider',{name:'Level',exact:true});
  const undo = page.getByRole('button',{name:'Undo musical edit',exact:true});
  const redo = page.getByRole('button',{name:'Redo musical edit',exact:true});
  await knob.focus();
  await expect(knob).toHaveAttribute('aria-valuenow','0.85');
  await expect(undo).toBeDisabled();

  await page.keyboard.down('Shift');
  await page.keyboard.down('ArrowUp');
  await page.keyboard.down('ArrowUp'); // Repeat while held, at fine step size.
  await expect(knob).toHaveAttribute('aria-valuenow','0.852');
  await page.keyboard.up('Shift');
  await expect(undo).toBeDisabled(); // A modifier release must not commit the gesture.
  await page.keyboard.down('ArrowUp'); // Same held arrow, now with the regular step size.
  await expect(knob).toHaveAttribute('aria-valuenow','0.872');
  await page.keyboard.up('ArrowUp');
  await expect(undo).toBeEnabled();

  await undo.click();
  await expect(knob).toHaveAttribute('aria-valuenow','0.85');
  await expect(undo).toBeDisabled();
  await redo.click();
  await expect(knob).toHaveAttribute('aria-valuenow','0.872');
});

test('64 steps, keyboard selection, drag on velocity lane and shared step edit history', async ({page}) => {
  const inspector = panel(page,'Inspector');
  await page.getByRole('combobox',{name:'Pattern slot',exact:true}).selectOption('3');
  await page.getByRole('group',{name:'Steps per bar'}).getByRole('button',{name:'64'}).click();
  await expect(grid(page).locator('.step-hit')).toHaveCount(1024);
  await hit(page).click(); await expect(hit(page)).toHaveAttribute('aria-pressed','true');
  await hit(page).press('ArrowRight'); await expect(hit(page,'Kick, step 2')).toBeFocused();
  await page.keyboard.press('Space');
  await hit(page).click({button:'right'}); await hit(page,'Kick, step 2').click({modifiers:['Shift']});
  await expect(inspector).toContainText('2 steps');
  await setValue(inspector.getByRole('textbox',{name:'Velocity value',exact:true}),'107');
  await expect(grid(page).getByRole('button',{name:'Velocity, step 1: 107',exact:true})).toHaveCount(1);
  await expect(grid(page).getByRole('button',{name:'Velocity, step 2: 107',exact:true})).toHaveCount(1);
  const lane = grid(page).getByRole('button',{name:'Velocity, step 1: 107',exact:true});
  await lane.scrollIntoViewIfNeeded(); const box = (await lane.boundingBox())!;
  await page.mouse.move(box.x+box.width/2,box.y+box.height/2); await page.mouse.down();
  await page.mouse.move(box.x+box.width/2,box.y+box.height/2+12,{steps:3}); await page.mouse.up();
  await expect.poll(async () => bounded(await inspector.getByRole('slider',{name:'Velocity',exact:true}).getAttribute('aria-valuenow'))).toBeLessThan(100);
  await page.getByRole('button',{name:'Undo musical edit'}).click();
  await expect(grid(page).getByRole('button',{name:'Velocity, step 1: 107',exact:true})).toHaveCount(1);
  await page.getByRole('group',{name:'Steps per bar'}).getByRole('button',{name:'32'}).click(); await expect(grid(page).locator('.step-hit')).toHaveCount(512);
  await page.getByRole('group',{name:'Steps per bar'}).getByRole('button',{name:'16'}).click(); await expect(grid(page).locator('.step-hit')).toHaveCount(256);
});

test('Song clones only on a bar edit, relinks on undo, and follows source on other bars', async ({page}) => {
  await workspace(page,'Composition');
  await page.getByRole('group',{name:'Drums mode'}).getByRole('button',{name:'Song',exact:true}).click();
  const song = panel(page,'Song Arranger');
  const bar = (n: number) => song.getByRole('button',{name:new RegExp(`^Bar ${n}:`)});
  await song.getByRole('button',{name:'Edit This Bar'}).click();
  await expect(bar(1)).toHaveAccessibleName(/Linked/);
  await hit(page).click(); await expect(bar(1)).toHaveAccessibleName(/Unique/);
  await expect(bar(2)).toHaveAccessibleName(/Linked/);
  await bar(2).click(); await expect(hit(page)).toHaveAttribute('aria-pressed','true');
  await bar(1).click(); await expect(hit(page)).toHaveAttribute('aria-pressed','false');
  await page.getByRole('button',{name:'Undo musical edit'}).click();
  await expect(bar(1)).toHaveAccessibleName(/Linked/); await expect(hit(page)).toHaveAttribute('aria-pressed','true');
  await page.getByRole('button',{name:'Redo musical edit'}).click(); await expect(bar(1)).toHaveAccessibleName(/Unique/);
  await song.getByRole('button',{name:'Edit Source Pattern'}).click();
  await hit(page).click(); await bar(2).click(); await expect(hit(page)).toHaveAttribute('aria-pressed','false');
  await song.getByRole('group',{name:'Song length in bars'}).getByRole('button',{name:'32'}).click();
  await expect(song.locator('.arranger-bar')).toHaveCount(32);
  await song.getByRole('group',{name:'Song length in bars'}).getByRole('button',{name:'64'}).click();
  await expect(song.locator('.arranger-bar')).toHaveCount(64);
  await song.getByRole('button',{name:'Loop selected bar'}).click();
  await page.getByRole('button',{name:'Play preview',exact:true}).click();
  await expect(bar(2)).toHaveAttribute('data-playing','true');
  const songExport = song.getByRole('button',{name:'Download whole song MIDI · 4/4 demo',exact:true});
  const patternExport = panel(page,'Step Sequencer').getByRole('button',{name:'Download pattern MIDI · 4/4 demo',exact:true});
  await expect(songExport).toHaveText('MIDI · 4/4 demo');
  await expect(patternExport).toHaveText('MIDI · 4/4 demo');
  const download = page.waitForEvent('download'); await songExport.click();
  const file = await download; expect(file.suggestedFilename()).toBe('vox-song.mid');
  const midi = await readFile((await file.path())!);
  expect(midi.toString('ascii',0,4)).toBe('MThd');
  expect(midi.includes(Buffer.from([0, 0xff, 0x58, 4, 4, 2, 24, 8]))).toBe(true); // Fixed 4/4 time-signature event.
});

test('Docking preserves playback and musical edits; only the named workspace layout survives reload', async ({page}) => {
  const footer = page.locator('.instrument-foot');
  await expect(footer.getByText('Browser demo',{exact:true})).toBeVisible();
  await expect(footer.getByText('Musical edits reset on reload',{exact:true})).toBeVisible();
  await expect(footer.getByText('Workspace layout saved locally',{exact:true})).toBeVisible();
  const kit = page.getByRole('combobox',{name:'Kit',exact:true});
  const kickLevel = panel(page,'Mixer Dock').getByRole('slider',{name:'Mixer Kick Level',exact:true});
  const kickStep = hit(page,'Kick, step 2');
  await expect(kit).toHaveValue('Analog Motion');
  await kit.selectOption('Dry Circuit');
  await setValue(panel(page,'Inspector').getByRole('textbox',{name:'Level value',exact:true}),'0.4');
  await expect(kickLevel).toHaveAttribute('aria-valuenow','0.4');
  await expect(kickStep).toHaveAttribute('aria-pressed','false');
  await kickStep.click();
  await page.getByRole('button',{name:'Play preview',exact:true}).click();
  await expect(page.getByRole('button',{name:'Stop preview',exact:true})).toBeVisible();
  const grip = page.getByRole('button',{name:'Move Pad Rack to right dock',exact:true});
  const left = page.locator('[data-zone="left"]'); const right = page.locator('[data-zone="right"]');
  const pointer = (await grip.boundingBox())!; const target = (await right.boundingBox())!;
  await page.mouse.move(pointer.x+11,pointer.y+11); await page.mouse.down();
  await page.mouse.move(target.x+50,target.y+95,{steps:15});
  await page.mouse.move(target.x+53,target.y+100,{steps:3});
  await expect(right.locator('[data-dock-preview=true]')).toBeVisible();
  await page.mouse.up();
  await expect(right.locator('[data-dock="pads"]')).toBeVisible(); await expect(left).toBeHidden();
  await expect(page.getByRole('button',{name:'Stop preview',exact:true})).toBeVisible();
  await expect(hit(page,'Kick, step 2')).toHaveAttribute('aria-pressed','true');
  const divider = page.getByRole('separator',{name:'Resize right dock'}); const b = (await divider.boundingBox())!;
  const before = bounded(await divider.getAttribute('aria-valuenow'));
  await page.mouse.move(b.x+b.width/2,b.y+62); await page.mouse.down(); await page.mouse.move(b.x-30,b.y+62,{steps:4}); await page.mouse.up();
  const after = bounded(await divider.getAttribute('aria-valuenow')); expect(after).toBeGreaterThan(before+20);
  await divider.press('ArrowLeft'); const width = bounded(await divider.getAttribute('aria-valuenow')); expect(width).toBeGreaterThan(after);
  const summary = page.locator('.workspace-menu summary');
  await summary.click();
  await expect(page.getByText('Only the layout is saved. Reload resets Kit, pad, Pattern and Song edits.',{exact:true})).toBeVisible();
  await page.getByRole('textbox',{name:'Save workspace layout as'}).fill('Studio setup');
  await page.getByRole('button',{name:'Save layout',exact:true}).click();
  await expect(footer.getByRole('status')).toHaveText('Workspace layout saved as Studio setup.');
  await workspace(page,'Reset Workspace');
  await expect(left.locator('[data-dock="pads"]')).toBeVisible();
  await expect(kickStep).toHaveAttribute('aria-pressed','true');
  await workspace(page,'Studio setup'); await expect(right.locator('[data-dock="pads"]')).toBeVisible();

  const mode = page.getByRole('group',{name:'Drums mode'});
  const song = panel(page,'Song Arranger');
  await mode.getByRole('button',{name:'Song',exact:true}).click();
  await song.getByRole('group',{name:'Song length in bars'}).getByRole('button',{name:'32',exact:true}).click();
  await expect(song.locator('.arranger-bar')).toHaveCount(32);
  await mode.getByRole('button',{name:'Pattern',exact:true}).click();
  await expect(kit).toHaveValue('Dry Circuit');
  await expect(kickLevel).toHaveAttribute('aria-valuenow','0.4');
  await expect(kickStep).toHaveAttribute('aria-pressed','true');

  await page.reload();
  await expect(summary).toContainText('Studio setup');
  await expect(right.locator('[data-dock="pads"]')).toBeVisible();
  await expect(divider).toHaveAttribute('aria-valuenow',String(width));
  await expect(kit).toHaveValue('Analog Motion');
  await expect(kickLevel).toHaveAttribute('aria-valuenow','0.85');
  await expect(kickStep).toHaveAttribute('aria-pressed','false');
  await mode.getByRole('button',{name:'Song',exact:true}).click();
  await expect(song.locator('.arranger-bar')).toHaveCount(16);
  await page.getByRole('button',{name:'Hide Pad Rack'}).click(); await expect(right.locator('[data-dock="pads"]')).toHaveCount(0);
  await showPanel(page,'Pad Rack'); await expect(left.locator('[data-dock="pads"]')).toBeVisible();
});

test('Expanded Mixer has 16 strips, real Out 2 signal, and Kit Level affects that bus', async ({page}) => {
  const mixer = panel(page,'Mixer Dock');
  await mixer.getByRole('button',{name:'Expanded',exact:true}).click();
  await expect(mixer.locator('.channel-strip')).toHaveCount(16);
  const bank = mixer.getByRole('group',{name:'Mixer bank'});
  await bank.getByRole('button',{name:'9–16',exact:true}).click();
  await expect.poll(() => mixer.locator('.channel-bank').evaluate(el => el.scrollLeft)).toBeGreaterThan(300);
  await bank.getByRole('button',{name:'1–8',exact:true}).click();
  const sep = page.getByRole('separator',{name:'Resize mixer height'});
  const h = bounded(await sep.getAttribute('aria-valuenow')); await sep.press('ArrowUp'); expect(bounded(await sep.getAttribute('aria-valuenow'))).toBeGreaterThan(h);
  await mixer.getByRole('combobox',{name:'Kick mix output'}).selectOption('1');
  await showPanel(page,'Routing');
  const routing = panel(page,'Routing');
  await expect(routing.getByText(/mixed to one browser playback device, not separate DAW outputs/)).toBeVisible();
  await expect(routing.getByRole('combobox',{name:'Pad output',exact:true})).toHaveValue('1');
  const bus = routing.getByRole('meter',{name:'Out 2 local bus peak'}).locator('..');
  const main = mixer.getByRole('meter',{name:'Main left pre-device peak'}).locator('..');
  await panel(page,'Pad Rack').getByRole('button',{name:/^Audition .*Kick/}).click();
  await levelAt(bus).toBeGreaterThan(.008); await levelAt(main).toBeLessThan(.00001);
  await page.getByRole('button',{name:'Panic',exact:true}).click();
  await setValue(mixer.getByRole('textbox',{name:'Kit Level value',exact:true}),'0');
  await levelAt(bus).toBeLessThan(.001);
  await panel(page,'Pad Rack').getByRole('button',{name:/^Audition .*Kick/}).click();
  await expect(panel(page,'Pad Rack')).toContainText('velocity 100');
  await levelAt(bus).toBeLessThan(.002);
  await mixer.getByRole('button',{name:'Collapsed',exact:true}).click(); await expect(mixer.locator('.channel-strip')).toHaveCount(0);
  await mixer.getByRole('button',{name:'Compact',exact:true}).click(); await expect(mixer.locator('.channel-strip')).toHaveCount(8);
});

test('Sample file decode, waveform trim and reversal are working browser interactions', async ({page}) => {
  await page.getByRole('combobox',{name:'Primary editor'}).selectOption('sample');
  const sample = panel(page,'Sample Editor');
  await sample.getByLabel('Import pad sample').setInputFiles({name:'fixture.wav',mimeType:'audio/wav',buffer:testWav()});
  await expect(sample.getByRole('group',{name:'Decoded sample waveform'})).toBeVisible();
  await expect(sample.locator('.wave-peak')).toHaveCount(240);
  await sample.getByRole('slider',{name:'Sample start handle'}).press('ArrowRight');
  await expect(sample.getByRole('slider',{name:'Sample start',exact:true})).toHaveAttribute('aria-valuenow','0.01');
  await setValue(sample.getByRole('textbox',{name:'Sample start value',exact:true}),'0.25');
  await expect(sample.getByRole('slider',{name:'Sample start handle'})).toHaveAttribute('aria-valuenow','0.25');
  await sample.getByRole('button',{name:'Reverse',exact:true}).click();
  await expect(sample.getByRole('button',{name:'Reverse',exact:true})).toHaveAttribute('aria-pressed','true');
  await sample.getByRole('button',{name:'Audition pad'}).click();
  await levelAt(panel(page,'Pad Rack').locator('.rack-readback .meter')).toBeGreaterThan(.008);
  await sample.getByRole('button',{name:'Remove sample'}).click(); await expect(sample).toContainText('Player A is empty');
});

test('Generator exposes all 23 styles, calls a clearly labeled test groove and honors lane locks', async ({page}) => {
  await grid(page).getByRole('button',{name:'Kick, step 1',exact:true}).click();
  await page.getByRole('button',{name:'Locks',exact:true}).click();
  await grid(page).getByRole('button',{name:'Lock Kick lane for generator'}).click();
  await showPanel(page,'Generator');
  const generator = panel(page,'Generator');
  await expect(generator.getByRole('combobox',{name:/Musical style/}).locator('option')).toHaveCount(23);
  await expect(generator).toContainText('Genre engine not connected');
  await generator.getByRole('button',{name:'Apply test groove'}).click();
  await expect(hit(page)).toHaveAttribute('aria-pressed','false');
  await expect(page.getByRole('status')).toContainText('Generic test groove applied');
});
