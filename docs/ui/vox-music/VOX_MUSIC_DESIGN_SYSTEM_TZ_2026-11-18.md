# ТЕХНИЧЕСКОЕ ЗАДАНИЕ — VOX MUSIC DESIGN SYSTEM 1.0

**Версия документа:** 1.0 / 18.11.2026  
**Репозиторий:** https://github.com/ultima-vox/vox-electronic-engine  
**Целевые продукты:** VOX Drums, VOX FX, VOX Synth (следующий продукт линейки).  
**Платформа:** Windows 10/11 x64, VST3, C++20, JUCE 9.0.1, Cubase 12–15, REAPER.  
**Предмет:** от инженерного аудита и визуального контракта до разработанной общей UI-библиотеки, интеграции в два действующих инструмента, тестирования и готовности к релизу.

> Статус этого текста: **предлагаемое единое техническое задание** на основе проверенных источников, а не заявление о выполненной работе. Предложения сверх существующих спецификаций помечены «новое требование ТЗ» и не подменяют утверждённые решения владельца. Никакие рисунки, изображения, mockup или визуальные прототипы не создаются без отдельной команды владельца «нарисуй».

## 0. Обязательные источники, приоритет и проверенная отправная точка

**Приоритет обязательств:** актуальное явное решение владельца → утверждённая спецификация продукта и issue #127 → действующие контракты идентичности/состояния/DSP → этот дизайн-системный контракт → исторические DS-0 документы и образцы → предпочтение разработчика. Сторонние дизайн-рекомендации не имеют права отменять установленные продуктовые требования.

**Авторитетные исходники:**

1. [#127 — утверждённый VOX Drums UI/UX v3](https://github.com/ultima-vox/vox-electronic-engine/issues/127): гибкий dockable workstation, один Pad Rack и Mixer, отдельные Select/Audition, спецификация функциональных панелей и критерии приёмки. Визуальный стиль hardware-inspired согласован как направление, **окончательное утверждение визуала ещё требуется до нового Drums UI C++**.
2. [#135 — hardware reference](https://github.com/ultima-vox/vox-electronic-engine/pull/135), [HARDWARE_REFERENCE.md](https://github.com/ultima-vox/vox-electronic-engine/blob/design/vox-drums-hardware-reference/docs/ultima-vox-music/drums/ux-v3/HARDWARE_REFERENCE.md): источник визуальных подсказок, не runtime HTML и не источник реальных данных DSP.
3. [03_VOX_DRUMS_SPEC.md](https://github.com/ultima-vox/vox-electronic-engine/blob/integration/vox-drums-1.0/docs/ultima-vox-music/03_VOX_DRUMS_SPEC.md), [04_VOX_FX_SPEC.md](https://github.com/ultima-vox/vox-electronic-engine/blob/arena/d9776bd9-vox-electronic-engine/docs/ultima-vox-music/04_VOX_FX_SPEC.md), [05_ULTIMA_VOX_UI_JUCE_SKIN.md](https://github.com/ultima-vox/vox-electronic-engine/blob/integration/vox-drums-1.0/docs/ultima-vox-music/05_ULTIMA_VOX_UI_JUCE_SKIN.md).
4. Действующая общая библиотека [libs/vox-ui](https://github.com/ultima-vox/vox-electronic-engine/tree/integration/vox-drums-1.0/libs/vox-ui), `Tokens.h`, `Typography.h`, `VoxLookAndFeel.*`, существующие компоненты, `showcase`.
5. **Наследие для выборочного reuse, НЕ готовый merge:** `feat/ds-0-design-system-freeze` и документы `docs/UI_DESIGN_SYSTEM_FREEZE.md`, `UI_FOUNDATIONS_TOKENS_SPEC.md`, `UI_INTERACTION_STATE_SPEC.md`, `UI_LAYOUT_RESPONSIVE_SPEC.md`, `UI_ACCESSIBILITY_SPEC.md`, `UI_ACCEPTANCE_MATRIX.md`, `UI_COMPONENT_CATALOG*.md`, `UI_PRODUCTION_BACKLOG.md`. Ветка **diverged** от нынешней `integration/vox-drums-1.0`; требования применяем после сопоставления, C++ файлы вслепую не переносим.
6. [VOX Drums #141](https://github.com/ultima-vox/vox-electronic-engine/pull/141), [VOX Drums StyleLibrary #153](https://github.com/ultima-vox/vox-electronic-engine/pull/153), [VOX FX #156](https://github.com/ultima-vox/vox-electronic-engine/pull/156). Проверка кода: Drums integration `ef13daa3887dbd6079054384b04be7b65849d9ef`; FX PR #156 `efe909b0b93e2a2ec3fe4919ef4068e2c1edc310` (на 18.11.2026; перепроверять live HEAD перед реализацией). Drums и FX **Draft/HOLD для релиза**.
7. Внешние инженерные ориентиры: [JUCE ParameterAttachment](https://docs.juce.com/master/classjuce_1_1ParameterAttachment.html), [JUCE AccessibilityHandler](https://docs.juce.com/master/classjuce_1_1AccessibilityHandler.html), [VST3 Parameters and Automation](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Parameters%2BAutomation/Index.html), [WCAG 2.2](https://www.w3.org/TR/WCAG22/). WCAG применяется как **целевой ориентир доступности**, а не как необоснованное юридическое заявление о сертификации VST3.

### Проверенная реализация на момент составления ТЗ

- В `libs/vox-ui/VoxComponents.h` импортируются **только** `VoxPanel`, `VoxKnob`, `VoxButton`, `VoxComboBox`, `VoxTabBar`, `VoxSectionHeader`. Библиотека имеет токены, типографику, LookAndFeel и showcase. Унифицированных общих `VoxFader`, `VoxPad`, `VoxKeyboard`, `VoxMeter`, `VoxWaveform`, `VoxPresetBrowser`, `VoxDockWorkspace` в этом публичном списке нет. Отдельные примитивы или прототипы внутри продуктов не следует ошибочно считать общей библиотекой.
- В Drums `products/drums/DrumsEditor.cpp` — **8-страничный редактор**, а нажатие `PadButton` выполняет и `selectPad(pad)`, и `audition(pad,110)`. Это **прямое расхождение с #127**. В `DrumsPages.cpp` есть параметры, генератор, микшер и измерители как доноры, но нет утверждённого докируемого workspace и общего вертикального fader-компонента.
- В FX #156 существуют **12 предварительных семейств DSP, 4 слоя, Glitch и Granular, частичный Program/генератор, четыре слота с ограниченным набором DSP-типов, отдельные тестовые библиотеки MIDI/WAV**, однако многое не подключено к пользовательским процессам. `FxEditor.cpp` — промежуточная единая панель, не полный набор рабочих пространств. Наличие C++ функций **не подтверждает UI/DAW-приёмку**.

## 1. Цель продукта и критерий «закончено»

Создать единую, самостоятельную и расширяемую дизайн-систему **VOX MUSIC**, эстетика — оригинальный профессиональный электронный музыкальный инструмент: тактильные органы управления, строгая геометрия, плотность без хаоса, реальная обратная связь, свой узнаваемый язык. Не копировать интерфейсы MPC, Maschine, Serum, Vital, Groove Agent и т. п.; коммерческие продукты можно изучать для эвристик, но не воспроизводить их защищённые материалы/компоновки.

**Результат:** одна `vox-ui` библиотека, один комплект семантических токенов и контрактов взаимодействия, библиотека тестируемых общих компонентов, демонстрационный JUCE-showcase; законченный Drums UI по #127; законченный FX UI по 04-спецификации; готовая общая клавиатура/музыкальные контролы для будущего Synth; объективные Windows тесты и единовременная финальная ручная DAW-приёмка каждого готового плагина.

**Не входит в этот UI-проект автоматически:** переписывание DSP, новый Rack/Kontakt-подобный хост, произвольная переразметка host ParameterID, новый VST3 MIDI output, сетевые/neural сервисы, HTML/WebView/Electron, внешняя обёртка вокруг JUCE, неутверждённая генерация картинок и спонтанное объединение рабочих веток. **Нужная для правдивого интерфейса телеметрия/контроллер** — минимальный отдельно обоснованный контракт, без изменения звука.

### Главные UX-принципы

1. **Музыкальная задача прежде экрана.** Одно действие — понятный эффект, минимум переключений, соседство связанных контролов.
2. **Каждый визуальный контрол привязан к реальному параметру, действию или диагностике.** Никаких нарисованных фейдеров, спектров, клавиш и VU без данных.
3. **Одно значение — одна истина.** UI не дублирует DSP state, host automation, selection или meter ownership.
4. **Визуальная интенсивность ≠ клиппинг.** Parameter value, MIDI velocity, DSP audio peak, post-host clipping — разные данные и разные индикаторы.
5. **Все существенные операции доступны клавиатурой** и имеют видимый фокус; цвет не единственный признак.
6. **Никаких промежуточных UI-«релизов» в DAW.** Автоматическая проверка во время работы, одна финальная ручная проверка владельцем после полной сборки.

## 2. Архитектура: общая библиотека и продуктовые UI

```text
VOX MUSIC SEMANTIC TOKENS / THEME / TYPOGRAPHY
               │
      libs/vox-ui (JUCE GUI only)
       ├─ control primitives
       ├─ form / input / feedback
       ├─ graph / meter / waveform grammar
       ├─ navigation / layout / docking primitives
       └─ showcase + visual gate / accessibility tests
               ▲
       ┌───────┼──────────────┐
products/drums UI    products/fx UI    products/synth UI
       │                   │             │
product controllers  product controllers  product controllers
       │                   │             │
     DSP/state          DSP/state       DSP/state
```

- Общая `vox-ui` **не имеет зависимостей** от Drums, FX, Synth DSP, Processor, Rack, Plug-in FUID, kit/Program schemas. Общее ядро не определяет, как работает Song или Granular: их модели принадлежат продукту.
- Product editor создаёт JUCE control с тематическим `LookAndFeel`, добавляет штатный attachment/собственный адаптер к **существующему** параметру или к строго типизированному product controller.
- Shared компоненты **пассивно получают** immutable/скопированные display snapshots. UI не читает изменяемые голоса из audio callback и не модифицирует host state в `paint()`.
- На входных контрактах должны быть: `ParameterBinding`, `DisplayValue/Unit`, `ControlState`, `TelemetrySnapshot`, `SelectionContext`, `ValidatedEdit/Undo`. Это **проектные интерфейсы для реализации**, не утверждение о существующих классах/API.
- Обязательна таблица ownership/lifetime: `AudioProcessor` владеет DSP и host parameters; controller — структурными документами/Undo; JUCE message thread — компонентами и file dialogs; audio thread — подготовленными DSP snapshot; никаких обратных владений.
- `vox_ui_showcase` и **реальный редактор каждого плагина** служат разными доказательствами: showcase доказывает локальный компонент, product/DAW-тест доказывает настоящее поведение.

## 3. Визуальный язык, токены и геометрия

### 3.1 Единые семантические темы

**Сохраняем текущие репозиторные цвета как начальную базу** (перед окончательным визуальным утверждением проверить фактические значения `Tokens.h`):

| Семантика | Начальный токен | Назначение |
|---|---|---|
| `background` | `#06121D` | основа редактора |
| `panel` | `#0B1925` | рабочие секции |
| `panelRaised` | `#102230` | активная/поднятая поверхность |
| `graph` | `#051019` | аудиоредактор/временная шкала |
| `border` | `#1D3B50` | структура и разделение |
| `accent` | `#00DDF5` | семантический акцент/focus в базовой теме |
| `text` | `#E9F3FA` | основной текст |
| `textSecondary` | `#9AB2C5` | вторичный текст |
| `warning` | `#E3B341` | предупреждение |
| `danger` | `#E4425D` | реальная ошибка/перегрузка |

**Идентичность:** Drums — copper/coral `#EF805D`; FX — aqua `#16CDBA`; Synth — electric violet `#9668F2` как предложения из `05_ULTIMA_VOX_UI_JUCE_SKIN.md`. Они не переопределяют семантику `error`, `focus`, `mute`, `modulated`, `clip`. Все цвета задаются токенами, без `Colour::fromString()` по всему продукту.

**Физические метафоры:** матовое тёмное шасси, мягко обработанный металл/металлические ручки, резиновые ударные поверхности, утопленные направляющие фейдеров, тонкая гравировка, функциональная подсветка по состоянию. Приоритет — читаемость и отклик. Запрещены текстурный шум/обои, тяжелые градиенты на каждом кадре, огромные свечения, имитация реального физического устройства в ущерб работе.

### 3.2 Система измерений

- Сетка **4 логических px**: отступы 4/8/12/16/20/24/32. Использовать semantic-token, не локальные числа по каждому экрану.
- Радиусы 4/6/8; линии 1/1.5/2; логические иконки 16/20/24, область клика отдельно от рисунка.
- Начальные размеры ручек **36 / 48 / 64 logical px**, аппаратное направление допускает крупный главный dial ~52 px, если отдельный токен/контракт утверждён. Общие контрольные высоты 28–32 px; вкладки ~36 px.
- Текст: instrument title ~26 px; section 14; label 12; значения 11–14, при наличии функциональной важности — не мельче проверенного минимума. Числа tabular, единицы Hz/kHz, ms/s, dB, %, semitones, BPM, steps/bars, pan L/R.
- Проверить **контраст основного текста ≥4.5:1**, крупного текста ≥3:1, важных графических границ/focus ≥3:1, целевой hit-area как минимум **24×24 логических px** и предпочтительно **28×28** для компактных иконок; приоритет — 32–40+ px для частых действий. Это проектные пороги на основе WCAG 2.2; фактический тест на экране обязателен.
- Primary UI states: Normal, Hover, Pressed, Focused, Selected, Active, Disabled, ReadOnly, Unavailable, Warning, Error, Automated, Modulated, Bypassed, Dirty, Dragging, DropValid/Invalid. **Disabled, ReadOnly и Unavailable — не одно состояние**.
- Движения: ≤80/140/220 ms для коротких *переходов состояния*, без постоянной декоративной анимации. Аудиометры движутся по данным аудио, не по анимационному таймеру.

### 3.3 Семантика величин и измерений

- **Unipolar** (velocity, drive, send, depth): 0–100% с видимым фактическим числом; прогресс-интенсивность разрешается только как значение параметра. Утверждённая гамма «зелёный→жёлтый→оранжевый→красный» **не означает CLIP**.
- **Bipolar** (Pan, Pitch, Mod Depth): видимый center/zero, оба направления, `0`, `L/R`, `± st` и корректная нулевая зона.
- **Logarithmic/frequency:** Cutoff и Time конвертируются согласно физическим единицам и нормализации существующего параметра; не линейная шкала Hz, если модель нелинейна.
- **Audio peak** — измеренное значение с указанной точкой сигнальной цепи, decay/hold и единицами. **OVER 0 dBFS / clip risk** — отдельная явно подписанная latched индикация, никогда не выдумывать подтверждённое *post-host clipping*.
- **Automation base** и **modulation result**: исходная позиция/значение параметра видна постоянно, внешнее кольцо показывает модуляцию, возможный bipolar range и несколько назначений различимы.

## 4. Контракты компонентов общей библиотеки

Каждый компонент обязан иметь карточку: имя, семантическая роль, варианты размеров, valid states, binding ownership, события/gesture, keyboard, accessibility, layout minimum, screenshots, unit/integration tests, статус `Concept → Prototype → Implemented → Production` либо документированное `Deferred`.

| Компонент / семейство | Обязательное поведение | Где используется |
|---|---|---|
| `VoxKnob`, `VoxEncoder` | 270° ось, физ. единица, начальное/текущее значение, default, fine drag, числовой ввод, модуляционный диапазон, host gesture | все продукты |
| `VoxFader`, `VoxLinearSlider` | **вертикальный фейдер для громкости микшера**, утопленный трек, разделённые gain/rms/clip, keyboard, baseline dB | Drums Mixer, FX outputs, Synth Mix |
| `VoxPad` / `VoxPadGrid` | 4×4 rubber, отдельные SELECT/AUDITION, MIDI note/velocity/hit/peak/mute/solo/missing, hit area, keyboard | Drums; уменьшенный trigger-pad FX только при реальном использовании |
| `VoxKeyboard` | клавиши, note ownership, octave markers, external-MIDI and UI-trigger states, sustain/velocity policy, mouse vs touch, focus | Synth; FX trigger/pitch preview |
| `VoxButton`, `VoxToggle`, `VoxSwitch`, `VoxIconButton`, `VoxSegmented` | однозначные momentary/latching semantics, enabled/unavailable, pressed/focus states, deferred actions busy/dirty | все |
| `VoxValueField`, `VoxNumericInput`, `VoxComboBox`, `VoxSearchField` | прямой ввод, unit parse/format, strict validation, Enter/escape, missing-choice, fuzzy/clear search | все |
| `VoxMeter`, `VoxPeakMeter`, `VoxStatusIndicator` | реальные snapshots, stale/no-data, peak hold, overflow status separate, no guessed RMS | все |
| `VoxGraph`, `VoxEnvelopeEditor`, `VoxCurveEditor` | оси, grid, snapping, 2–16 nodes где требуется, drag with undo, keyboard edit, zoom, per-point normalization | FX timeline, Drums envelopes, Synth modulation |
| `VoxWaveform`, `VoxSampleRange` | peak pyramid из non-RT worker, zero crossings, real A/B sample, handles, missing asset | Drums, FX granular, Synth sampler при наличии |
| `VoxXYPad`, `VoxModRing`, `VoxModMatrixView` | два связных параметра, multi-route, base/mod overlay, drag и keyboard alternative, type-safe mappings | FX, Synth |
| `VoxStepGrid`, `VoxTimeline`, `VoxPianoRoll` | grid/zoom/scroll, multi-select, velocity/ratchet/microtime, edit/playhead separation, accurate visible time ruler | Drums, FX, Synth |
| `VoxDockWorkspace`, `VoxSplitter`, `VoxPanelHeader` | ограничения зоны docking, save/restore named layout, keyboard navigation, safe fallback | **Drums**, optional reuse later |
| `VoxPresetBrowser`, `VoxFileStatus`, `VoxDropTarget` | factory/user, search/tag/favorites, dirty/compare, version and missing asset, import/export progress/errors | Drums, FX, Synth |
| `VoxPopover`, `VoxDialog`, `VoxTooltip`, `VoxToast`, `VoxBanner` | focus trap/restore, keyboard Escape, error recovery, no blocking realtime actions | все |
| `VoxLevelStrip`, `VoxRoutingIndicator` | совместные meters/fader/pan/sends/solo/mute/out, связь с host enabled-bus и реальной routing model | Drums; FX Process routing при необходимости |

**Статусы:** названия выше являются **контрактом целевой библиотеки**, а не утверждением, что такие C++ классы уже существуют. Реальные имена и повторное использование уточняются аудитом `libs/vox-ui` и текущего DS-0 каталога. `VoxPadGrid`, `VoxKeyboard`, `VoxPianoRoll` не должны требовать создания новых DSP/DAW возможностей без отдельного утверждённого продуктового потребителя.

## 5. Подробная интеракция органов управления

### 5.1 Knob / Encoder

- Внешнее **тактильное кольцо** и внутренняя указательная риска, нет ложной «металлической» тени, скрывающей позицию. Для normal/large ручек постоянные label + value + unit; компактная — value доступен рядом/по фокусу.
- Движение мышью преимущественно **вертикально**, независимо от движения указателя по окружности; `Shift+drag` — единый fine mode, с чувствительностью в контракте; keyboard arrows/standard page increments и direct numeric entry. Не назначать случайно wheel на ручку при прокрутке панели; колесо — только explicit policy.
- Double-click reset — к **реальному factory/default параметра**, не к декоративному 50%. Bypass ручки не меняет host value; value состояние read-only при unsupported.
- Автоматизируемый параметр: начало пользовательского gesture → изменения → конец gesture; DAW automation изменяет отображение без рекурсии. Для кастомного компонента — настоящий JUCE `juce::ParameterAttachment` (с документированными gesture методами), иначе `SliderAttachment`/`SliderParameterAttachment` по существующей реализации.
- MIDI learn показывать только при **реальном** контроллере learn и тесте связи. Модуляционное внешнее кольцо отражает диапазон, а не копирует base arc.

### 5.2 Fader

- **Громкость и микширование — вертикальные фейдеры** с утопленным треком и контрастным колпачком. Отдельно meter, peak-hold и latched overload; не превращать peak meter в шкалу отправляемого значения.
- 0 dB unity и диапазон dB определяются существующим `ParameterID` и нормализацией. Если в движке параметр линейный gain 0..1, UI конвертирует представление документированно; не менять звук и automation map ради произвольной dB шкалы.
- Fader поддерживает double-click default, fine, keyboard, numeric edit; `Mute/Solo` отдельные кнопки. На узком экране — bank/scroll, не 16 микрофейдеров.

### 5.3 Pad и drum-velocity

- Ровно 16 пэдов, истинная 4×4 матрица, одинаковые почти квадратные rubber faces. Большая зона **AUDITION** и отдельный очевидный **SELECT** (настройка на выбранный pad без звука). Политика keyboard: Tab/стрелки для selection, Enter/Space audition только когда pad focused в ожидаемом режиме; не перехватывать Cubase transport.
- **Velocity от MIDI или внутреннего секвенсора** — измеренное событие; mouse audition: явно заданная velocity или определённая жестом чувствительность, не притворяться pressure sensing обычной мыши. Вспышка velocity и акустический `peak` — разные индикаторы.
- Пэд показывает note, name, selected, sounding, mute, solo, routing, error/missing sample. При отсутствии телеметрии source/velocity — объявить unavailable, не генерировать «рандомную активность».
- Один shared telemetry coordinator делает `takePadMeter`/`takeMainMeter`/`takeHitMask` **один раз** за тик и рассылает неизменяемые snapshots. Измерение per-hit velocity и per-bus clip требует минимального дополнительного безопасного processor-контракта, с независимой ревизией DSP tap-point.

### 5.4 Keyboard

- Общая `VoxKeyboard` как **синхронизированный performance control**, а не piano-roll, изображение клавиш или фейковый выход MIDI из VST3. Масштаб 1–8 октав (фактический минимум/максимум выбирает продукт), white/black geometry и читаемая нота/октава.
- Pointer Down / Move / Up, уход мыши/потеря focus, повторная нота, sustain, Panic, внешний MIDI и UI audition — раздельная ownership policy, никакой зависшей ноты. При drag между клавишами завершить старую и начать новую согласно проверенному trigger contract; с multi-pointer только если реально поддерживается.
- Не трактовать позицию мыши как polyphonic aftertouch или MPE, если DSP/host не поддерживает. Fixed velocity и selectable audition velocity в явном виде. Для FX one-shot — визуализация «событие запущено», не притворяться note-held/gate. Для Synth — полноценный note-on/off в продуктовой модели; продукт решает, нужно ли отображать MIDI channel/MPE.
- Клавиатурная альтернатива через focus + Enter/Space / документированные безопасные клавиши, без перехвата глобальных DAW shortcut. Широкая клавиатура горизонтально scroll, не сжатие белых клавиш до 5 px.

### 5.5 Graph / Waveform / Step / Matrix

- Единая ось времени: реальные seconds/bars/ticks и region zoom; snap имеет отчётливые маркеры и выключаемое состояние. Узлы перемещаются с visual preview, записываются один раз в Undo-группу по завершении drag; Escape — revert.
- Editable curve: число узлов, shape, duplicate-time policy, диапазон и units строго соответствуют продукту (у FX — до 20 кривых и 2–16 узлов; у Drums — только реально поддерживаемые envelope models).
- Waveform: peak pyramid, no synchronous file decode in paint. Trim start/end, sample A/B, reverse/loop только где реализовано и валидация неразрушительна.
- Mod matrix: показывает source, destination, signed depth, transform и smoothing; automation base сохраняется; не редактировать host automation втайне под «модуляцией».
- Timeline grid: multi-selection, copy/duplicate, undo/redo, snapping, zoom, drag cancellation, note/playhead and playback ownership; рендер максимально использует стабильные snapshot/data caches.

## 6. Привязка к движку, threading и состояние

**Host automatable scalar:** стабильный ParameterID/type/range/default/normalization → штатный JUCE attachment → host gesture → bounded DSP parameter smoothing. Иерархия: **host parameter base + Program structural state + modulation evaluated value**. Нельзя хранить несколько «настоящих» значений одного base-параметра в разных страницах.

**Structural state:** pad note/output/model/sample, Song/pattern, FX family/type, curves/routes/program/sample hash, named workspace layout — редактируется и проверяется на controller/message/worker threads; audio thread получает подготовленный snapshot без блокировки и выделения памяти. Никакого чтения изменяемых UI `ValueTree`/`std::vector`/`shared_ptr` со стороны `processBlock` без корректного handoff/lifetime-контракта.

**Telemetry:** DSP → atomic/lock-free bounded POD snapshot → UI coordinator на 20–30 Hz → заинтересованные widgets. При жесте редактирования графика допустим локальный repaint 30–60 Hz; статические области только по изменению. Не использовать `paint()` или timer для вычисления DSP либо файлового I/O.

**State ownership:** аудиосостояние хранится в версиях product state. Dock layout — UI-only формат с safe default, schema version и раздельным именованным workspace preset. Для Drums owner подтвердил отсутствие выпущенных прежних пользовательских проектов: **не создавать dual-write DUIS v1/новый формат только ради совместимости с неопубликованным прототипом**; решить единый формат по #127/§14 #130. Для FX — versioned `FPRM/FFAM/FPRG/FXSL` и будущие секции, совместимые с уже утверждённой картой и безопасными upgrade/migration в текущем проекте; не перезаписывать неизвестные данные молча.

**Отзывчивость:** UI не должен блокировать audio callback при поиске файлов, генерации пресетов, экспорте MIDI/WAV, preset load, window resize или обновлении meters. При длительной задаче реальный progress/cancel/Error state, не пустой экран.

## 7. VOX Drums — обязательный интерфейс и актуальные недостачи

**Сущности продукта:** 16 пэдов, 8 алгоритмов/пэд, Synth/Sample/Layer, user samples A/B, 16-lane patterns, Song 16/32/64 bars, generator **23 styles**, Main+Out2–9, send delay/reverb, kits/pad/pattern/song presets, MIDI drag file, real host automation. Это требования и частично реализованные функции; не переизобретать работающие механизмы.

### 7.1 Утверждённая пространственная модель (без перепроектирования)

- Top chrome: имя, kit/Pattern/Song, честная информация о BPM/position/host, Undo/Redo/Panic, selected context/status. В performance-контексте сохранить существующие **8 macros**, если их фактическая параметрная карта и действия подтверждены: никакой декоративной ручки с названием Macro без функции.
- Side docks: ровно один Pad Rack (4×4 или List 16), один context Inspector (follow/pin), Generator, Library, Routing, Diagnostics. Одна панель — одна модель, без дубликатов в других вкладках.
- Center: максимум два редактора, горизонтальный/вертикальный split: **Step Sequencer, Song Arranger, Full Sound Editor, Sample Editor**, maximize/restore/tab fallback. Панель не скрывает sequencing canvas.
- Bottom: **ровно один Mixer**, Collapsed / Compact / Expanded, изменяемая высота, не overlay. Expanded — все 16 strips с вертикальными faders, Pan/Delay/Reverb sends, реальные meters, Main/FX return, Main/Out 2–9, при узком размере bank/scroll.
- **Именованные layout-presets:** Beatmaking, Composition, Sound Design, Mixing — это **конфигурации** docks, не новые основные страницы. Restore/reset safe.
- `1040×700` usable minimum; 1280×720, 1440×900, 1600×1000, 125/150/200% Windows DPI. Разворачивать/сворачивать переферийные зоны прежде, чем сжимать музыкальную сетку.

### 7.2 Матрица разрывов Drums (на проверенном `ef13daa3`)

| ID | Требование и точный gap | Что подтверждено кодом | Обязательная поставка / проверка |
|---|---|---|---|
| DR-UI-01 P1 | Dock workspace #127 | `DrumsEditor.cpp` всё ещё eight-page `tabs_`/`pages_[0..7]` | Side/center/bottom zones; named layouts, restore, focus and splitter tests |
| DR-UI-02 P1 | **SELECT ≠ AUDITION** | `PadButton::onClick` одновременно `selectPad` и `audition` | Раздельные действия и MIDI ownership; отсутствие случайных hit triggers |
| DR-UI-03 P1 | Hardware Pad Rack 4×4 + List16 | 4×4 pad buttons есть, единый dock/list с отдельным selection contract не найден | 16 равных rubber pads + List, notes/status/missing/mute/solo, keyboard |
| DR-UI-04 P1 | Bottom 16-strip Mixer с вертикальными faders | MixerPage имеет реальные knobs/peak, уровень `drm.kit.volume` подписан как `Main` | Перенести в единственный bottom dock; kit fader явно `Kit Level / All outputs`; не маскировать как Main-only |
| DR-UI-05 P1 | Истинные telemetry velocity/peak/over | `takeHitMask()` отдаёт bits, `takePadMeter` и `takeMainMeter` используют `exchange(0)`; hit-velocity и per-bus over-снимков нет | Единственный UI telemetry coordinator, минимальная проверенная telemetry extension, точные tap points |
| DR-UI-06 P1 | Inspector follow/pin, Quick Controls ↔ Sound Editor | В текущем восьмистраничном UI нет workspace Inspector с per-object pin | Один выбранный pad/step/bar, sync under DAW automation, invalid pin recovery |
| DR-UI-07 P1 | Song editing mode source vs this bar | Song engine/Strip и per-bar editors существуют | 2 явных edit scopes; copy-on-first-real-edit; Undo restores linkage |
| DR-UI-08 P1 | Step parameter lanes под одной временной линейкой | PatternEditor есть, но #127 layout ещё не реализован | Velocity/Probability/Ratchet/Microtiming aligned + multi-select/zoom/undo |
| DR-UI-09 P1 | Full Sound + Sample Editor | Прототипы Sound/Sample в `DrumsEditor` существуют | Synth/Sample/Layer signal flow, waveform trim and actual controllable parameters, no unsupported ADSR |
| DR-UI-10 P1 | Dockable Generator 23 styles | `makeGeneratePage` и 23-style registry в интеграции есть | Докируемый генератор с реальными 8 параметрами, seed/locks/undo/context, без fake actions |
| DR-UI-11 P1 | Drum routing and MIDI transfer | Routing page и Pattern/Song MIDI export code существуют | Честные active-bus/fallback; transferable **editable MIDI** via drag в Cubase/REAPER; no fake insert-to-track |
| DR-UI-12 P1 | Workspace save/reset, resize/DPI | Старый `DUIS` page-based UiState | Единый layout schema, migration policy #127, 4 workspace defaults, complete keyboard navigation |
| DR-UI-13 P1 | Release factory content | `FACTORY_CONTENT.md`: электронные наборы **listening pending**, Rock/Metal **NOT RELEASE-READY** | Musical listening and licensed acoustic samples or honestly scope/exclude non-release-ready packs; actual content manifest |
| DR-UI-14 P1 | Real Windows candidate acceptance | #141 и #153 ещё Draft/HOLD, самостоятельное UI release не доказано | Exact-head suite/build/validator/RT/states/multibus; one owner Cubase/REAPER pass |
| DR-UI-15 P2 | Accurate notes/roles mapping to UI | Pad role mapping и kits есть в engine/state | Label+note/semantic role/genre table with real data and visibility at 4×4/List |
| DR-UI-16 P2 | Accessibility/empty/error states | Current components do not prove full #127 gate | Tab/focus, missing sample, invalid output, no data, warnings, keyboard editing tests |

**Примечание:** этот список — разрывы от утверждённого **релизного UI**, не утверждение, что генератор/Song/мультивыходы отсутствуют в DSP. **UI-разработчик обязан сначала описать карту переноса всех существующих работающих функций** 8 страниц → новые docks без потери возможностей, а не переписать Drums с нуля.

### 7.3 Производственные сценарии Drums, подлежащие end-to-end тесту

1. Открыть проект → выбрать kit → выбрать pad без звука → отдельно audition с правильной velocity → изменить Tune/Decay через knob → проверить автоматизацию и Undo.
2. Создать pattern с шагами и параметрическими lanes → генерация/lock/mutate/fill → A/B → сохранить/восстановить без рассинхронизации.
3. Создать **Song 16/32/64** → Edit Source и Edit This Bar → copy-on-write → full-song MIDI export/drag → открыть в Cubase/REAPER как *редактируемые ноты*.
4. Микшировать 16 пэдов → Main/Out2–9, Mute/Solo, sends/returns/tails, disabled bus fallback и per-bus метры без ложной телеметрии.
5. Открыть WAV A/B → waveform/trim/reverse/velocity layering → missing-file resolution → перенос проекта на другой Windows-PC → диагностика без подмены сэмпла.
6. Поменять layout, pin Inspector, expand Mixer, ресайз и DPI → restore workspace и проект; DSP/Pattern/automation не должны измениться от расположения окон.

## 8. VOX FX — обязательный интерфейс и актуальные недостачи

**Назначение:** самостоятельный MIDI-triggered **VST3 Instrument**, **НЕ** insert audio processor. Реальный Program — 4 слоя A–D, максимум 8 одновременных событий, глобальный максимум 64 grains, длительность 50 ms–32 s seconds/bars, up to 20 curves, 64 modulation routes, 8 macros, 4 shared serial FX processors, 12 synthesis families, 48 curated programs/12 patterns, deterministic generation, offline WAV/MIDI export. Это целевая спецификация, не факт её выполнения.

### 8.1 Требуемые области UX

- **PERFORM:** Program/category/style browser, true Generate/Mutate/A–B, lock/seed, 8 macros, stereo output, trigger keyboard/audition, actual event count/time/peak.
- **DESIGN:** 4 layer cards; each enabled/solo, family, pitch/gain/pan/width/timing, 4 real curves, family-specific controls. All layer settings persist and control actual active DSP.
- **TIMELINE:** seconds/bars, time ruler, 20 editable curves 2–16 points, snap/shape/zoom, multi-select/undo/redo, real preview.
- **MOTION:** 2 LFO, envelope, S&H, ADSR, phase/velocity/follower, typed 64-route matrix, 8 macros with mappings, base vs evaluated overlays.
- **PROCESS:** 4 actual effect slots with real algorithm selector, bypass, wet/dry, amount and type-specific controls, latency and tail reporting.
- **TRIGGER/PATTERN:** 1–4 bars / 1/4–1/32/triplet grid, velocity/probability/ratchet, internal vs host timing, MIDI export trigger events (no fake audio-to-MIDI).
- **PRESETS:** Factory/User ≥48 sound programs and ≥12 patterns, search/favorites/compare/portable samples, serialization, A/B and missing files.
- **ADVANCED:** truthful event/grain count, CPU/memory/tail/clipping, quality, VST3 identity/version and diagnostics; no decorative placeholder.

### 8.2 Матрица разрывов FX (на проверенном `efe909b0`)

| ID | Требование и gap | Код/состояние сейчас | Требуемая поставка |
|---|---|---|---|
| FX-UI-01 P1 | Complete workspace/navigation | Один interim `FxPanel` с knob/family/seed, layer cards и FX types | Нативные PERFORM/DESIGN/TIMELINE/MOTION/PROCESS/TRIGGER/PRESETS/ADVANCED с navigation и control map |
| FX-UI-02 P1 | 20 curves, real editing | Есть CurveEditor для gain и частичные layer settings | Все 4 curves per layer + 4 global, snap/units/undo, headless equivalence to DSP |
| FX-UI-03 P1 | 2 oscillators, 2 filters per layer, family-specific design | 12 предварительных family-рецептов в `NoiseRiser.h` | Актуальные per-family controls + complete DSP agreement, не generic control с меняющимся смыслом |
| FX-UI-04 P1 | Typed modulation integrated | Standalone `Modulation.h` primitives; модуляция не подключена к Program/DSP UI | 64 route controller, 2 LFO/S&H/ADSR/etc, overlays, deterministic state + tests |
| FX-UI-05 P1 | 8 automatable macros | Полноценной macro/controller/visual mapping нет | 8 stable IDs, mappings, base/automation policy and Program recall |
| FX-UI-06 P1 | Complete FX chain | Реальные 4 slots и часть types уже есть; delay/reverb/phaser/flanger/chorus и tails ещё не завершены | Все утверждённые types, true bypass, wet/dry, type-specific controls, quality/tails |
| FX-UI-07 P1 | Factory generator integration | Generate/Mutate задействованы; 48 uncurated seeds и 12 test patterns | Seed + 4-layer full program + fine-grained locks/A-B + curated browser/categorization |
| FX-UI-08 P1 | Host-sync trigger grid | `Pattern.h` экспортирует SMF bytes offline; runtime host-sync и UI нет | Единый real-time scheduler, start/seek/tempo/loop and MIDI export parity, grid edit |
| FX-UI-09 P1 | WAV/MIDI user workflows | Worker-only `OfflineRender.h` / MIDI bytes; нет file/drag UX | Export dialog/drag with progress/cancel/accuracy/content tails, real files and import tests |
| FX-UI-10 P1 | User sample/granular lifecycle | Built-in procedural sample and 64-grain pool | decode worker, safe ownership, hash/relocation/missing sample diagnostics + waveform UI |
| FX-UI-11 P1 | Musical sound and Program correctness | 12 algorithm placeholders/preliminary implementations | Aural, spectral, alias, level and bounded performance per family at sample rates |
| FX-UI-12 P1 | Full host validation | Exact-head Windows qualification not evidenced, prior SEGFAULT tests on older heads | Current exact HEAD MSVC, suite/validator, memory/RT, full DAW acceptance |
| FX-UI-13 P2 | Keyboard/performance feedback | MIDI note triggers existing but no complete shared keyboard | `VoxKeyboard` visual feedback with correct note/panic/velocity and one-shot policy |
| FX-UI-14 P2 | UI accessibility + responsive | Interim fixed layout and basic bindings | 1040×700 graceful fallback, 125/150/200% DPI, focus/labels/numeric values |
| FX-UI-15 P2 | Accurate diagnostics | Current output peak exists; not full profiling/metrics | event/grain/clip/quality/source telemetry, no fake CPU/RMS |

**Контрольное правило:** заголовок `Glitch`, наличие `Granular`, `Generate`, `Effect slot` или файла `Pattern.h` сами по себе не являются доказательством завершённого пользовательского сценария. Проверяется цепочка **параметр/ввод → DSP → аудио/события → Program/host recall → UI**.

## 9. VOX Synth — подготовка дизайн-системы (без преждевременной продуктовой реализации)

Общие `VoxKeyboard`, `VoxKnob`, `VoxXYPad`, `VoxEnvelopeEditor`, `VoxWaveform`, `VoxModMatrixView`, `VoxStepGrid/PianoRoll`, `VoxPresetBrowser` проектировать с тестовыми нейтральными display-data и без зависимостей от одного инструмента. **Точные осцилляторы/режимы Bass/Acid/Synth, арпеджиатор, MPE, клавиатурное владение и параметрные IDs** берутся из действующей спецификации Synth на этапе его разработки; не объявлять их готовыми или обязательными без аудита. VOX Synth не требует отдельной копии `VoxLookAndFeel`.

## 10. Инженерный план разработки от начала до конца

**Одна отдельная зона ответственности UI-команды.** DSP-разработчики Drums/FX продолжают работу. UI-агент не должен переписывать их текущие PR и менять общие processor/state contracts без ревью.

### DS-00 — Аудит и контракт (сначала, без переписывания)

1. Создать сравнительную таблицу исходников: текущий `libs/vox-ui` + archived DS-0 branch + product widgets + визуальные/функциональные требования #127/03/04/05.
2. Снять точные shared API, ownership, базовые цвета/темы, размеры, прикреплённые параметры. Каждый компонент пометить `Production / Implemented / Prototype / Concept / Deferred` **с доказательством**.
3. Выписать полный `UI_BINDING_MATRIX.csv`: `product,workspace,panel,control,actual_source_path,parameterID_or_controller_method,value_domain,unit,default,automation,structural_state,telemetry_source,gesture,reset,undo,status,test_case`. Не выдумывать ID.
4. Сверить несовместимые исторические DS документы и актуальную standalone-архитектуру. Публиковать **один** current-owner source of truth; старые документы пометить donor/historical, не копировать конфликтующие правила.
5. Документально зафиксировать `USER_VISUAL_SIGNOFF_REQUIRED` для Drums #127: визуальное направление принято условно, **до начала нового Drums UI C++ нужен отдельный финальный sign-off**. По просьбе «не рисовать» на этом этапе только текстовые спецификации и инспекция существующих референсов.

**Exit:** матрица, component registry, архитектура, priority P1/P2, закрытые противоречия и проверенные исходные файлы.

### DS-01 — Foundations и компонентные контракты

- Semantic tokens/theme per product, typography, grids, radius, focus/accessibility, scale and responsive helpers.
- Shared input components: knob/encoder, fader, switch/toggle/button/segmented, numeric, combo, tags and structured error states.
- Единая host-gesture/parameter-adapter policy; прямо запретить привязку к несуществующим IDs.
- `vox_ui_showcase`: каждый компонент normal/hover/pressed/focus/disabled/automated/modulated/error; scalable 75–200%.

**Exit:** compilable independent shared library + showcases + unit tests, no Drums/FX DSP dependency, owner-approved visual language.

### DS-02 — Performance / graphical / musical primitives

- Real meter/peak/over; `VoxPad` 4×4 and List; `VoxKeyboard`; timeline/step/curve/waveform/XY/routing/drag grammar; search/preset/file status.
- Dual modes (`read-only display` / `editable via controller`) where valid; immutable data snapshots, no paint-time DSP.
- Автоматический visual gate: screenshot naming convention, reference SHA, no accidental fallback to default JUCE skin.

**Exit:** component tests/accessibility/performance and example in showcase for every shared primitive, one reuse consumer.

### DS-03 — Докируемый VOX Drums UI по #127

- Карта 8 existing pages → side/center/bottom dock components. **Не удалять ни одну действующую функцию** без проверенного replacement.
- Compose Pad Rack 4×4/List, Inspector/Pinned, centered Step+Song+Sound+Sample, one bottom mixer, real Generator/Routing/Presets/Diagnostics, workspace layouts.
- Минимальная нужная телеметрия только после Architecture Guard; никаких изменений drum الصوتового пути ради скина.
- В продукте tests: audio unaffected, all 16 pads, 23 styles, 9 buses, 16/32/64 Song, MIDI drag, automation, presets, hover/focus/resize/layout restore.

**Exit:** полноценный нативный UI + реальные данные + exact-HEAD CI + visual owner acceptance, но всё ещё Draft/HOLD до полного продукта.

### DS-04 — Полный VOX FX UI

- Compose Perform/Design/Timeline/Motion/Process/Trigger/Presets/Advanced поверх действующего `products/fx` и общего `vox-ui`.
- Визуализировать только настоящий Program state. Нельзя дублировать движок, создавать новую FX identity или обходить установленные parameter IDs.
- Полные пользовательские workflows: generate/mutate/locks/A-B, layer design, curves, modulation/macros, FX, host-synced patterns, WAV/MIDI, samples, presets, diagnostics. Недостающий DSP возвращается владельцу FX PR как конкретное требование, не заменяется UI-муляжом.

**Exit:** все FX workspaces functional, Windows CI/release gate, готовность к единой DAW-приёмке.

### DS-05 — Production hardening

- Полные tests без P0/P1; exact-head Windows x64 CI; Steinberg validator; checker against approved IDs, release manifest; output hashes.
- Сравнение UI controls с binding matrix: **100% видимых интерактивных контролов имеют рабочую цель**; 0 fake controls.
- Smoke screenshots of **actual JUCE editors** at Drums 1040×700, 1280×720, 1440×900, 1600×1000, and 100/125/150/200% Windows DPI; focus/hover/pressed/error/missing. For FX define same usability matrix and verify product-specific compact fallback.
- Under keyboard-only navigation all essential operations are reachable and pass focus order/semantic role/visible focus checks. Contrast/hit areas tested on actual assets.
- Host automation and program restore tested after editor close/reopen, host changes, undo/redo, resize and offline bounce. UI CPU/repaint measured with real meters/graphs, multiple plugin instances and 96 kHz; final thresholds approved from measured budget rather than invented universal CPU guarantee.
- Final 1× owner Cubase/REAPER manual acceptance when **the full product candidate** is complete, followed by independent TL approval. No merge to `main` until authorized.

## 11. Рекомендуемая структура Git и параллельная работа

- Новая **отдельная** UI/DS ветка от проверенного standalone-suite baseline (`next/standalone-suite`), один основной Draft PR для общей библиотеки. Финальные владельческие решения/визуальный sign-off обязательны до переписывания Drums UI. Если ветка проекта уже получила аналогичный UI PR, переиспользовать её вместо дублирования.
- Drums-specific UI PR только от **текущего** `integration/vox-drums-1.0`, после DS review; не работать в `drm/production-candidate` или Rack. Работа по #153 StyleLibrary и #152 docs независима от UI, не терять её.
- FX-specific UI — в координации с **активным #156** (Arena assigned branch `arena/d9776bd9-vox-electronic-engine`). Передача через согласованный shared-UI commit/контракт; **не force-push, не переключать насильно ветку Arena, не переписывать её параллельно**.
- Миграция `vox-ui` в `next/standalone-suite` допустима **только** после code/contract review и успешных cross-product CI; до этого изолированный Draft. `main` остаётся закрыт для разработки.
- Нельзя смешивать задачу дизайн-системы с полной заменой DSP/микшера. Изменения processor API — узкий отдельный PR или согласованный интерфейс без нарушения производительности. Компонент становится общим после реальной проверки его использования минимум в двух применимых контекстах либо обоснованного базового нейтрального контракта.
- CI: ожидаемые проверки должны выполняться для **точного текущего SHA**; провал Linux syntax check не равен Windows qualification, зелёный старый commit не засчитывается. Новая concurrency-настройка в #155 слита в Drums integration, но в других ветках может отсутствовать: перепроверить её распространение, не удаляя полезные текущие CI evidence.

### Проверка CI — конкретные операции на Windows (после установки закреплённых зависимостей)

Параметры и source-dir для JUCE/VST3 SDK брать **строго** из действующего `.github/workflows/suite.yml`; не скачивать произвольную новую версию SDK и не менять ABI во время UI-работ. На Windows runner с Visual Studio 2022 конфигурация suite по существующему workflow использует:

```powershell
cmake -S . -B build-suite -A x64 -DVOX_BUILD_SUITE=ON -DVOX_BUILD_LEGACY=OFF -DVOX_STRICT_DEPENDENCIES=ON "-DFETCHCONTENT_SOURCE_DIR_JUCE=$env:JUCE_DIR"
cmake --build build-suite --config Release --clean-first --parallel
ctest --test-dir build-suite -C Release -L suite --output-on-failure
```

В полном workflow тесты Drums gain-staging выполняются отдельным параллельным пакетом, Steinberg validator — отдельным обязательным шагом с pinned SDK. Приведённый короткий `ctest` — **не подмена** этих обязательных стадий. Записывать SHA, run URL, число тестов, результаты, binary manifest/sha-256, точную причину FAIL и подтверждение отсутствия P0/P1.

## 12. Обязательный протокол приёмки каждого компонента

Один компонент может получить статус **Production** только при выполнении всех применимых условий:

- Реальная цель и продуктовый владелец; нет копий того же компонента с иным поведением.
- Public API и unit/parameter normalization документированы; требования 1 control ↔ 1 audible/base parameter соблюдены (кроме документированных multi-parameter XY и macro).
- Все допустимые normal/hover/pressed/selected/active/disabled/readOnly/unavailable/automation/modulation/dirty/error states различимы.
- Мышь + клавиатура + fine edit + numeric + reset + focus; доступное имя/роль/значение/единицы; отсутствует ловушка focus и захват DAW shortcut.
- Правильные JUCE host automation gestures, feedback без циклов, state/Undo/redo, record/playback automation из Cubase/REAPER.
- Данные meter/timeline/overlay поступают по безопасному snapshot contract; hidden editor останавливает ненужные repaint timers.
- Responsive/dpi/long localization labels, отсутствует клиппинг редактора и скрытые недоступные кнопки.
- Screenshot реального компонента и реального продукта: default/important state/scale; независимое визуальное ревью после запроса владельца.
- No audio callback allocations, locks, file/network, GUI or object lifetime destruction; accurate host state and real audio tests.
- Не менее одного негативного теста: missing sample/unavailable bus/invalid preset/bad file/invalid program/disabled action/transport edge в зависимости от компонента.
- Exact HEAD Windows CI + VST3 validator и воспроизводимый asset/license/provenance список. Ничего не называть `PASS`, если тест не исполнялся.

## 13. Завершающий чек-лист принимающего техлида

- [ ] Единые токены/LookAndFeel для трёх продуктов; никаких приватных несовместимых цветовых систем и stock JUCE skin на реальных экранах.
- [ ] Завершены input controls (knobs, faders, pads, keyboard, value input, switches); актуальный component registry без ложного Production.
- [ ] Завершены общие graph/timeline/selection/drag/drop/meters/file/preset/responsive/accessibility контракты.
- [ ] Drums — **весь** #127, включая dock/Inspector/bottom faders/4×4/List/select-audition/one source of truth/named layouts и реальную музыкальную функциональность.
- [ ] FX — **все** восемь workspaces и связь со всеми FX-01…FX-07 DSP/pattern/export/state/quality функциями.
- [ ] Cross-plugin тесты контролов, host automation и editor resize; отрицательные тесты, state round trip, telemetry truthfulness.
- [ ] Windows 10/11 x64 CI на конечном SHA, Steinberg validator, репродуцируемый пакет VST3 и SHA-256; 44.1/48/96 kHz, 64/128/512 frame, нагрузка нескольких инстансов.
- [ ] Юридически проверенный factory-content manifest, 48 FX программ/12 паттернов, готовый Drums factory контент и ручное прослушивание.
- [ ] Финальная отдельная визуальная подпись владельца и **один финальный DAW-тест** владельца на готовом продукте.
- [ ] Нет открытых P0/P1, нет фальшивых элементов, непроверенных «PASS», нет changes в `main` без отдельного разрешения.

**Финальный результат для разработки:** versioned design-system specification + token set + component inventory/ownership + shared native JUCE library + product binding matrices + Drums/FX native workspaces + component showcase + screenshot/interaction/accessibility evidence + exact SHA CI/validator evidence + release-gate checklist. Не ограничиваться красивой картинкой или демонстрацией одного контрола.
