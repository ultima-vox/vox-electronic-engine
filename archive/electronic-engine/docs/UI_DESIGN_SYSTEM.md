# VOX ELECTRONIC ENGINE — UI DESIGN SYSTEM v1.0

**Статус:** CANON / обязательная спецификация для UI  
**Назначение:** единая дизайн-система JUCE-интерфейса VST3/Standalone  
**Проект:** `trance-vst-engine`  
**Версия:** 1.0

---

## 1. Цель

Эта спецификация задаёт единый визуальный язык, структуру компонентов и правила реализации интерфейса VOX Electronic Engine.

Основные цели:

- профессиональный внешний вид уровня коммерческого audio software;
- единообразие всех инструментов и вкладок;
- отсутствие локальных «самодельных» стилей;
- масштабируемость UI;
- повторное использование компонентов;
- разделение визуального слоя и DSP;
- отсутствие работы UI в realtime audio thread;
- возможность добавлять новые инструменты без переделки базовой UI-архитектуры.

---

## 2. Визуальное направление

Стиль:

**Dark futuristic / professional electronic-music workstation**

Ключевые признаки:

- глубокий тёмно-синий/чёрный фон;
- холодный cyan как основной accent;
- высокая информационная плотность;
- тонкие границы;
- небольшие радиусы;
- минимум декоративного шума;
- glow используется только точечно;
- интерфейс не должен выглядеть как game HUD;
- интерфейс не должен выглядеть как generic JUCE demo;
- основной акцент — читаемость, контроль и визуальная иерархия.

---

## 3. Design tokens

Все цвета, размеры, интервалы и радиусы должны быть объявлены централизованно.

Рекомендуемый файл:

```text
Source/UI/Design/Tokens.h
```

### 3.1 Цвета

```text
bg.window          #06121D
bg.panel           #0B1925
bg.panelRaised     #102230
bg.control         #0A1722
bg.graph           #051019

border.default     #1D3B50
border.subtle      #122C3D

accent.primary     #00DDF5
accent.hover       #42EEFF
accent.dim         #087B92

text.primary       #E9F3FA
text.secondary     #9AB2C5
text.muted         #587487

danger             #E4425D
success            #32D296
warning            #E3B341
```

### 3.2 Правила цвета

- `accent.primary` используется только для:
  - active state;
  - selected state;
  - текущего значения;
  - важных realtime indicators;
  - active modulation;
  - текущей позиции;
  - выделенного инструмента.
- Не использовать cyan как фон каждого элемента.
- Не вводить новые цвета локально.
- Любой новый цвет должен сначала появиться в `Tokens.h`.

---

## 4. Сетка и интервалы

Базовая единица:

```text
4 px
```

Spacing tokens:

```text
space.1 = 4
space.2 = 8
space.3 = 12
space.4 = 16
space.5 = 20
space.6 = 24
space.8 = 32
```

Правило:

> Все основные размеры, интервалы и отступы должны быть кратны 4 px, если нет технической причины отступить от этого правила.

---

## 5. Радиусы

```text
radius.small   = 4 px
radius.medium  = 6 px
radius.large   = 8 px
```

Не использовать чрезмерно округлённые кнопки и панели.

---

## 6. Базовый размер окна и масштабирование

Базовый reference canvas:

```text
1440 × 1080
```

Поддерживаемые UI scale targets:

```text
75%
100%
125%
150%
200%
```

UI не должен зависеть от абсолютного размера окна.

Запрещено строить весь интерфейс через набор глобальных hardcoded `setBounds(x, y, w, h)`.

---

## 7. Типографика

Используется ограниченный набор уровней.

### Instrument Title

```text
24–28 px
SemiBold
```

### Section Title

```text
13–14 px
SemiBold
UPPERCASE
```

### Control Label

```text
11–12 px
Medium
```

### Value / Secondary

```text
10–11 px
Regular
```

### Правила

- Не вводить случайные размеры шрифта.
- Не использовать более 4 основных уровней типографики без отдельного обоснования.
- Primary text — `text.primary`.
- Labels — `text.secondary`.
- Disabled — `text.muted`.

---

## 8. Компонентная библиотека

Все страницы интерфейса должны строиться из стандартных VOX-компонентов.

Минимальный обязательный набор:

```text
VoxLookAndFeel
VoxPanel
VoxKnob
VoxButton
VoxIconButton
VoxToggle
VoxComboBox
VoxTabBar
VoxMeter
VoxGraph
VoxEnvelopeGraph
VoxFilterGraph
VoxInstrumentSlot
VoxParameterLabel
VoxSectionHeader
VoxKeyboard
VoxTooltip
```

Нельзя создавать локальные визуальные вариации стандартных компонентов без изменения design system.

---

## 9. VoxPanel

Назначение:

- Oscillator;
- Filter;
- Envelope;
- Drive;
- Accent;
- Performance;
- Modulation;
- Matrix;
- Routing;
- Advanced sections.

Стандарт:

```text
background : bg.panel
border     : 1 px border.default
radius     : 6 px
padding    : 12 px
```

---

## 10. VoxKnob

Основной rotary control.

### Размеры

```text
Small   36 × 36
Normal  48 × 48
Large   64 × 64
```

### Состав

- inactive arc;
- active value arc;
- central body;
- indicator;
- label;
- value text;
- optional modulation ring.

### Диапазон дуги

Рекомендуется:

```text
270°
```

Типичная геометрия:

```text
135° → 405°
```

### Цвета

```text
inactive arc   border.default
active arc     accent.primary
indicator      text.primary
modulation     accent.hover / отдельный modulation layer
```

### Состояния

```text
Normal
Hover
Pressed
Focused
Active
Disabled
Modulated
```

### Правила

- Label всегда под или над knob согласно layout contract.
- Значение отображается единообразно.
- Не создавать уникальные knob styles для каждого инструмента.
- Модификация должна быть визуально отделена от основного значения.

---

## 11. VoxComboBox

Стандартная высота:

```text
32 px
```

Стиль:

```text
background  bg.control
border      border.default
radius      4 px
padding     8 px
```

Состояния:

```text
Normal   border.default
Hover    accent.dim
Focused  accent.primary
Disabled text.muted / lowered opacity
```

---

## 12. Buttons

Типы:

```text
Primary
Secondary
Toggle
Danger
Icon
```

Стандартная высота:

```text
32 px
```

### Primary

- акцентная команда;
- используется ограниченно.

### Secondary

- стандартные действия;
- тёмный фон, светлый текст.

### Toggle

- переключаемое состояние;
- active state должен быть очевиден без наведения.

### Danger

Используется для:

```text
Panic
Reset-critical
Destructive action
```

Цвет:

```text
danger
```

---

## 13. VoxTabBar

Используется для:

```text
SOUND
PATTERN
ROUTING
ZONES
MACROS
ADVANCED
```

Высота:

```text
36 px
```

Active:

```text
background  accent.primary
text        bg.window
```

Inactive:

```text
background  bg.control
text        text.secondary
border      border.default
```

---

## 14. Instrument Rack

Компонент:

```text
VoxInstrumentSlot
```

Рекомендуемая высота:

```text
54–58 px
```

Структура:

```text
01 | thumbnail | Psy Bass       CH1 | power
                 Ultima Vox 1.0
```

Состояния:

```text
Active
Inactive
Empty
Disabled
Muted
Soloed
```

### Active

- cyan outline;
- немного более яркий фон;
- номер и power state хорошо различимы.

### Empty

- muted text;
- без thumbnail;
- inactive controls visually reduced.

---

## 15. VoxGraph

Базовый компонент для:

- waveform;
- filter response;
- ADSR;
- modulation;
- envelopes;
- automation preview.

Стиль:

```text
background  bg.graph
grid        subtle / low-opacity
curve       accent.primary
curve width 1.5–2 px
nodes       6–8 px
```

Fill:

- допустим слабый cyan gradient;
- запрещены тяжёлые декоративные заливки.

---

## 16. Envelope graphs

Компонент:

```text
VoxEnvelopeGraph
```

Должен:

- отображать A/D/S/R;
- обновляться при изменении параметров;
- использовать единую геометрию;
- поддерживать interactive points только если это предусмотрено функционально;
- не дублировать DSP-state отдельно от parameter state.

---

## 17. Filter graph

Компонент:

```text
VoxFilterGraph
```

Должен визуализировать:

- cutoff;
- resonance;
- filter type;
- slope.

График должен отражать реальное состояние параметров, а не быть декоративной анимацией.

---

## 18. Piano keyboard

Базовый компонент:

```text
VoxKeyboard
```

Можно использовать `juce::MidiKeyboardComponent` как функциональную основу, но внешний вид должен быть кастомизирован.

Белые клавиши:

```text
#EBEEF0
```

Чёрные:

```text
#081018
```

Pressed:

```text
accent.primary
```

Допускаются подписи:

```text
C1
C2
C3
...
```

---

## 19. Общие состояния компонентов

Каждый интерактивный компонент обязан поддерживать:

```text
Normal
Hover
Pressed
Focused
Active
Disabled
```

Рекомендуемая логика:

```text
Normal:
  border = border.default

Hover:
  border = accent.dim

Focused / Active:
  border = accent.primary

Disabled:
  opacity ≈ 0.35
  text = text.muted
```

Запрещено придумывать состояния локально.

---

## 20. Основная layout-архитектура

Верхний уровень:

```text
PluginEditor
│
├── TopBar
├── InstrumentRack
├── InstrumentHeader
├── MainTabBar
└── ActivePage
```

Main area для `Sound`:

```text
┌──────────────┬───────────────┬──────────────┐
│ Oscillator   │ Filter        │ Envelope     │
├──────────────┼───────────────┼──────────────┤
│ Drive        │ Accent        │ Performance  │
├───────────────────────┬─────────────────────┤
│ Modulation            │ Matrix              │
├─────────────────────────────────────────────┤
│ Keyboard                                    │
└─────────────────────────────────────────────┘
```

---

## 21. Рекомендуемая C++ структура

```text
Source/
└── UI/
    ├── Design/
    │   ├── Tokens.h
    │   ├── Typography.h
    │   └── VoxLookAndFeel.h/.cpp
    │
    ├── Components/
    │   ├── VoxPanel.h/.cpp
    │   ├── VoxKnob.h/.cpp
    │   ├── VoxButton.h/.cpp
    │   ├── VoxComboBox.h/.cpp
    │   ├── VoxTabBar.h/.cpp
    │   ├── VoxGraph.h/.cpp
    │   ├── VoxInstrumentSlot.h/.cpp
    │   ├── VoxKeyboard.h/.cpp
    │   └── ...
    │
    ├── Pages/
    │   ├── SoundPage.h/.cpp
    │   ├── PatternPage.h/.cpp
    │   ├── RoutingPage.h/.cpp
    │   ├── ZonesPage.h/.cpp
    │   ├── MacrosPage.h/.cpp
    │   └── AdvancedPage.h/.cpp
    │
    └── Panels/
        ├── OscillatorPanel.h/.cpp
        ├── FilterPanel.h/.cpp
        ├── EnvelopePanel.h/.cpp
        ├── DrivePanel.h/.cpp
        ├── AccentPanel.h/.cpp
        ├── PerformancePanel.h/.cpp
        ├── ModulationPanel.h/.cpp
        └── MatrixPanel.h/.cpp
```

---

## 22. JUCE implementation rules

### 22.1 LookAndFeel

Вся базовая визуальная кастомизация должна идти через:

```cpp
class VoxLookAndFeel : public juce::LookAndFeel_V4
```

В частности:

```text
drawRotarySlider
drawButtonBackground
drawComboBox
drawToggleButton
drawLinearSlider
drawPopupMenuItem
```

Локальная отрисовка допустима для специализированных компонентов.

### 22.2 Parameter bindings

Параметры UI должны привязываться к параметрам процессора через стандартные attachment-механизмы там, где это применимо.

Пример:

```cpp
juce::AudioProcessorValueTreeState::SliderAttachment
```

UI не должен хранить независимую «копию истины» для параметров DSP.

### 22.3 Realtime safety

UI никогда не должен:

- выполнять тяжёлые вычисления в audio callback;
- брать блокирующие mutex на audio thread;
- выделять память в realtime path;
- читать графические данные напрямую из небезопасного mutable DSP-state;
- инициировать disk I/O из audio thread.

Для визуализации realtime state использовать безопасный bridge:

```text
atomics
lock-free snapshot
timer-driven UI polling
safe message-thread update
```

---

## 23. Layout rules

Запрещено использовать абсолютные координаты как основной подход для всего интерфейса.

Разрешено:

- относительные layout calculations;
- reusable layout helpers;
- nested rectangles;
- flex/grid abstraction;
- пропорциональное распределение;
- scale-aware geometry.

Пример допустимого подхода:

```cpp
auto area = getLocalBounds().reduced(12);
auto row = area.removeFromTop(100);

layoutKnob(cutoff,    row.removeFromLeft(72));
layoutKnob(resonance, row.removeFromLeft(72));
layoutKnob(keyTrack,  row.removeFromLeft(72));
layoutKnob(envAmount, row.removeFromLeft(72));
```

---

## 24. Запрещённые практики

Агент НЕ ДОЛЖЕН:

- добавлять новые цвета без design token;
- использовать разные стили ручек на разных страницах;
- использовать emoji как UI-icons;
- строить весь интерфейс на абсолютных координатах;
- локально менять typography;
- создавать собственные random radii;
- добавлять glow повсеместно;
- использовать decorative gradient без функциональной причины;
- копировать JUCE default look;
- смешивать DSP-код и отрисовку;
- изменять visual language отдельного инструмента;
- создавать новые reusable controls внутри конкретной страницы;
- дублировать parameter state;
- выполнять UI работу в realtime audio thread.

---

## 25. Иконки

Иконки должны быть:

- vector-based;
- единообразными по stroke;
- без emoji;
- без случайной стилистики.

Предпочтительно:

```text
SVG / juce::Drawable
```

Базовая толщина stroke:

```text
1.5–2 px
```

---

## 26. Visual hierarchy

Приоритеты:

1. текущий инструмент;
2. активная вкладка;
3. realtime/critical state;
4. primary parameters;
5. secondary parameters;
6. metadata / hints.

Нельзя делать все элементы одинаково яркими.

---

## 27. Accessibility / usability

Минимальные требования:

- все controls должны иметь tooltip;
- параметр должен иметь текстовое значение;
- critical states нельзя показывать только цветом;
- hit target не должен быть меньше визуального control area;
- disabled control должен быть визуально очевиден;
- drag sensitivity knobs должна быть единообразной;
- double-click reset должен быть одинаков для всех параметрических controls;
- modifier behaviour должен быть глобально согласован.

---

## 28. Interaction rules

Для knobs:

```text
Drag — единый режим по всему UI
Double click — reset to default
Shift + drag — fine adjustment
Mouse wheel — optional, но единообразно
Right click — context / MIDI Learn при наличии
```

Для dropdown:

```text
Click — open
Escape — close
Keyboard navigation — желательно
```

Для tabs:

```text
Single click — switch page
No destructive state change
```

---

## 29. Instrument-page contract

Каждый инструмент обязан предоставлять UI через унифицированный контракт.

Минимально:

```text
Title
Subtitle / engine name
Channel
Preset selector
Power state
Primary page controls
Optional graph
Optional performance controls
Optional modulation section
```

Инструмент не должен менять глобальную layout-систему.

---

## 30. Definition of Done для нового UI-компонента

Компонент считается готовым, если:

- использует design tokens;
- поддерживает все необходимые состояния;
- работает при масштабировании;
- не содержит случайных hardcoded цветов;
- не содержит локальной дублированной темы;
- имеет deterministic layout;
- не зависит от DSP implementation details;
- корректно работает при resize;
- не создаёт realtime-safety проблем;
- имеет минимум один UI test / screenshot test / deterministic rendering check, если инфраструктура проекта позволяет.

---

## 31. Definition of Done для страницы

Страница считается готовой, если:

- собрана только из стандартных VOX-компонентов;
- соответствует общей сетке;
- работает при 75/100/125/150/200%;
- не имеет visual overflow;
- все controls связаны с реальными параметрами;
- значения обновляются в обе стороны;
- active/disabled states корректны;
- отсутствуют элементы-заглушки без явной маркировки;
- отсутствуют fake graphs, не отражающие реальное состояние.

---

## 32. Этапы внедрения

### Phase UI-1 — Foundation

Создать:

```text
Tokens
Typography
VoxLookAndFeel
VoxPanel
VoxKnob
VoxButton
VoxComboBox
VoxTabBar
```

Acceptance:

- отдельный component showcase;
- visual consistency;
- resize works.

### Phase UI-2 — Reference Panel

Полностью реализовать:

```text
FilterPanel
```

Содержимое:

```text
filter type
filter graph
cutoff
resonance
key track
env amount
power
```

Это эталонная панель для всей системы.

### Phase UI-3 — Sound Page

Реализовать:

```text
Oscillator
Filter
Envelope
Drive
Accent
Performance
```

### Phase UI-4 — Modulation + Matrix

Добавить:

```text
Envelope tabs
LFO tabs
Step modulation
Matrix
```

### Phase UI-5 — Instrument Rack

Добавить:

```text
16 slots
active state
empty state
channel
power
instrument identity
```

### Phase UI-6 — Keyboard

Интегрировать:

```text
VoxKeyboard
Pitch
Mod
Velocity controls
MIDI Learn
```

### Phase UI-7 — Remaining Pages

```text
Pattern
Routing
Zones
Macros
Advanced
```

---

## 33. Инструкция агенту

При работе с UI агент обязан:

1. Сначала прочитать этот документ.
2. Не менять design language без отдельного решения.
3. Не создавать локальные альтернативы существующим VOX-компонентам.
4. При необходимости нового компонента:
   - сначала добавить его в component library;
   - затем использовать на страницах.
5. После каждого UI checkpoint:
   - обновить этот документ, если появился новый канонический token/component/rule;
   - не менять существующий CANON молча.
6. Не смешивать функциональный refactor DSP с UI refactor в одном checkpoint без необходимости.
7. Каждый PR должен указывать:
   - какие компоненты добавлены;
   - какие tokens изменены;
   - какие страницы изменены;
   - какие состояния проверены;
   - какие scale factors проверены.
8. Скриншот/рендер после значимого UI PR обязателен.
9. Все отклонения от design system должны быть явно описаны в PR.

---

## 34. Acceptance criteria v1.0

Design System v1.0 считается внедрённой, когда:

- существует единый `Tokens.h`;
- существует единый `VoxLookAndFeel`;
- базовые controls не используют default JUCE appearance;
- `VoxKnob`, `VoxPanel`, `VoxButton`, `VoxComboBox`, `VoxTabBar` переиспользуются;
- минимум одна полноценная панель реализована по стандарту;
- resize и UI scaling не ломают layout;
- отсутствуют локальные случайные стили;
- UI не нарушает realtime safety;
- документация актуализируется вместе с UI checkpoints.

---

## 35. Каноническое правило

> Новый UI-код должен расширять систему, а не обходить её.

Если нужного визуального элемента нет — сначала создаётся стандартный компонент, после чего он используется в продукте.

Это правило является обязательным для дальнейшей разработки интерфейса VOX Electronic Engine.
