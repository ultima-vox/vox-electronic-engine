# 05 — Ultima VOX Music UI: как полностью заменить внешний вид JUCE, сохранив его функциональность

**Версия:** proposal 1.0, 08.11.2026. **Визуальные макеты/картинки не создаются по этой задаче.** Документ описывает точную реализацию интерфейса и его связывание с DSP в C++/JUCE.

## 1. Главная идея: JUCE — framework, а не внешний вид

JUCE не заставляет использовать стандартный визуальный стиль. `juce::Component` предоставляет события мыши/клавиатуры, иерархию, фокус, перерисовку и bounds; `juce::LookAndFeel_V4` переопределяет отрисовку Slider/Button/ComboBox/PopupMenu; `juce::Graphics`, `juce::Path`, `juce::Drawable` позволяют создавать собственную векторную графику. Данные и хостовые параметры остаются в `juce::AudioProcessor`/`APVTS`. **Никакого HTML/CSS поверх VST3 для первого релиза, отдельного Chromium, Electron/CEF и плагинного браузера.** Не менять SDK ради скина.

Проверенные ссылки JUCE: [LookAndFeel_V4](https://docs.juce.com/master/classjuce_1_1LookAndFeel__V4.html), [Component](https://docs.juce.com/master/classjuce_1_1Component.html), [SliderAttachment](https://docs.juce.com/master/classjuce_1_1AudioProcessorValueTreeState_1_1SliderAttachment.html), [ParameterAttachment](https://docs.juce.com/master/classjuce_1_1ParameterAttachment.html), [Drawable](https://docs.juce.com/master/classjuce_1_1Drawable.html). Это **реальные API**, а не придуманная библиотека.

## 2. Что реально можно переиспользовать из старого кода

Репозиторий `ultima-vox/vox-electronic-engine` уже содержит `libs/vox-ui/Tokens.h`, `Typography.h`, `VoxLookAndFeel.*`, компоненты `VoxButton`, `VoxTabBar`, `libs/vox-ui/showcase` и `docs/ui/DESIGN_SYSTEM.md`; подтверждено на интеграционном SHA `f9c8bc6`. **Не переписывать эту библиотеку с нуля**. Сначала откомпилировать её с маленьким независимым `AudioProcessorEditor` и получить скриншоты; не тащить вместе с библиотекой `apps/vst3/PluginProcessor.*` / Rack rail.

**Задача архитектору UI:** превратить `vox::ui` в общую библиотеку, которая зависит **только от JUCE graphics/gui + собственных токенов**, а не от `Rack`, DSP, old APVTS IDs, `InstrumentId` или `.vins`. Три `ProductEditor` используют одну тему и компоненты; различаются содержимым страниц.

## 3. Правила визуального языка

**Дизайн-направление:** техническая музыкальная рабочая станция премиального уровня, строгая геометрия, контраст, высокая плотность управления без «киберпанк-перегрузки». Логотип и идентичность **ULTIMA VOX MUSIC**, индивидуальные имена продуктов. Не копировать skin Serum/Vital/Phase Plant/Kontakt, не использовать чужие asset/bitmap/fonts без лицензии.

| Token | Значение (проверенный legacy UI) | Использование |
|---|---|---|
| Background | `#06121D` | фон редактора |
| Panel | `#0B1925` | основные группы |
| Raised panel | `#102230` | выделенная текущая зона |
| Graph/control | `#051019` / `#0A1722` | редактирование/трек |
| Border | `#1D3B50` | контуры/разделители |
| Accent | `#00DDF5` | взаимодействие/primary active |
| Accent hover | `#42EEFF` | hover/focus |
| Text primary | `#E9F3FA` | основные значения |
| Text secondary | `#9AB2C5` | подписи |
| Warning/Error | `#E3B341` / `#E4425D` | реальные статусы |

**Акценты продуктов** (новые предложения): Synthesis — Electric Violet `#9668F2` (кроме фокусного cyan), Drums — Copper/Coral `#EF805D`, FX — Aqua `#16CDBA`. Акцент — идентичность, а не единственный носитель статуса. `focus`, active, disabled и error различаются контуром, надписью и доступным цветовым контрастом.

**Typography:** токены из `Typography.h` сохранить; no decorative illegible fonts. Base 12–14 px labels, 16–20 px section headers, 26 px title; числовые значения tabular, единицы `Hz/kHz/ms/s/dB/semitones`; установка/встраивание собственных шрифтов допускается только после юридической проверки. Рекомендуется рендер векторных ручек, пиктограмм и графиков; большие фоны можно кэшировать, но не создавать отдельные PNG для каждого значения параметра.

**Control grammar:** `label + editable value + functional control + visible modulation overlay`. Rotary: 270° arc, base value stroke, separate modulation halo, value with unit, double-click reset to actual default, shift/fine drag, wheel optional with host safety, Ctrl+click direct edit only when supported. Pad: label/note/selected state/velocity/trigger feedback, keyboard and mouse accessible. Meter: actual peak/RMS with 20–30 FPS UI polling; clip hold and reset. No fake oscilloscopes, no decorative sliders wired to wrong parameter.

## 4. Два уровня кастомизации

### Tier A — самый дешёвый/надёжный: JUCE controls, полностью свой LookAndFeel

Используем `juce::Slider`, `juce::ComboBox`, `juce::ToggleButton`, `juce::TextButton`, `juce::PopupMenu` как функциональные controls. Заменяем их drawing через собственный `vox::ui::VoxLookAndFeel : public juce::LookAndFeel_V4` (он уже есть в legacy; расширяем, не дублируем). По API реально можно override `drawRotarySlider`, `drawLinearSlider`, `drawButtonBackground`, `drawToggleButton`, `drawComboBox`, `getComboBoxFont`, `drawTooltip`, `createSliderTextBox`.

**Преимущества:** штатные мышиные/клавиатурные события, double-click reset, host gesture через attachments, popup/focus и множество исправленных edge cases остаются JUCE. Реализовать так **все базовые ручки, переключатели, numeric fields, popup-контролы и панели**.

Проверенный по сигнатурам эскиз C++ (предлагаемый код, не существующий файл):

```cpp
class VoxLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics& g, int x, int y,
                          int width, int height, float proportion,
                          float startAngle, float endAngle,
                          juce::Slider& slider) override {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x),
            static_cast<float>(y), static_cast<float>(width),
            static_cast<float>(height)).reduced(5.0f);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 3.0f;
        const auto centre = bounds.getCentre();
        juce::Path track, active;
        const auto from = startAngle;
        const auto to = startAngle + proportion * (endAngle - startAngle);
        track.addCentredArc(centre.x, centre.y, radius, radius,
                            0.0f, from, endAngle, true);
        active.addCentredArc(centre.x, centre.y, radius, radius,
                             0.0f, from, to, true);
        g.setColour(juce::Colour::fromRGB(29, 59, 80));
        g.strokePath(track, juce::PathStrokeType(3.0f));
        g.setColour(slider.isEnabled()
                        ? juce::Colour::fromRGB(0, 221, 245)
                        : juce::Colour::fromRGB(88, 116, 135));
        g.strokePath(active, juce::PathStrokeType(3.0f));
    }
};
```

*Пример демонстрирует переопределение графики: label/value/modulation halos, size policy и exact default reset добавляет существующий `VoxKnob` composition. Перед включением требуется компиляция с закреплённой версией JUCE.*

Привязка обычной ручки без собственной реализации MIDI/host gestures:

```cpp
juce::Slider cutoff;
cutoff.setSliderStyle(juce::Slider::RotaryVerticalDrag);
cutoff.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 22);
// keep attachment alive for exactly the lifetime of the slider and APVTS
juce::AudioProcessorValueTreeState::SliderAttachment cutoffAttachment {
    processor.apvts, "synth.filter.cutoff", cutoff
};
```

**Важно:** `processor.apvts` в примере — поле **нового проектируемого Processor**, а не утверждение о текущем исходнике. `synth.filter.cutoff` — пример ID, финальная карта утверждается в отдельной спецификации параметров. Для настоящего C++ члена класса порядок конструирования `Slider`/`APVTS`/attachment нужно обеспечить на уровне полей и RAII.

### Tier B — настоящее кастомное редактирование: свой juce::Component

Нужен для **waveform editor, curve/timeline, pattern grid, modulation matrix, piano roll, XY pad, routing graph, sample slices, per-pad transient curves**. Эти редакторы не «присосать поверх» обычного Slider: у них собственное typed state, выделение узлов, Undo/Redo, snap, selection, hit regions, export.

`CustomCurveEditor : public juce::Component` отвечает только за `paint`, `mouseDown/Drag/Up`, keyboard/focus и local selection; **не** за lifetime аудиообъектов. UI получает immutable snapshot из `EditorModel` (контроллер thread), редактирует копию `Program` и публикует validated prepared snapshot через bridge. Во время drag repaint можно локально делать 30–60 FPS, а commits коалесцировать; каждый ввод должен достигнуть DSP предсказуемо и восстанавливаться в DAW проекте. `UndoManager` на controller/message thread.

Для кастомного **односкалярного** параметра вместо Slider применяем `juce::ParameterAttachment` с реальными `sendInitialUpdate`, `beginGesture`, `setValueAsPartOfGesture`, `endGesture`, `setValueAsCompleteGesture` — методы официально документированы. Не вызывать их из `processBlock`.

**Формат связей:**

```text
DAW automation ───> JUCE parameter (stable ID) ──> base atomics ──> DSP smoothing
         ↑                      ↑
         │               ParameterAttachment / APVTS Attachment
VOX custom control <────── message-thread updates

Custom curve / pattern ─> EditorModel ─> validate/prepare snapshot ─> RT-safe publish ─> DSP
                            ↑
                     Undo/Redo + serialized state

DSP peak/voice/grain counters ─> lock-free POD telemetry snapshot ─> UI timer 20–30Hz
```

## 5. Архитектура исходников (предложение)

```text
libs/vox-ui/
  Tokens.h                  // already exists; extend without breaking semantics
  Typography.h              // already exists
  VoxLookAndFeel.*          // already exists; theme entry point
  theme/Theme.h             // theme semantic tokens and per-product accents
  controls/VoxKnob.*        // composed JUCE Slider + label/value/mod overlay
  controls/VoxToggle.*      // JUCE button, custom skin
  controls/VoxSelector.*    // JUCE ComboBox, custom popup
  controls/VoxMeter.*       // real peak/RMS, UI poll, zero audio dependency
  controls/VoxXYPad.*       // 2 parameter attachments with gestures
  editors/VoxCurveEditor.*  // typed snapshot + undo
  editors/VoxStepGrid.*
  editors/VoxWaveform.*
  editors/VoxModMatrix.*
  layout/VoxResponsivePanel.*
  assets/                   // only licensed SVG/PDF/typography data; no fonts disclosed
products/*/Editor.*         // each product's screen composition, not duplicate widgets
```

Do **not** create `legacy Source/UI/`, fork three VoxLookAndFeel copies, or couple `vox-ui` to Rack or DSP. `vox-ui` links `juce_gui_basics` and graphic modules; product layer links DSP only through a narrow `EditorModel`/`ParameterStore` interface. Shared preview renderer should reuse the same DSP curves/math in non-RT mode for accuracy; **no renderer owns mutable voice pointers**.

## 6. Product-specific workspaces (no bitmap mockups)

| Product | Primary navigation | Specialized editors |
|---|---|---|
| **Trance Synthesis** | PERFORM / BASS / ACID / SYNTH / SEQUENCE / MOD / FX / PRESETS / ADVANCED | 3-OSC mixer, dual filters, modulation matrix, piano roll, accent/slide, note automation overlays |
| **Drums** | PERFORM / SOUND / PATTERN / MIXER / GENERATE / ROUTING / PRESETS / ADVANCED | 4×4 pads, model-specific envelopes, waveform with sample slices, 16-lane velocity/ratchet grid, 16 mixer strips |
| **FX** | PERFORM / DESIGN / TIMELINE / MOTION / PROCESS / TRIGGER / PRESETS / ADVANCED | 4 source cards, 20 curves, 64-route modulation, 4 FX stages, playback timeline, real event/grain telemetry |

**Common header:** logo (product-specific), preset selector, tempo/sync state, Save, Compare A/B, Undo/Redo, CPU (measured/nonblocking), output peak. Performance buttons must have true state. **Common footer:** MIDI activity, voice/event count, patch dirty, plugin version, sample/missing file alerts. Dynamic hiding vs dummy controls: if DSP feature absent, section is not exposed as usable.

## 7. DPI, resizing, rendering and performance

- Editor min design target 1040×700; standard 1180×760; large 1500×920; scale 100/125/150/200% Windows. Default minimum is a design hypothesis; verify controls and adjust. Use `juce::FlexBox`/`juce::Grid` or computed `resized()` layout; no hard-coded absolute coordinates for critical controls. Editor resize saves only display dimensions, not DSP state.
- Vector art first; cache static drawing (background, waveform mini maps, spectrogram/oscilloscope decimated buffers) in JUCE Images prepared on the **message thread**. Never instantiate SVG parsers, dynamic fonts, or resource files in audio callbacks. `juce::Drawable::createFromSVGString` / `createFromSVGFile` parse supported SVG features; use static assets not volatile untrusted markup.
- UI meters/graphs: 20–30 fps for routine meters, 30–60 fps during active gesture or detailed scope; if editor hidden, suspend repaint timers. Avoid `juce::OpenGLContext` by default: it adds context/platform complexity, plugin-host interactions and lifecycle hazards. Consider hardware rendering only after profiling actual curve editor CPU; never require GPU for sound.
- Use state/colour not only for accessibility: focus rings, keyboard step, tab order, tooltip/accessible names and alternate text input for all drag-controlled values.
- All controls support `enabled`, `disabled` (with why), `hover`, `pressed`, `focused`, `active`, `warning`, `error`. Tooltips never substitute for functional labels.
- Handle dark-mode? VOX theme remains dark 1.0; additional skins can be data-only theme variants later with deterministic token changes. Do not let theme change VST3 automation IDs.

## 8. Visual + functional acceptance (hard release gates)

| Gate | Required proof |
|---|---|
| Skin | No default JUCE buttons, standard blue slider/popup or OS-form visual leaks in the actual editor (including dialogs hosted by plugin where possible) |
| Automation | Moving every exposed knob produces proper begin/change/end gestures, host writes automation and restores it; DAW automation updates control without recursion |
| Structural | Curve nodes, pad mappings, sample slices and modulation routes actually change DSP and survive save/reopen; multi-node undo/redo correct |
| Telemetry | Meter equals actual processed peak/RMS envelope within documented decay; event/playhead/voice counts read real snapshots; 0 UI-origin audio-thread allocations |
| Layout | screenshot-based Visual Gate at 1180×760 and 1500×920, scaling 100/125/150/200%, no invisible/overlapping controls |
| Lifecycle | Editor open/close repeatedly during playback and DAW project load, no UAF; modal/popups don't hang DAW; standalone not required for phase 1 |
| Performance | UI closed vs open measured callback timings; no data locks/blocking calls in callback, bounded repaints, no continuous drawing when hidden |
| Consistency | Same knobs/buttons/tabs, keyboard behavior, typography and spacing across all 3 plugins; different product pages are intentional not divergent skins |

## 9. How Claude should implement the skin (task decomposition)

1. **UI-00:** audit `libs/vox-ui` for hidden Rack dependencies, API surfaces and current verified versions; make a minimal separate test harness/editor that uses old theme without Rack.
2. **UI-01:** finalize shared `Theme`, token color/font/spacing specs and semantic component states; compile documented `LookAndFeel` overrides.
3. **UI-02:** implement `VoxKnob`/`VoxSelector`/`VoxButton`/`VoxToggle`/`VoxMeter` with correct JUCE APVTS and ParameterAttachment binding, toolbar and dialogs.
4. **UI-03:** implement responsive scaffold, header/preset browser/tab/page composition + basic DPI and keyboard acceptance.
5. **UI-04:** add waveform editor, curve timeline, step grid, XY and modulation matrix as genuine typed-state editors with undo/redo and nonblocking DSP publication.
6. **UI-05:** product-specific composition for Drums, Synthesis and FX with **exhaustive control→host parameter/Program→DSP audit matrix**, Visual Gate, real DAW screenshots, UI regression and performance tests.

The existing default JUCE widgets are **not painted over with a disconnected SVG**. The functional widgets stay connected to actual parameters; only drawing and complex editing are custom. This is what makes an original ULTIMA VOX MUSIC UI fast to develop and trustworthy in a DAW.
