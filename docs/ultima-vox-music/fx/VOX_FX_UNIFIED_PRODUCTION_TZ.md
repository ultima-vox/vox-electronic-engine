# VOX FX — ЕДИНОЕ PRODUCTION-ТЗ
## Самостоятельный Trance FX Instrument VST3 · Windows x64 · версия ТЗ 1.0 · 18.11.2026

**Документ:** единый контракт реализации для Arena, проверок TL и приёмки владельца.  
**Целевой PR:** [#156](https://github.com/ultima-vox/vox-electronic-engine/pull/156), рабочая ветка `arena/d9776bd9-vox-electronic-engine`.  
**Проверенная отправная точка кода:** `f4e407fc379cbc36bd447af24e0626dc1c80fec8` (18.11.2026); разработчик ОБЯЗАН проверить актуальный HEAD перед работой.  
**Статус:** техническое задание; перечисленные ниже целевые функции **не объявляются существующими**. PR остаётся **Draft / HOLD / NO MERGE** до выполнения релизных ворот.  
**Владелец DSP и интерфейса FX:** Arena. **Владелец Drums и общего API `libs/vox-ui`:** Codex; совместные изменения согласовывать в GitHub, не работать одновременно в одних общих файлах.

---

# 0. Правила прочтения и приоритет источников

1. Это **единая целевая спецификация VOX FX**, объединяющая функциональный контракт [`04_VOX_FX_SPEC.md`](https://github.com/ultima-vox/vox-electronic-engine/blob/f4e407fc379cbc36bd447af24e0626dc1c80fec8/docs/ultima-vox-music/04_VOX_FX_SPEC.md), дополнения владельца о полной палитре Trance и содержимое обсуждений #156. Там, где прежние материалы ограничены 48 заготовками и 12 паттернами, теперь действует расширенная **цель 600 утверждённых фабричных программ**, а 48/12 — только текущий технический baseline. Не подменять достижение музыкального качества счётчиком.
2. Фактический текущий статус — [`fx/STATUS.md`](https://github.com/ultima-vox/vox-electronic-engine/blob/f4e407fc379cbc36bd447af24e0626dc1c80fec8/docs/ultima-vox-music/fx/STATUS.md). **ТЗ != свидетельство реализации**. Любой пункт предъявлять с кодом, тестами, аудио и доказательством точного SHA.
3. Проектная архитектура — **три самостоятельных Windows x64 VST3**: VOX Drums, VOX FX, VOX Synth; в текущем PR делается ТОЛЬКО FX. Без Rack, `.vins`-хоста, WebView, онлайн-сервисов и нейросетевой генерации.
4. Визуальное направление семейства VOX принято владельцем в [#127](https://github.com/ultima-vox/vox-electronic-engine/issues/127#issuecomment-6980638479); [#135](https://github.com/ultima-vox/vox-electronic-engine/pull/135) — физический визуальный ориентир; [#160](https://github.com/ultima-vox/vox-electronic-engine/pull/160) — **браузерный референс, НЕ runtime**. FX нужны свои экраны и управление, не копия Pad Rack Drums.
5. Порядок и ответственность: Arena продолжает **один существующий #156**, без перезапуска DSP с нуля и без потока микроскопических PR; Codex ведёт Drums #141. Не сливать #156 в `next/standalone-suite` или `main` без отдельной независимой release-приёмки.
6. **Обязательные** требования обозначены `MUST`, допускаемая реализация с выбранным и задокументированным компромиссом — `DESIGN CHOICE`, возможная будущая версия — `BACKLOG`. Любое изменение стабильных существующих идентификаторов и контрактов требует отдельного review. При конфликте безопасности RT, корректности сохранения и новых звуковых функций сначала обеспечить корректность.

# 1. Миссия, пользовательские сценарии и границы продукта

## 1.1. Назначение
VOX FX — **самостоятельный MIDI-triggered stereo VST3 instrument** для создания оригинальных переходных, ударных, психоделических, атмосферных, мелодических и ритмических FX во всём спектре Trance: от мягкого Sunrise/Morning, Dream/Balearic, Vocal и Anthem/Uplifting до Progressive, Tech/Hard/Acid, Goa, Full-On, Forest, Twilight, Dark Psy, Hi-Tech/Psycore. Не концентрироваться только на Psy.

Главный пользовательский путь: **выбрать Style → Sound Role → Play Mode → длительность/тональность → Preview → Generate/Mutate либо выбрать готовый Program → настроить 4 слоя, кривые и эффекты → автоматизировать макросы → сохранить, вывести в DAW/экспортировать**. Любые обещанные кнопки должны реально воздействовать на DSP/состояние.

## 1.2. Входы/выходы
- `MUST`: MIDI input (нотные события с sample offsets, канал/нота/velocity, relevant CC), **стереовыход** Main; автономный синтез DSP и опциональные принадлежащие пользователю WAV-сэмплы. Проигрывание по MIDI в Cubase 12–15 и REAPER.
- `MUST`: внутренний триггер/предпрослушивание и Host Pattern с корректной политикой состояния.
- `MUST`: offline WAV и MIDI-файлы с правдивой семантикой; MIDI представляет **ноты-триггеры**, а не содержание синтезированного FX.
- **НЕ FX 1.0:** обработка входящего аудио другой дорожки, сайдчейн-вход, multibus Rack, скрытая подмена инструмента аудио-процессором. Возможный отдельный «VOX FX Processor» — будущий иной продукт/ID.
- `MUST`: локальная работа без сети и без AI-инференса; никаких сторонних защищённых звуков без права распространения.

## 1.3. Качество пользовательского опыта
- Первая пригодная программа — без настройки 50 ручек.
- Чёткая индикация: какой звук/Program загружен, как он запускается, сколько звучит, синхронизирован ли с хостом, существует ли сэмпл, занят ли генератор, что экспортируется.
- Единицы и границы параметров честные: Hz, ms, s, bars, dB/%, semitones, cents, pan L/R по смыслу.
- При отсутствии транспорта, сэмпла, неподдерживаемом размере/размерности MIDI или перегрузке — явный статус, не тихая «успешная» операция.

# 2. Текущий baseline (доказано кодом, НЕ готовность продукта)

На проверенном SHA существуют:
- 4 слота `NoiseLayer`, 12 предварительных семейств `riser/downlifter/shepard/impact/whoosh/laser/squelch/metallic/texture/formant/glitch/granular`; до 8 активных событий, 4 preallocated retirement fades, до 64 глобальных grains.
- One-shot события с игнорированием обычного NoteOff; 4 слоя хранят **только Gain и Pan curves**: 8 кривых вместо целевых 20.
- 4 последовательных FX-слота; 8 host macros и ограниченные живые маршруты LFO/S&H/Macro преимущественно в Gain/Pan.
- 48 **неотредактированных процедурных seed-программ**, 12 заготовок паттернов; Generate/Mutate; session-only A/B; первая MIDI 4/4 и WAV C4 48kHz PCM24 реализация; WAV импорт с SHA-256/состоянием отсутствия.
- 19 host automatable параметров в [`products/fx/parameters.csv`](https://github.com/ultima-vox/vox-electronic-engine/blob/f4e407fc379cbc36bd447af24e0626dc1c80fec8/products/fx/parameters.csv) — **ID нельзя менять ради UI**.
- Промежуточный `FxPanel`, `LayerPanel`, `CurveEditor`; полных FX-рабочих областей нет.
- На предыдущих Windows runs зарегистрированы **4 настоящих SEGFAULT** (noise_riser/processor/layer/Shepard suite); точный HEAD Windows CI/validator пока не принят. Локальные Linux DSP-тесты и syntax-check JUCE тестов не эквивалентны Windows VST3 PASS.
- В описании PR исторически сохранилась устаревшая формулировка про «fixed 120 BPM MIDI». В коде экспорт передаёт последнее наблюдавшееся tempo, fallback 120; **4/4 при этом фиксирован**. Документацию синхронизировать с кодом.

# 3. Функциональная классификация звуков — независимые измерения

**Не смешивать** жанр, роль звука и способ исполнения. Один и тот же тип Impact может существовать в Morning/Uplifting/Goa/Dark Psy; режим Sequence может содержать Laser, Squelch или Stutter. Пресет имеет **одну primary role** (для учёта объёма) и несколько tags.

## 3.1. Музыкальные стили и настроение (genre taxonomy)
**Светлые / мелодичные / дневные:** Balearic / Sunset Trance, Dream / Melodic Trance, Classic 90s Trance, Uplifting Trance, Anthem / Euphoric Trance, Vocal Trance (без нелицензированных вокалов), Orchestral / Cinematic Trance.

**Глубокие / прогрессивные / энергичные:** Progressive Trance, Deep / Atmospheric Progressive, Tech Trance, Acid Trance, Hard Trance, Peak-Time Trance.

**Психоделический спектр:** Progressive Psy, Morning Psy / Full-On Morning, Full-On Night, Goa / Neo-Goa, Twilight Psy, Forest Psy, Dark Psy, Hi-Tech / Psycore, Psybient / Psychill, Zenonesque / Dark Progressive.

**Вторичные cross-over tags:** Ambient, Space / Sci-Fi, Industrial / Techno, Experimental / Glitch, Organic, Cinematic. Они **не заменяют Trance-first каталог**.

Фасеты: `dayphase` = Sunrise / Morning / Day / Sunset / Night / Deep Night; `energy` = Soft / Moderate / Driving / Peak / Extreme; `mood` = Airy / Warm / Euphoric / Melancholic / Hypnotic / Acidic / Tense / Dark / Alien / Organic / Aggressive; `tonality` = Non-tonal / Tonal / Key-aware. Значения являются **семантической схемой библиотеки**, а не обязательными названиями C++ enum. Поддержать несколько genre/mood tags.

## 3.2. Роли звуков (sound-role taxonomy)
У каждой роли несколько distinct sonic recipes, а не один неизменяемый wavetable:
1. **Impact / Boom / Hit:** sub impact, bass drop accent, punch, clap/snare-like transition hit, metallic stab, layered harmonic impact, cinematic thunder-like synthetic hit.
2. **Riser / Uplifter / Build:** noise/spectral riser, pitched / harmonic riser, Shepard lift, snare-roll/tick build, tension grow, gated build, accelerating transition.
3. **Downlifter / Fall / Reverse:** tonal downslide, reverse impact, reverse swell/cymbal-like synth, suckback, filtered release, descending glissando.
4. **Whoosh / Sweep / Flyby:** bandpass/frequency sweep, stereo pass, Doppler flyby, air/plasma motion, cross-rhythm sweep.
5. **Laser / Zap / Lazer Zap / Squelch:** short FM chirp, Goa zap, acidic resonance, rubber squelch, liquid/organic bubble, alien vocal-like zap, complex laser burst, resonant talkbox-like sweep.
6. **Sequence / Glitch / Rhythmic:** laser sequence, psy rolls, stutter/retrigger, ratchet, trance gate, digital artifact, acid pulse, syncopated fill, fake-drop cut.
7. **Drone / Atmosphere / Texture:** sunrise pad-like atmospheric wash (не полноценный lead synth), dusk drone, dark/forest organism, tension bed, space texture, granular cloud, breakdown air.
8. **Stinger / Tonal Transition / Fill:** harmonic accent, alarm/stab, short melody/arp effect, pre-drop marker, release sparkle, spectral tail.

Дополнительные назначения для поиска: Intro, Breakdown, Build-up, Pre-drop, Drop, Fill, Transition, Outro. Один Program может иметь несколько use-case tags. Не выдумывать искусственные «семейства DSP» на каждый tag: генерировать комбинациями реальных 12 источников, 4 слоёв, envelopes и FX.

# 4. Заводская библиотека — целевая программа 600

## 4.1. Учёт объёма
Каждый Program имеет один основный sound-role, считается один раз независимо от количества genre tags:

| Основная роль | Цель уникальных утверждённых Program |
| --- | ---: |
| Impacts, sub booms, punches, metallic hits | **110** |
| Risers, uplifters, builds, snare/tick builds | **95** |
| Downlifters, falls, reverse / suckback | **65** |
| Whooshes, sweeps, Doppler, flybys | **70** |
| Zap, laser/lazer zap, squelch, acid chirp, psy blip | **105** |
| Sequences, rhythmic rolls, stutter, glitch, gate | **75** |
| Drones, atmospheres, textures | **55** |
| Stingers, fills, harmonic / tonal transition | **25** |
| **Итого** | **600** |

Цифра — **целевой scope библиотеки**, установленный в рамках расширения требований; 48 существующих seed-программ НЕ засчитывать автоматически. Не делать пустых/повторяющихся файлов ради цифры.

## 4.2. Этапы приёмки контента
- **Wave A / 120:** end-to-end рабочий формат, Factory/User браузер, реальные все основные роли, минимум representative sounds в широком спектре Daytime / Uplifting / Progressive-Tech / Goa-Psy / Dark-Forest / Hi-Tech, и **все объявленные поддерживаемыми play modes**. Нельзя сформировать этот этап одними 120 Psy zaps.
- **Wave B / 300:** сложные комбинации, полноценные режимы, 4/8/16-bar events, парность тональных/нетональных переходов, качественные presets для каждой большой genre-группы, пользовательские assets, restore/export.
- **Wave C / 600:** добрать распределение из таблицы, убрать дубликаты и слабые звуки, выполнить всю матрицу качества и лицензий. **Не объявлять фабричную библиотеку 1.0 полной до Wave C**, если не принято явное решение о сокращении release-scope. Технический RC раньше Wave C можно помечать только как **engineering candidate**, не content-complete release.
- Внутри волн предпочтительны проверенные качественные Program, а не поток автоматически сгенерированных вариантов.

## 4.3. Метаданные одного фабричного Program
`MUST`: стабильный неизменяемый `preset_id`, читаемое уникальное `name`, `revision/schema`, одна `primary_role`, массив `style_tags`, `mood_tags`, `energy/dayphase`, `play_mode`, `use_case`, `duration_kind` и секунды/такты, тональная классификация + key/root/scale где применимо, seed/provenance, версия генератора, валидная Program+FX+mod+pattern структура, ссылки/хэши внешних assets и лицензионная информация, preset-specific Macro mapping, preview metadata. Не сохранять «тональность» для чистого шума как фиктивное управление.

Разработчик вправе выбрать binary/JSON manifest/набор файлов по нагрузке и совместимости, но **MUST** зафиксировать каноническую схему, загрузчик с валидацией, версии и миграции, детерминированный порядок сортировки, стабильные ID и защиту от дублирования. Изменение пути файла не должно произвольно изменять identity preset. Поставляемые ресурсы работают offline.

## 4.4. Отличимость и музыкальная пригодность
Каждый утверждённый пресет:
- не содержит NaN/Inf, не тождественно тихий, не имеет DC/попов, бесконтрольной самогенерации, незаканчивающихся хвостов (кроме осмысленно held/loop до release);
- реально отличается конфигурацией источника/слоёв/формой частоты/огибающими/эффектами/пространством/ритмом. **Смена только имени, MIDI-ноты, seed или +1% яркости НЕ равна новому звуку**;
- имеет проверенный preview и WAV golden (или детерминированный render reference); спектральную/динамическую/длительную проверку плюс экспертное прослушивание нескольких representative clips каждой роли и стиля;
- имеет уровень без цифрового клиппинга в предусмотренном reference playback, контролируемый tail, mono compatibility и проверенный behavior at 44.1/48/96 kHz;
- не копирует звучание конкретных коммерческих пресетов, не содержит sample без provenance; не выдавать AI/нейросеть за алгоритмический генератор.

**Приёмочная метрика:** отчёт по ID показывает распределение по ролям, режимам и ключевым Trance-направлениям, fingerprints для поиска дублей, silence/peak/finiteness/tail/restore результаты и аудио-ссылки. Порог похожести выбирается по проверенным audio-render выборкам, чтобы не выбрасывать музыкально близкие, но функционально различимые звуки.

# 5. Play Modes: реальная семантика, не label

`MUST` разделить **режим исполнения** и **политику повторных NoteOn**:
- **One Shot** — один MIDI NoteOn запускает законченный timeline + bounded tail; обычный NoteOff не обрывает. Impacts/zaps/stingers. При Stop/seek и all-sound-off есть документированное поведение.
- **Gate / Held** — NoteOn начинает/поддерживает звук; NoteOff по той же паре MIDI channel+note инициирует собственную огибающую release. Повтор и перекрытие нот не должны преждевременно прекращать другой голос. Panic/CC120/123 работают корректно.
- **Continuous / Sustain** — непрерывное процедурное развитие без заранее заданного автоокончания, пока note held/режим явно активен. Продолжительность MIDI-ноты не ограничивается 32 сек, зато CPU/memory и число voices — ограничены. Не держать в RAM произвольный многоминутный рендер.
- **Loop** — фазово согласованное, бесшовное либо явно click-managed зацикливание между boundaries с MIDI release/stop, независимое от одного повторно воспроизводимого one-shot без гарантий. Возможность sync free/host; отсутствие PPQ помечать.
- **Sequence** — 1–4 bars pattern as current baseline, привязка к точному PPQ, читаемые Note/Velocity/Probability/Ratchet/Swing, выбор trigger behavior, variation; MIDI экспорт и реальный scheduler используют один contract. Режим Pattern ON при отсутствии хоста не имитирует «идёт транспорт».
- **Transition / Build / Fall** — событие, которое заканчивается точно в целевой момент/на такте с предсказуемым impact/release, может состоять из нескольких звуковых слоёв и tail. Тональная/нетональная версия.
- **Retrigger / Overlap / Ignore / Legato replacement** — это **политики повторного запуска**, а не обязательно отдельные режимы аудио: повтор того же note делает документированную конкретную вещь, старый голос завершается без щелчка/случайного убийства соседних.

Для каждого режима сохранить mode + trigger policy + note ownership + release/tail настройки в versioned Program, покрыть state restore и edge cases (два одинаковых note, разные MIDI channels, velocity-zero, NoteOff вне порядка, panic, stop, loop, seek, transport missing). Текущая implementation One Shot с игнорированием NoteOff считается **GAP**.

# 6. Длительности, музыкальное время, тональность

1. `MUST` различать **seconds duration**, **musical bars duration** и **unbounded held lifecycle**. Не кодировать все режимы в единственном float параметре `fx.event.duration = 0.05..32 s`.
2. `MUST` 1/2/4/8/16-bar transitions и sync на host BPM/PPQ в 4/4 в рабочем диапазоне поддерживаемого транспорта. При других time signatures — поддержать корректно либо **честно объявить неподдерживаемыми** с тестом и безопасным fallback; НЕ трактовать 3/4 как 4/4. Длительность 16 bar при низком BPM >32 sec: реализовать **bounded clock-based event без хранения полного WAV в callback** либо документировать ограничение с явным отказом; прежний 32s cap не изменять тайно через host parameter.
3. `MUST` корректно обрабатывать tempo automation во время события, PPQ discontinuity, DAW transport Start/Stop/Seek/Loop, bouncing offline/real-time, пропавший playhead; timestamp и event placement sample-accurate в границах block-а.
4. `MUST` key/root/scale и транспозиция для tonal FX: музыкальные ноты, frequency sweep target, range, optional major/minor/другие режимы, отображаемая тональность; не навязывать pitch-quantization шумам и спецэффектам без определённого pitch. Источник key: preset-local, с явным управлением пользователя; не обещать чтение тональности из DAW без проверенного host API.
5. `MUST` отдельный **показатель peak/clip** и ощущение интенсивности; высокая intensity/drive — не автоматически «клиппинг». Для гармонических FX выдерживать тональную совместимость с басом/лидом.
6. `DESIGN CHOICE`: точный допустимый tempo range, allowed signatures и потолок bar duration зафиксировать в таблице проверенного механизма с memory/performance budget. Минимальный обязательный тест: 4/8/16-bar transition при 60/100/128/140/160/200 BPM в 4/4 + изменение BPM на полпути.

# 7. DSP и Program — целевое устройство

## 7.1. Основной граф
MIDI/Internal Trigger → bounded voice/event allocator → Event Timeline/Mode → 4 Layer sources A–D → per-layer amp/pitch/filter/pan and curves → stereo sum → shared modulation → 4 serial post-FX slots → master safety/output → real telemetry.

- 4 layers per Program; enable/solo/mute где корректно, family и параметры независимы; event snapshots immutable в течение воспроизведения, structural Program exchange вне RT.
- До 8 активных events; до 64 grains глобально, а НЕ на событие; ограниченный pool для bounded fade/retirement (baseline 4 slots по 5ms). Дополнительные steals > pool гарантированно сообщаются через telemetry; policy документируется и tested.
- DSP в `processBlock` не выделяет heap, не ждёт mutex/IO/file decode, не ходит в UI/worker, не делает блокирующих host calls; тяжелые таблицы, oversampling buffers, delays и sample decode в prepare/non-RT thread. Измерить deadline на Windows.

## 7.2. 12 исходных source families
Все 12 обязаны быть реально редактируемыми и музыкально различимыми, в том числе source-specific parameters:
1. Noise Riser: noise colors, filter/spectral sweep, gain/centroid progression, stereo.
2. Downlifter: pitched/noise descent, controlled tail/brightness, reverse-like variants.
3. Shepard / Tonal Sweep: anti-aliasing, pitch continuity, level normalization.
4. Impact/Boom: transient click/attack + body/sub/decay, optional tonal root, punch and tail.
5. Whoosh/Doppler: coherent movement of pan/width/filter/pitch, controlled flyby.
6. Zap/Laser: chirp type/range, FM/ring/drive, high-frequency alias safety, short attack/release.
7. Psy Blip/Squelch: controlled resonance, comb/morph/filter motion, glide, ratchet.
8. Metallic/Resonant: damped resonators/modal ratios, safe feedback, clang/chime.
9. Texture/Drone: evolving noise/oscillator layers, harmonic/tone and held lifecycle.
10. Granular Cloud: internal/user WAV source, grain length/density/position, stereo, pitch/speed/reverse/deterministic random seed, bounded global 64 grain pool.
11. Glitch/Stutter: own preallocated sample/history (не чужая DAW audio дорожка), grid/slice/retrigger/gate, host tempo sync, controlled seam.
12. Formant/Alien: 2–4 formant/bandpass vowel-like trajectories, safe modulation.

**Нельзя** создавать 12 одинаковых белых шумов с разными именами. Применение 4 слоёв для cross-family hybrid Program обязательно: e.g. Impact + noise reverse swell, Uplifter + Shepard + laser accent, Dark Forest organic + granular, Anthem tension + harmonic rise.

## 7.3. Общие источник и фильтры
- До двух bandlimited melodic oscillators на layer (sine/triangle/saw/pulse; detune/phase), sub + filtered noise где нужно; optional FM/ring внутри семейства; дорогие режимы имеют resource budget.
- 2 per-layer configurable stable SVF filters: LP/HP/BP/notch, 12/24dB, resonance, drive, frequency smoothing, no non-finite/unstable runaway; оптимизации только с аудиопаритетом/quality checks.
- **20 curves = 4 per layer × 4 layers + 4 global**: amplitude, pitch, cutoff, panorama на уровне layer, аналогичные осмысленные global trajectories на уровне Program. Nodes 2–16, shapes linear/smooth/exponential/hold, domain 0..1 с точным mapping to seconds/bars; per-curve lookup bounded, smoothing, typed units. Curve editor multi-selection, snap, copy/paste, undo/redo, selected nodes numeric, segment durations.
- Текущий `NoiseProgram` содержит только 2 per-layer curves; **MUST** добавить новые поля с versioned codec migration + rejection invalid/future/truncated sizes и bounded prepared snapshots. Не ломать чужие проекты.

## 7.4. Voice/tail/retrigger
- One-shot, Held, Continuous, Loop требуют разной политики event lifecycle; boundaries smooth без clicks; Panic fades; CC120/123 semantics documented.
- FX tails строго ограничены, не обрываются без объяснения на Stop/offline bounce; `getTailLengthSeconds()` truthful. Не использовать произвольные 60s как доказанный tail — cap подтвердить measured/stated.
- Отчёт dropped-event count, active/retiring voices, grains, output sample peak/clip — настоящие snapshots.

# 8. Modulation, 8 macros и 4 effect slots

## 8.1. Sources, destinations
- Источники: LFO1/2 с free Hz и host-sync, triggered step envelope, seeded S&H, ADSR, event phase, velocity, own signal follower, macros 1–8.
- Целевые назначения: не только Gain/Pan, но pitch, filter cutoff/resonance (где применимо), drive/FX mix, stereo width, granular params, meaningful layer controls. У каждого маршрута тип/единицы, signed depth, polarity, smoothing, clamping, ordering; отсутствие циклов и feedback hazards.
- До 64 маршрутов одновременно; управление матрицей в native FX UI со списком и читаемой картой; preset-specific macro assignment с 8 stable automation IDs.
- **Host base value сохраняется:** mod прибавляет/изменяет effective value, но не переписывает lane; restore, preset load и DAW automation readback честные.

## 8.2. Post FX
4 последовательных shared FX slots, реальные состояния:
`Bypass`, фильтр/EQ, Drive, Wavefold, Phaser, Flanger, Chorus, Delay (feedback+tempo sync), Reverb (управляемый хвост), Bitcrush, Ring Modulation, Trance Gate.

Обязательно: bypass, wet/dry, clear type switching с bounded crossfade, modulated parameters, корректные хвосты/delay clearing, anti-zipper; dry level/gain staging; сохранившиеся host IDs `fx.slot.N.wet` и `fx.slot.N.amount`. **DESIGN CHOICE** для oversampling 2x/4x nonlinear: enable only on verified latency/CPU/alias quality, prepared off RT and reflected in host latency.
- Исследовать известную проблему: 4 Drive в каскаде давали длительную насыщенность ≥1 full-scale в Linux stress. Измерить, настроить gain staging, headroom/quality, предотвратить нескончаемую звуковую перегрузку. Нельзя скрыть проблему простой сменой test threshold или permanent brickwall limiting без дизайна.

# 9. Generator / Mutate / Variation A/B

## 9.1. Вход
`{sound_role, style_tags, family/families, energy, mood, intensity, complexity, length_seconds_or_bars, key/root/scale_if_tonal, stereo_motion, seed, locks, play_mode, trigger_policy}`. Поля — **проектная форма контракта**, не придуманный существующий C++ API; имена/схема утверждаются реализацией.

- Generate создаёт **полный coherent Program**, а не только `gain/pan/3-node curve`: реальные 4 слоя, source-specific controls, 20 curves, meaningful 4-slot FX, 8 macro mappings/64 route constraints, trigger mode, seed, optional Pattern, asset dependencies.
- Модель разнообразия зависит от реальных музыкальных правил ролей и жанров (антемный harmonic lift ≠ Goa chirp ≠ Forest squelch), при этом тот же seed/engine+version всегда воспроизводит тот же canonical result. Lock может защищать все edited fields, including sample refs, curves, routing, pattern, FX, key, duration.
- Mutate меняет только незаблокированное, предупреждает если изменять нечего; Variations A/B — полноценные самостоятельные снимки Program, а не два label на текущем состоянии. Запись/recall A/B в project/user preset по явной политике; **session-only** baseline не удовлетворяет persistent requirement.
- Тональная генерация предлагает музыкальные диапазоны и MIDI note responses; noise events не требуют выдуманной тональности.
- Сравнение вариантов по громкости не должно вводить в заблуждение: предусмотреть слышимые, не только численные различия.

## 9.2. Factory vs Generate
Factory curated catalog и user Generate — разные вещи. Автогенерация 600 seed с изменёнными заголовками не закрывает Factory. Пользовательские Generated звуки сохраняются отдельно, не затрагивают immutable factory stock. Переименование/удаление пользовательского пресета не должно повреждать аудиопроект.

# 10. Pattern / trigger scheduler / MIDI

- Текущий baseline: 1–4 bars, 1/4…1/32 straight/triplet, 4/4 host PPQ, swing, note/velocity/probability/ratchet, deterministic seed. Поддержать визуальное редактирование grid, selection, copy/paste, undo и variations; display bar/page/step.
- `MUST` sample-accurate scheduling within block, stable ordering same-offset MIDI vs internal Pattern, zero notes missed/duplicated on normal boundary; patterns with probability/ratchet reproducible seed and export matching playback where representable.
- `MUST` capture tempo for fixed-tempo MIDI file correctly, meter consistency, clearly mark unavailable host timing. Если host-событие содержит tempo automation, MIDI-файл должен иметь выбранную/задокументированную snapshot/export policy; не ложно заявлять полную DAW tempo map.
- Внешний MIDI — вход; продукт как **instrument не обязан выдавать MIDI OUT**. Кнопка Export создаёт editable MIDI file; поддержать OS drag exported file when tested. Не изобретать «insert clip into Cubase» API.
- Не отображать 16-bar Song Arranger из Drums как обязательный FX-модуль; **FX Sequence** — отдельная функция. Long 8/16-bar FX transition — не эквивалент 16-bar trigger-grid паттерну.

# 11. Samples / Granular / portability / rights

- WAV PCM/float decoder with bounds, channels/length/sample rate validation, SHA-256, async import worker, cancellation, immutable source leases, sample-rate pitch fidelity.
- Saved external sample reference must include honest path/hash/missing status, version/migration; restore in project with missing/corrupt/moved asset produces **Unavailable** (с предложением Re-link) и не проигрывает другой звук без явного согласия; не блокировать не-гранулярные части/пресеты без причины.
- Import-pending transaction: save state records pending accurately, supersession cannot accidentally commit older sample, failure rollback coherent, existing sounding events own exactly selected immutable generation. Offline export must capture exactly one immutable sample version or fail; no partial successful WAV.
- User sample assets/libraries: licensing/provenance, optional user-side use without bundling and distributing their files. Factory licensed/original samples have manifest and verified redistribution rights. Offline render and project recovery deterministic.
- Audio callback NEVER decodes files, computes hash or waits for worker.

# 12. Экспорт WAV, MIDI и DAW workflow

**WAV**: отдельный non-RT worker рендерит **реальное аудио текущего Program**, включая effect tail, automation snapshot, user sample generation, correct tempo/key/velocity/notes. Selectable 44.1/48/96 kHz (или ясно указанный реально поддерживаемый subset), stereo, PCM24/float32 where qualified, cancel/progress, no overwrite surprise, atomic/transactional file write, no successful empty/truncated file. Рендер выбранного 1-shot, gate/loop finite region, sequence и 4/8/16-bar build — mode-specific, явный end/tail policy.
**Решение владельца от 20.11.2026 — обязательная политика Save 1.0: CREATE-ONLY / NO REPLACE для WAV и MIDI.** Существующий целевой файл (включая файл, созданный другим процессом во время экспорта) **никогда не перезаписывается, не удаляется и не изменяется**. При конфликте выдаётся понятная ошибка и предлагается выбрать **другое имя/путь**; без автоматического добавления суффикса и без скрытого повторного рендеринга. Проверка `exists` перед записью **не является гарантией**; финальная публикация должна использовать OS-операцию с атомарным запретом замены целевого имени. Если ОС/файловая система не поддерживает эту семантику либо отказала в операции — export FAIL, существующий файл и его данные остаются нетронутыми; нет fallback через copy, delete-first или overwrite. Подготовленный файл создаётся в безопасном staging и открывается DAW только после успешного завершения и проверки размера; при отказе/отмене временные файлы удаляются. Проверить WAV/SMF, диалог, конкурирующее создание файла перед commit, небезопасные пути/симлинки, отсутствие прав, Unicode, нехватку места и cleanup на Windows x64. **Это утверждённый owner requirement, не свидетельство готовой реализации и не разрешение Windows CI/merge.**
**MIDI**: экспортируем только trigger notes/pattern (не аудиокривые), валидный Standard MIDI File, note order/channel/tempo signature, editable in target DAWs. Увеличение возможностей не должно скрывать ограничения исходного 4/4.
**Drag**: допустим только экспорт real temporary MIDI/WAV file через поддерживаемый JUCE/OS mechanism и подтверждённую final DAW проверку; кнопка Save остаётся fallback. Drag не должен одновременно открывать Save и не должен создавать дубликат.
**DAW-приёмка:** Cubase 12–15 и REAPER; MIDI drag/drop редактируемый, WAV соответствует previews/offline bounce, NoteOff Gate, transport/loop/seek, project restore и automation. **Ручной этап один раз, на готовом интегрированном кандидате**, как требует владелец.

# 13. Native JUCE UI / пользовательский путь

## 13.1. Family look / interaction
- Оригинальный VOX hardware-inspired интерфейс: matte metal / rubber, чёткий рельеф, умеренный bevel, recessed tracks, tactile knobs/faders/switches, оригинальные VOX colour/type tokens, ясные focus/hover/pressed/disabled и контрастные статусы.
- Общие visuals заимствовать из `libs/vox-ui` и #127/#135/#160 без копирования UI конкурентов, React-компонентов, WebAudio и без WebView. Общий API `libs/vox-ui` изменяет Codex по согласованному контракту; Arena реализует FX-specific panels в `products/fx`.
- Одна ручка = один реальный parameter/effective control, unit/signed/bipolar origin корректен; автоматизация DAW и внешний preset restore немедленно отражаются в UI.
- Нет фальшивых meters, waveform, levels, CPU indicator. Intensity != Clip. Pad Rack 4×4 Drums не переносить.

## 13.2. Обязательные функциональные рабочие области
1. **PERFORM:** поиск/выбор Program/role/style, Mode, Duration, Tonality when meaningful, Preview/Stop, Generate/Mutate, A/B, 8 Macro, stereo peak/clip, transport status.
2. **DESIGN:** 4 независимых Layer A–D, family model, waveform/source/sample when relevant, gain/pan/pitch/filter/timing, mute/solo, source-specific controls, true curve thumbnails.
3. **TIMELINE:** 20 typed curves (16 layer + 4 global) with duration axis in s/bars, nodes editing/snap/zoom/multi-select/copy/undo, actual synchronized playhead/progress.
4. **MOTION:** 2 LFO, step, S&H, ADSR, velocity/follower and typed 64-route modulation matrix; macro assignment and readback.
5. **PROCESS:** четыре реальных FX slots with type/bypass/wet/amount/advanced controls, tail and metering; no hidden fake controls.
6. **TRIGGER / PATTERN:** 1–4 bar trigger grid, straight/triplet, note/velocity/probability/ratchet/swing, host sync, MIDI export.
7. **PRESETS:** Factory/User browser, multi-facet search (role+genre+mood+mode+use-case+duration+tonality), favorites, save/delete user presets, compare A/B, preview metadata, licensed/missing assets.
8. **ADVANCED:** real event/grain/drop/clip counts, CPU/memory from non-RT snapshots if implemented, voice/quality mode, sample status/relink, MIDI/transport diagnostics, plugin version, state schema, safe reset/panic.

Конкретная компоновка FX должна быть удобной **для FX**, не насаждать eight-page legacy Drums или обязательный bottom Mixer; допустим один workspace с docking/панелями и переключением central editors. Не ограничиваться одним гигантским промежуточным `FxPanel` с прокруткой. Размеры/DPI: тесты в Windows на 100/125/150/200%, representative 1040×700, 1280×720, 1440×900, 1600×1000; если конкретная поддерживаемая minimum отличается, документировать без обрезания функциональных controls.

## 13.3. Потоки действий и доступность
- Workflows: найти по жанру/роли; прослушать one-shot/loop; создать 8-bar uplifting riser в тональность; сделать night-psy laser sequence; поменять 4 слоя; загрузить granular WAV; сохранить User; A/B; рендер FX; открыть проект повторно.
- Keyboard navigation, tab order, tooltips, value typing, visible focus, screen reader labels где JUCE/OS предоставляет accessibility API, undo/redo для структурных редактирований.
- Не допускать UI thread жёстких задержек из-за WAV worker join/filesystem. Любая async операция имеет корректный lifetime/weak reference/cancellation.
- UI состояние docking/layout отделено от музыкального Program, project/preset state и host automation; ограничения сохранения отображать честно.

# 14. Форматы, состояние и совместимость

- Existing VST3 identity/FUID, product I/O и все **19 host parameter IDs** в `products/fx/parameters.csv` сохранять. New parameters добавлять осмысленно, с unique stable IDs, типами, диапазонами, automation gestures и тестом migration; нельзя переиспользовать старый ID для другого поведения.
- Typed structural Program state: 4 layers, source models, 20 curves, mode/trigger policy, effect settings/tails, modulation routes, macros assignments, generator settings/locks, patterns, A/B/presets, sample reference hashes/paths, export defaults/metadata. Версии/формат endian/bounds/non-finite validation, max chunk sizes, backward migration из существующего ProgramCodec v1, generator/pattern/sample state revisions.
- Project restore **atomic**: неверные/будущие данные не переводят часть плагина в новое состояние, не оставляя другую часть старой; no partial writes, no stale A/B and no dangling sample leases.
- Preset load vs DAW automation: явная policy (что Program меняет, что остаётся host base), no automation lane corruption; MIDI held events cleanly retire/fade on state reload; synth sound not unexpectedly continue with stale references.
- Протестировать сохранение без открытого редактора, повторное открытие проекта, undo/redo after restore, sample missing/relink, preset port across user paths, no asset dependence on developer environment.

# 15. Аудио/RT безопасность, нагрузка, инструментальная квалификация

**Mandatory sample rates:** 44.1 / 48 / 96 kHz. **Mandatory callback blocks:** 64 / 128 / 512 frames, включая неравномерные блоки, zero/large block и границы событий. Voices: 1/4/8 active, 4 layer and 64 grains, repeated same-pitch notes, maximum ratchet. Проверить default/low/high BPM и tempo changes.

- no heap allocations/locks/file IO/UI operations в callback; memory and all async jobs bounded, pool exhaustion deterministic (drop status) without crash/UB.
- no NaN/Inf/denormal runaway, DC excursion, unstable resonance, unexpected clipped factory audio; проверить избыточную saturation при каскаде 4 drive, mono fold-down и частотную/антиалиасинговую чистоту.
- sound family reference renders/golden; minimum curated representative clips by family×major style×mode; separate listening notes for rises, impacts, pitch falls, chirps, grain seam, formant clarity, stereo motion, loops and tails.
- Compare offline vs realtime within documented float/sample-timing tolerances, with explicit oscillator/random seed and motion reset phase rules. RT benchmarks measure p50/p95/p99/max and CPU budget on real Windows runner/DAW PC and a slower reference PC; do **not** promote Linux timing to Windows PASS. Declare threshold(s) before final run and publish measurement environment. Verify with Drums+Synth loaded simultaneously in final DAW pass.
- Hard memory caps, abort on invalid/corrupt sample/content, crash-safe worker teardown and project restore.

# 16. Тест-матрица и доказательства (не просто «CI green»)

| ID | Required automated evidence |
| --- | --- |
| FX-T01 | 12 distinct family renders; valid finite non-silent output and characteristic golden tests |
| FX-T02 | 4-layer mixed family rendering; independent layer gain/pan/pitch/filter/timing; immutable event snapshots |
| FX-T03 | All 20 curves: versioned encode/decode/migration, edit/playback/undo/automation readback |
| FX-T04 | One Shot ignores NoteOff only by policy; Gate releases; Continuous sustains until NoteOff/Stop; Loop seams; Sequence timings; Transition end on target bar |
| FX-T05 | Same note/channel repeats, cross-channel, velocity-zero, CC120/123, panic, full 8 events and 4 retirement fade exhaustion; no hangs/clicks |
| FX-T06 | Musical time: 1/2/4/8/16 bars; 60/100/128/140/160/200 BPM; change tempo, seek/loop/start/stop, missing PPQ/unsupported time signature |
| FX-T07 | Key/root/scale tonal behavior and MIDI note transpose; noise/non-tonal tags don't masquerade as pitched |
| FX-T08 | Macro 1–8, 64 typed routes, all supported sources/destinations, smoothing and automation-base integrity |
| FX-T09 | 4 effects, all supported types, bypass/morph/parameter automation, reverb/delay tails, anti-alias and 4×Drive saturation case |
| FX-T10 | Granular 64 grains, own/user WAV, hash verify/relink, pending/supersession/failed load, immutable lease lifetime, TSan stress |
| FX-T11 | Deterministic Generate/Mutate/locks full Program, 2 A/B states, migration, factory/user save/search/restore without automation corruption |
| FX-T12 | 120→300→600 catalog audit: unique IDs, count by role/style/mode, no filler duplicates, peak/silence/NaN/tail and audio evidence |
| FX-T13 | Pattern 1–4 bars: probability/swing/triplet/ratchet/seek/sample boundary; SMF structural validation and event parity |
| FX-T14 | WAV capture active sample generation, correct format/rate/note/tail/cancel/partial-file handling; MIDI export/drag file correctness |
| FX-T15 | JUCE editor runtime: all controls bound, no feedback recursion, correct focus/scroll/readback, DPI 100/125/150/200, resizing |
| FX-T16 | Product state round-trip without GUI, malformed/truncated/future-state rollback, project reopen and missing asset prompt |
| FX-T17 | Windows Release CTest, processor/editor EXECUTED not syntax-only, Steinberg VST3 validator, golden artifact manifests and SHA-256 |
| FX-T18 | RT nonallocation/no-lock probes, callback deadline 44.1/48/96 × 64/128/512, voice/grain stress and lifecycle |
| FX-T19 | Licensing/provenance per factory asset and no copyrighted presets/samples from competitors |
| FX-T20 | Final **once-only** owner Cubase 12–15/REAPER integrated acceptance: plugin recognition, MIDI triggering, state, automation, tempo/loop/stop, WAV/MIDI editable transfer, install/uninstall |

Каждому тесту в release evidence: **exact commit SHA, test command/run URL, environment, result, logs/artifact hashes, open exceptions**. Нельзя маркировать `PASS`, если на нём только syntax-check, Linux run, screenshot или браузер Playwright. Алгоритмическая и музыкальная приёмка — разные ворота.

# 17. Windows CI / shipping gates

1. **P0 infrastructure:** доступность 4 Windows self-hosted runners, queue hygiene; current FX workflow на ветке `#156` ещё использовал `runs-on: [self-hosted, windows]`; механизм failover из merged Drums CI #155 **не был автоматически перенесён в FX ветку**. Принять решение о безопасной общей интеграции workflows, не менять параллельно чужие рабочие ветки. Не запускать десятки obsolete SHA jobs.
2. **P0 existing crash:** в предыдущем Windows suite [44172558893](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/44172558893) четыре FX теста SegFault (riser, processor, layer, Shepard). Собрать Windows exception/dump, найти root cause, исключить для **текущего** и **финального** HEAD. Передвижение крупных test objects с stack на heap — гипотеза, не автоматически принятое решение.
3. **CI commands source of truth:** читать действующие `.github/workflows/suite.yml` / build workflow. На Windows VS 2022 x64 и pinned JUCE/VST3 SDK должны выполняться equivalently:
   ~~~powershell
   cmake -S . -B build-suite -A x64 -DVOX_BUILD_SUITE=ON -DVOX_BUILD_LEGACY=OFF -DVOX_STRICT_DEPENDENCIES=ON
   cmake --build build-suite --config Release --clean-first --parallel
   ctest --test-dir build-suite -C Release -L suite --output-on-failure
   ~~~
   Зависимости/pinned checkouts/SDK validator — строго из действующего workflow; эти команды являются **логической последовательностью**, не обещанием, что runner configured без его env variables.
4. **Gate A — functional:** FX-01–06 integrated DSP, state, modes, UI, 600 certified factory content and documentation; zero blocking P0/P1; feature-specific tests executed.
5. **Gate B — qualification:** exact-HEAD complete Windows x64 Release, tests, Steinberg validator, plugin bundle identity/SHA, error/no-sample conditions, CPU/memory/RT benchmarks, license inventory, packaging.
6. **Gate C — human acceptance:** однократная ручная DAW приёмка владельцем **только полностью готового** кандидата на Cubase 12–15 и REAPER, прослушивание репрезентативных стилей/режимов/impact/laser/ambient and broad FX content. Fix all blockers, затем контрольный exact-HEAD rerun при изменениях.
7. **Gate D — merge/release:** независимый TL review + отдельное разрешение на интеграцию/merge. #156 **Draft/HOLD/NO MERGE** до Gates A–C; не сливать в `main` по факту только зелёных тестов.

# 18. Последовательность разработки — один консолидированный пакет

Работу вести **на действующем FX #156**, не на browser #160, без переписывания проекта и без 20 однострочных PR:

**Stage 0 — baseline & CI:** актуальный HEAD/changed files, фиксация release scope, исправление Windows crashes/runners, проверка реального controller mapping/параметров. Параллельно допускается безопасная работа DSP, но не называть её validated.

**Stage 1 — structural model & timing:** versioned Program/codec migration для 20 curves, full source controls, play mode/trigger policy, bar clock/held loop, pitch/key, deterministic state + unsafe-load tests.

**Stage 2 — sound fidelity & performance:** довести 12 families, filter/oscillator fidelity, 4-slot quality, tails, 64-route modulation and active macro mapping. Аудио/RT tests и audio references после каждой интегральной серии.

**Stage 3 — end-to-end programs:** полный Generator→Program→DSP→Preview, factory/user persistence, A/B/locks, sample hash transactions, MIDI/WAV capture+export. Подготовить curated Wave A 120, затем B 300 и C 600 с coverage reports (можно параллельно native UI).

**Stage 4 — JUCE workstation:** не переписывая Processors, реализовать FX-specific Perform/Design/Timeline/Motion/Process/Trigger/Presets/Advanced with VOX tactile skin. Использовать реальные API и только работающие controls; координировать shared visuals с Codex. Проверить Windows UI/headless/integration automation.

**Stage 5 — integrated RC:** один полный Windows exact-HEAD test/validator/golden/perf/licensing/artifacts evidence bundle; независимое TL review; одна общая Cubase/REAPER owner DAW-приёмка; исправление найденного, final SHA evidence and release decision.

Разработчик публикует **один понятный progress checkpoint** с матрицей `feature | code | test | audio | UI | unresolved | SHA`, а не длинный список якобы закрытых P0 по неисполненным тестам.

# 19. Однозначные критерии готовности (Definition of Done)

VOX FX может быть назван **production-ready** ТОЛЬКО если одновременно:
- самостоятельный Windows x64 VST3 узнаётся DAW и воспроизводит MIDI события без Rack;
- 12 семейных DSP моделей и 4-layer смешивание музыкально пригодны; 20 кривых/модуляция/4 FX slots работают и сохраняются;
- One Shot, Gate, Continuous/Loop, Sequence и Transition действительно имеют заявленную семантику; 4/8/16-bar tempo events и tonal key-aware FX подтверждены;
- полная каталожная цель 600 **уникальных утверждённых** программ с True factory/user browse, search, tags, preview, provenance, 120→300→600 coverage и без фальшивых вариантов выполнена (либо владелец отдельно изменил scope);
- Native JUCE UI закончен и все видимые элементы имеют настоящие bindings, automation/readback, accessibility/resizing;
- user samples/file export/state restore/version migrations/asset missing handling работают в реальном плагине;
- Windows CTest + processor/editor execution + validator + performance/safety + licensing по exact SHA, no unexplained segfaults, published hashes/golden audio;
- финальная **единая** DAW-приёмка владельцем выполнена, независимое TL review закрыто; разрешение на merge зафиксировано.

**Неприемлемые суррогаты:** названия вместо звуков, demo-only генератор без специальных режимов, «600 seed = 600 factory», browser test = VST3 PASS, только Linux/syntax-check, fake Host sync, UI knob без параметра, fake clip meter, обрезанная MIDI дорожка, silent fallback на чужой sample, успешный incomplete WAV export.

# 20. Вне scope 1.0 и открытые engineering decisions

## 20.1. Не делать без отдельного решения
- FX processor на входном аудио другой дорожки DAW / sidechain; его создавать отдельно и с другим VST3 ID.
- Rack, Kontakt-подобный хост, Bass/Acid как отдельные части VOX FX.
- AI, облачная генерация, сеть, телеметрия пользователя, защищённые сторонние библиотеки без лицензии.
- Продвинутый Dolby Atmos/3D/многоканальный spatial renderer; FFT convolution/dynamic spectral editing без proven quality/budget.
- Имитировать интерфейсы и лицензированные пресеты конкретных конкурентов.
- Переписывать JUCE shared `libs/vox-ui` без согласования с Drums Codex.

## 20.2. Решения, которые разработчик ОБЯЗАН документировать перед реализацией
1. **Clock model:** как bounded 16-bar event работает при темповых изменениях/low BPM и как совмещается существующий 32s host parameter (новое typed structural duration vs existing automation).
2. **Mode/voice ownership:** что конкретно происходит при повторе NoteOn, CC64 optional, stop/seek, loop, pre-existing tails and source change.
3. **Source/detail scope:** source-specific ranges and anti-alias/oversampling budgets; какие полифонические сложные режимы реально включены в 8-voice cap.
4. **Global curves:** точные typed 4 global destinations, влияние на post-FX/tail, serialized migration.
5. **Host parameter expansion:** stable ID map с отдельным owner review (19 existing IDs immutable); что structural vs automatable.
6. **Factory schema/content packaging:** бинарный/файловый формат, manifest, license audit, deterministic preview, asset location/relink.
7. **Performance budget:** до final qualification согласовать measurable thresholds на конкретных Windows hardware profiles; не выдумывать CPU %.
8. **User sample size/length:** memory limit/streaming or fully decoded lease, version retention and missing policy; no unbounded allocation.
9. **UI layout:** native FX flow/sizing, genuine control bindings, shared Vox component contract; no duplication with Drums.
10. **Release staging:** всё 600 — выбранная целевая finished 1.0 library; ранние технические RC до Wave C являются development-only. Если owner решит выпускать с меньшим количеством, требуется явная поправка в этот же документ и PR.

## 20.3. Утверждённое решение владельца — Save create-only (20.11.2026)

**CLOSED — решение принято, не альтернатива для Release 1.0.** Экспорт `Save WAV` и `Save MIDI` только создаёт новый путь; существующий файл нельзя заменять даже после проверки его identity/mtime. Убрать overwrite из пользовательского workflow и commit policy; результат отказа должен быть явным и безопасным. `Prepared drag` использует собственные временные файлы и сохраняет отдельную семантику lifetime/copy-into-project. Нарушение create-only — **P0 release blocker**. Тесты и точный SHA в FX-T14/FX-T17; фактическая Windows/DAW приёмка обязательна. Не расширять разрешение на Windows CI, merge или release по факту изменения ТЗ.

---

# 21. Требуемый ответ Arena по этому ТЗ

Не переписывать ТЗ своими словами вместо реализации. Ответить в **#156** кратким инженерным планом и продолжить код:
1. `Source baseline`: точный актуальный HEAD и список выявленных несоответствий этому документу, со ссылками на файлы (не делать голословных «done»).
2. `Implementation slices`: 3–5 крупных технически связных пакетов + зависимости, без отдельных микроскопических PR.
3. `Initial blockers`: результаты/причина Windows SEGFAULT, CI queue и **явные открытые design decisions** из §20.2; можно выбирать минимально сложную безопасную реализацию, но зафиксировать выбранные ограничения.
4. `First evidence target`: один truly playable full-stack Program с новым режимом Gate/Continuous, tonal/16-bar transition, complete state migration, editable native UI and executable Windows tests; затем расширять готовую архитектуру до 600 **после** музыкального proof.
5. `Release ledger`: таблица FX-T01…FX-T20 с exact-HEAD run + audio/manual status, и coverage 120/300/600.

**Не ждать повторного разрешения на каждую техническую правку. HOLD означает запрет merge/release, а не запрет продолжать разработку.**
