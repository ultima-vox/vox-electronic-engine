import AxeBuilder from '@axe-core/playwright';
import { test, expect } from '@playwright/test';
import type { Page } from '@playwright/test';
import { testWav } from '../src/shared-test-utils/fixtures';

const product = async (page: Page, name: string) => page.getByRole('tab',{name,exact:true}).click();
const zone = async (page: Page, name: string) => page.locator('.fx-nav').getByRole('tab',{name,exact:true}).click();

for (const [width,height] of [[1040,700],[1280,720],[1440,900],[1600,1000]]) {
  test(`Drums fits ${width}×${height}; target hit sizes, one Mixer and no viewport clipping`, async ({page}, testInfo) => {
    await page.setViewportSize({width,height});
    const errors: string[] = []; page.on('pageerror',e=>errors.push(e.message));
    await page.goto('/'); await page.getByRole('heading',{name:'VOX Drums',exact:true}).waitFor(); await page.evaluate(() => document.fonts.ready);
    await expect.poll(() => page.locator('.work-area').evaluate(e => e.clientWidth)).toBeGreaterThan(900);
    await expect(page.getByRole('region',{name:'Pad Rack',exact:true})).toHaveCount(1);
    await expect(page.getByRole('region',{name:'Mixer Dock',exact:true})).toHaveCount(1);
    const dims = await page.evaluate(() => ({ page:document.documentElement.scrollWidth, view:innerWidth, bottom:document.querySelector('.mixer')!.getBoundingClientRect().bottom, centerBottom:document.querySelector('.work-area')!.getBoundingClientRect().bottom, mixerTop:document.querySelector('.mixer')!.getBoundingClientRect().top, icon:document.querySelector('.hide-dock')!.getBoundingClientRect().right }));
    expect(dims.page).toBeLessThanOrEqual(dims.view); expect(dims.bottom).toBeLessThan(height); expect(dims.mixerTop).toBeGreaterThanOrEqual(dims.centerBottom);
    expect(dims.icon).toBeLessThan(width);
    const padBounds = await page.locator('.pad-face').first().boundingBox(); expect(padBounds?.width).toBeGreaterThan(34);
    const cellBounds = await page.locator('.step-hit').first().boundingBox(); expect(cellBounds?.width).toBeGreaterThan(17);
    expect(errors).toEqual([]);
    if (process.env.VOX_VISUAL_CAPTURE === '1') await page.screenshot({path:testInfo.outputPath(`drums-${width}x${height}.png`)});
  });
}

test('Center split stack/side, maximize and responsive tab fallback maintain musical data', async ({page}, testInfo) => {
  await page.goto('/');
  await page.getByRole('button',{name:'Add second editor',exact:true}).click();
  const divider = page.getByRole('separator',{name:'Resize editor split'});
  await expect(divider).toBeVisible(); await expect(divider).toHaveAttribute('aria-orientation','vertical');
  const box=(await divider.boundingBox())!; const old=Number(await divider.getAttribute('aria-valuenow'));
  await page.mouse.move(box.x+box.width/2,box.y+70); await page.mouse.down(); await page.mouse.move(box.x+45,box.y+70,{steps:4}); await page.mouse.up();
  expect(Number(await divider.getAttribute('aria-valuenow'))).toBeGreaterThan(old+2);
  await page.getByRole('button',{name:'Switch to stacked editors'}).click(); await expect(divider).toHaveAttribute('aria-orientation','horizontal');
  await page.getByRole('button',{name:'Maximize Step Sequencer'}).click();
  await expect(page.getByRole('region',{name:'Pad Rack',exact:true})).toBeHidden();
  await page.getByRole('button',{name:'Restore Step Sequencer'}).click();
  await page.getByRole('button',{name:'Switch to side-by-side editors'}).click();
  await page.setViewportSize({width:1040,height:700});
  const fallback=page.getByRole('group',{name:'Visible center editor'});
  await expect(fallback).toBeVisible(); await fallback.getByRole('button',{name:'Song Arranger',exact:true}).click();
  await expect(page.getByRole('region',{name:'Song Arranger',exact:true})).toBeVisible();
  await fallback.getByRole('button',{name:'Step Sequencer',exact:true}).click();
  await expect(page.getByRole('button',{name:'Kick, step 1',exact:true})).toHaveAttribute('aria-pressed','true');
  if (process.env.VOX_VISUAL_CAPTURE === '1') await page.screenshot({path:testInfo.outputPath('split-fallback.png')});
});

test('FX eight work areas, editable 20-curve timeline, assignment and configured processing', async ({page}, testInfo) => {
  const errors: string[]=[]; page.on('pageerror',e=>errors.push(e.message));
  await page.goto('/#fx'); await expect(page.locator('.suite')).toHaveAttribute('data-theme','fx');
  const tabs=page.getByRole('tablist',{name:'FX work areas'}); await expect(tabs.getByRole('tab')).toHaveCount(8);
  await page.getByRole('button',{name:'Generate program'}).click(); await expect(page.getByRole('status')).toContainText('Seeded browser Program');
  await zone(page,'Design'); await expect(page.locator('.fx-layers > section')).toHaveCount(4);
  await page.getByRole('button',{name:'Edit layer D Pan curve'}).click();
  await expect(page.locator('.curve-directory').getByRole('button')).toHaveCount(20);
  const curve = page.getByRole('group',{name:'Layer D Pan curve editor'});
  const node = curve.getByRole('button',{name:/^Node 2,/}); const before = await node.getAttribute('aria-label'); const box = (await node.boundingBox())!;
  await page.mouse.move(box.x+box.width/2,box.y+box.height/2); await page.mouse.down(); await page.mouse.move(box.x+box.width/2+24,box.y+box.height/2-15,{steps:6}); await page.mouse.up();
  await expect(node).not.toHaveAttribute('aria-label',before!);
  await page.getByRole('button',{name:'Undo Program edit'}).click(); await expect(node).toHaveAttribute('aria-label',before!);
  await page.getByRole('button',{name:'+ Node',exact:true}).click(); await expect(curve.getByRole('button')).toHaveCount(5);
  await page.getByRole('button',{name:'Copy curve',exact:true}).click();
  await page.getByRole('button',{name:'Global Amp timeline'}).click(); await page.getByRole('button',{name:'Paste',exact:true}).click();
  await expect(page.getByRole('group',{name:'Global Amp curve editor'}).getByRole('button')).toHaveCount(5);
  if (process.env.VOX_VISUAL_CAPTURE === '1') await page.screenshot({path:testInfo.outputPath('fx-timeline.png')});
  await zone(page,'Motion'); await expect(page.getByRole('heading',{name:'LFO 1',exact:true})).toBeVisible();
  await page.getByRole('button',{name:'Macro 4',exact:true}).click();
  await page.getByRole('combobox',{name:'Macro assignment target'}).selectOption('C.cutoff');
  await page.getByRole('button',{name:'Assign Macro 4'}).click();
  await expect(page.getByRole('combobox',{name:'Route 3 source'})).toHaveValue('Macro 4');
  await expect(page.getByRole('combobox',{name:'Route 3 target'})).toHaveValue('C.cutoff');
  await zone(page,'Process'); await expect(page.locator('.fx-layers > section')).toHaveCount(4);
  await page.getByRole('combobox',{name:'Effect type 1'}).selectOption('Reverb');
  await page.getByRole('switch',{name:'Bypass slot 1'}).click();
  await expect(page.locator('.fx-chain-track')).toContainText('Reverb (bypassed)');
  await zone(page,'Trigger'); await page.getByRole('button',{name:'Play visual pattern'}).click();
  await expect(page.locator('.event-readout')).toContainText('VISUAL EVENT RUNNING');
  await page.getByRole('button',{name:'Stop visual pattern'}).click();
  await zone(page,'Advanced'); await expect(page.getByText('Not connected · cap 64',{exact:true})).toBeVisible();
  await expect(page.getByText('Not measured',{exact:true})).toBeVisible();
  await page.getByRole('button',{name:'Panic visual events'}).click();
  await expect(page.getByText('0 / 8',{exact:true})).toBeVisible();
  expect(errors).toEqual([]);
});

test('FX search/favorites, independent A/B, validated JSON import and saved Program reload', async ({page}) => {
  await page.goto('/#fx'); await zone(page,'Presets');
  await page.getByRole('searchbox',{name:'Search presets'}).fill('ember');
  const load=page.getByRole('button',{name:'Load Ember Strike'});
  await expect(load).toBeVisible(); await expect(page.getByRole('button',{name:'Load Glass Drift'})).toHaveCount(0);
  await page.getByRole('button',{name:'Favorite Ember Strike'}).click(); await page.getByRole('button',{name:'Favorites',exact:true}).click();
  await load.click(); await expect(page.locator('.fx-program-label strong')).toHaveText('Ember Strike');
  await page.getByRole('button',{name:'Copy A to B'}).click();
  const cmp=page.getByRole('group',{name:'Program comparison'}); await cmp.getByRole('button',{name:'B',exact:true}).click();
  await page.getByRole('button',{name:'Favorites',exact:true}).click(); await page.getByRole('searchbox',{name:'Search presets'}).fill('glass');
  await page.getByRole('button',{name:'Load Glass Drift'}).click();
  await cmp.getByRole('button',{name:'A',exact:true}).click(); await expect(page.locator('.fx-program-label strong')).toHaveText('Ember Strike');
  await cmp.getByRole('button',{name:'B',exact:true}).click(); await expect(page.locator('.fx-program-label strong')).toHaveText('Glass Drift');
  await page.getByRole('textbox',{name:'Save Program as'}).fill('My texture');
  await page.getByRole('button',{name:'Save Program',exact:true}).click();
  await page.reload(); await zone(page,'Presets');
  await page.getByRole('group',{name:'FX preset collection'}).getByRole('button',{name:'User',exact:true}).click(); await page.getByRole('button',{name:'Load My texture'}).click();
  await expect(page.locator('.fx-program-label strong')).toHaveText('My texture');
  await page.getByLabel('Import FX Program JSON').setInputFiles({name:'bad.json',mimeType:'application/json',buffer:Buffer.from('{"version":2}')});
  await expect(page.getByRole('alert')).toContainText('Invalid Program v1');
  await expect(page.locator('.fx-program-label strong')).toHaveText('My texture');
});

test('Separate Console routes, three token accents and live control fixture states', async ({page}) => {
  await page.goto('/'); await product(page,'Design System');
  await expect(page.getByRole('heading',{name:'Design System Console'})).toBeVisible();
  const themes=page.getByRole('group',{name:'Product accent theme'});
  await themes.getByRole('button',{name:'Synth',exact:true}).click(); await expect(page.locator('.suite')).toHaveAttribute('data-theme','synth');
  await page.getByRole('button',{name:'Tokens',exact:true}).click(); await expect(page.getByText('#c3a9f7',{exact:true})).toBeVisible();
  await themes.getByRole('button',{name:'FX',exact:true}).click(); await expect(page.getByText('#7ce2d0',{exact:true})).toBeVisible();
  await themes.getByRole('button',{name:'Drums',exact:true}).click(); await expect(page.getByText('#f3bd72',{exact:true}).count()).resolves.toBeGreaterThan(0);
  await page.getByRole('button',{name:'Showcase',exact:true}).click(); const state=page.getByRole('combobox',{name:'Fixture state'});
  const knob=page.getByRole('slider',{name:'Cutoff',exact:true});
  await expect(knob.locator('..')).toHaveAttribute('data-state','normal');
  await knob.hover(); expect(await knob.evaluate(el => el.matches(':hover'))).toBe(true);
  await state.selectOption('Selected'); await expect(knob.locator('..')).toHaveAttribute('data-selected','true');
  await state.selectOption('Disabled'); await expect(knob).toHaveAttribute('aria-disabled','true');
  await state.selectOption('Automated'); await expect(knob.locator('..')).toContainText('Auto');
  await state.selectOption('Modulated'); await expect(knob.locator('..')).toContainText('Mod'); await expect(knob).toHaveAttribute('aria-valuenow','1200');
  await state.selectOption('Warning'); await expect(knob.locator('..')).toContainText('Check range');
  await state.selectOption('Error'); await expect(knob.locator('..')).toContainText('Link unavailable');
  await page.getByRole('button',{name:'Binding',exact:true}).click(); await expect(page.getByRole('region',{name:'Binding seams',exact:true}).getByText('drm.p01.level',{exact:true})).toBeVisible();
  await product(page,'VOX Drums');
  await expect(page.getByRole('main')).not.toContainText('drm.p01.level');
  await expect(page.getByRole('region',{name:'Binding seams',exact:true})).toBeHidden();
  await expect(page.getByRole('table',{name:'DS-00 native binding rows'})).toBeHidden();
});

test('Accessibility smoke audit of active Drums, FX and Console landing surfaces', async ({page}) => {
  await page.goto('/');
  const scan = async (name: string) => {
    const { violations } = await new AxeBuilder({page}).withTags(['wcag2a','wcag2aa','wcag21a','wcag21aa']).analyze();
    expect(violations.map(({id,nodes}) => ({id,targets:nodes.map(n=>n.target)})),name).toEqual([]);
  };
  await scan('Drums'); await product(page,'VOX FX'); await scan('FX'); await product(page,'Design System'); await scan('Design System');
});

test('Owner review interactions: Composition, Sound Design, Mixing, and docked Generator', async ({page}, testInfo) => {
  await page.goto('/'); await page.evaluate(() => document.fonts.ready);
  const choose = async (name: string) => {
    await page.locator('.workspace-menu summary').click();
    await page.getByRole('button',{name,exact:true}).click();
  };
  await choose('Composition');
  await page.getByRole('group',{name:'Drums mode'}).getByRole('button',{name:'Song',exact:true}).click();
  await expect(page.getByRole('region',{name:'Song Arranger',exact:true})).toBeVisible();
  await expect(page.getByRole('region',{name:'Step Sequencer',exact:true})).toBeVisible();
  if (process.env.VOX_VISUAL_CAPTURE === '1') await page.screenshot({path:testInfo.outputPath('composition.png')});
  await choose('Sound Design');
  await expect(page.getByRole('region',{name:'Sound Editor',exact:true})).toBeVisible();
  await expect(page.getByRole('region',{name:'Sample Editor',exact:true})).toBeVisible();
  await page.locator('[data-pad-select="0"]').click();
  await page.getByLabel('Import pad sample').setInputFiles({name:'reference-sine.wav',mimeType:'audio/wav',buffer:testWav()});
  await expect(page.getByRole('group',{name:'Decoded sample waveform'})).toBeVisible();
  if (process.env.VOX_VISUAL_CAPTURE === '1') await page.screenshot({path:testInfo.outputPath('sound-design.png')});
  await choose('Mixing');
  await expect(page.locator('.mixer .channel-strip')).toHaveCount(16);
  if (process.env.VOX_VISUAL_CAPTURE === '1') await page.screenshot({path:testInfo.outputPath('mixing.png')});
  await choose('Beatmaking');
  await page.getByRole('group',{name:'Drums mode'}).getByRole('button',{name:'Pattern',exact:true}).click();
  await page.locator('.panel-menu summary').click();
  await page.getByRole('checkbox',{name:/Generator/}).check();
  await page.locator('.panel-menu summary').click();
  await expect(page.getByRole('region',{name:'Generator',exact:true})).toBeVisible();
  if (process.env.VOX_VISUAL_CAPTURE === '1') await page.screenshot({path:testInfo.outputPath('generator.png')});
});
