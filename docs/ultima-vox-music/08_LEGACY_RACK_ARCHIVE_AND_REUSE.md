# 08 — Безопасное архивирование старого Rack и перенос пригодного кода

**Решение владельца от 08.11.2026:** общий хост/Rack переводится в legacy; будущие продаваемые продукты — три независимых VST3. Старую ветку сохранить как **исторический read-only источник** для кода, ABI/тестов и аудиореференсов. **Не удалять историю и не пытаться автоматически перенести все старые PR.**

## 1. Что означает «в архив» в Git/GitHub

GitHub поддерживает **архивирование целого репозитория**, но не отдельный специальный `archive=true` флаг для ветки. Архивировать весь `ultima-vox/vox-electronic-engine` нельзя, если тот же репозиторий станет монорепозиторием новых продуктов: он перестанет принимать обычные правки. Подход без потери кода:

1. Зафиксировать фактический HEAD старого `integration/rack-1.0` и список незавершённых дочерних PR, workflows, artefacts, внешних зависимостей, подписей и тестов. Исторически проверенный SHA: `f9c8bc642567ba0edc6b638b64c72f9dc6ccf2e1`; **проверить актуальное состояние перед тегом**, а не считать его вечным HEAD.
2. Создать **аннотированный неизменяемый архивный тег** `archive/rack-legacy-2026-11-08` (имя предложено, если не занято) на подтверждённый SHA. При желании дублировать его в отдельной ветке `archive/rack-legacy-2026-11-08`, защищённой от push; это только указатель на сохранённую историю, не переписывание кода.
3. Оставить старые рабочие `integration/rack-1.0`, `fx/base`, `perf/router-channel-index` и другие ветки нетронутыми, пока есть открытые PR, поскольку **удаление base веток делает review/сравнение зависимостей недостоверным**.
4. Отдельным issue/ADR зафиксировать, что #77 legacy release gate **superseded by three-standalone direction**, а #78 и #110 **reference only**. Обсуждения и P1 по performance оставить доступными; старые CI не выдавать за green новых инструментов.
5. По завершении переноса донорского кода (и только после владельческого решения) закрыть неподходящие Draft PR как `superseded`, с ссылкой на архивный тег. Ничего не merge «ради сохранности» — git tag уже сохраняет историю.
6. Новая ветка разработки: `next/standalone-suite` от выбранной согласованной базы; переносить **точечными коммитами**, зафиксировав SHA источника/допустимые диффы/golden parity. Сценарий `merge integration/rack-1.0 into main` не допускается без отдельного архитектурного ревью.

**Контрольный пример команд** (не выполнялись и здесь приведены как процедура, применять после верификации):

```powershell
# Run from a clean local clone. Verify target branch still exists and actual remote SHA.
git fetch origin --prune
git ls-remote origin refs/heads/integration/rack-1.0
git show --no-patch --format=fuller origin/integration/rack-1.0
# Confirm SHA against the reviewed release baseline BEFORE performing write:
git tag -a archive/rack-legacy-2026-11-08 <VERIFIED_SHA> -m "Legacy Vox Rack archival snapshot, standalone pivot"
git push origin refs/tags/archive/rack-legacy-2026-11-08
```

Перед `git tag` заменить `<VERIFIED_SHA>` действительным SHA. Не запускать `git branch -D`, `git push --force`, `git reset --hard` на ценных рабочих ветках. Если tag already exists, **не передвигать его**: выбрать дату/ревизию и написать ADR.

## 2. Аудированный перечень кандидатов на reuse

Пути ниже получены через GitHub PR diff/file inventory и чтение исходников у `integration/rack-1.0` `f9c8bc6`, а также PR #52/#59/#69/#70/#72/#79/#92/#110. Это **кандидаты**, не утверждение о готовности в другом продукте. Перед переносом каждый проходит reference render & dependency audit.

| Категория | Точный путь/источник | Решение | Гейт переноса |
|---|---|---|---|
| DSP BLEP/SVF/ADSR | `libs/dspcore/Primitives.h` | **Reuse** (JUCE-independent) | тест частоты/aliasing, filter stability, no allocations |
| Effects | `libs/effects/EffectsChain.*` | **Reuse with adapter** | снять зависимости от общего FX state/return graph, тестировать tails/latency/RT |
| VOX UI | `libs/vox-ui/`, `docs/ui/DESIGN_SYSTEM.md` | **Reuse core** | вынести зависимость от Rack, сохранить Theme, LookAndFeel, bindings, visual gate |
| Drums | `libs/drums/VoxFullOnDrums.c`, PR #72 + staged #95–#104 | **Extract and extend** | золотые рендеры 8 пэдов, затем переход на 16 с чёткой версией алгоритмов |
| Bass | `libs/bass/FullOnBass.*`, `libs/bass/PsyBassVoice.*`, PR #52/#70 | **Extract** | объективная проверка NoteOff/Release, phase, bass transient, sound parity |
| Acid 303 | `libs/acid/AcidEngine.*`, `AcidGenerator.*`, PR #59/#69/#92 | **Extract, fix reviews** | не переносить unresolved lifecycle/slide/Panic/ABI баги; direct DSP tests |
| Samples | `libs/instrument/samples/SampleDecoder.*`, `SampleLoader.*`, `SampleExchange.*` | **Adapt** | сделать standalone sample store, lifetime/retirement on nonRT, path portability |
| Sequencer/pattern | `libs/pattern/Pattern.*`, `libs/midi/*`, tests | **Selective reuse** | instrument-specific scheduler, one event source for render/export, transport parity |
| FX routing/duck | `libs/rack/AudioRouting.*`, `RackDuckingTests.cpp` | **Reference/tests only** | standalone instruments don't need 16-slot graph; adapt own buses/sends correctly |
| JUCE shell | `apps/vst3/PluginProcessor.*`, `PluginEditor.*` | **Reference only** | new processors/IDs per product, no Rack constructor/loading/16×128 slots |
| `.vins` loader/signatures | `libs/instrument/vins/*`, `native/*`, PR #79 | **Archive; optional idea reuse** | standalone VST3 needs no dynamic instrument loading; don't bring security attack surface |
| FX-00 V2.5 docs | `docs/fx/*`, PR #110 | **Architectural reference only** | standalone programs via processor state, no shared ABI extension |
| Native C ABI | `libs/instrument/abi/VoxModuleAbiV2.h` | **Archive** | no need for VST3 host-owned nested module; local C adapter possible only during transitional tests |
| Fixed automation pool | `libs/rack/RackState*`, `RACK_PARAMETER_CONTRACT.md`, PR #80 | **Do not port as constraint** | each plugin has its own stable IDs and APVTS |
| Realtime/CI | `tests/support/AllocationProbe*`, `tests/performance/*`, `.github/workflows/*` | **Reuse methodology** | change test subject to new exact binary / processor, don't recycle old green result |
| Content signing | `docs/release/RELEASE_SIGNING.md` | **Reference** | `.vins` signing differs from VST3 Authenticode/installer signing; define new policy |

**No blind cherry-pick**: old PRs contain shared Rack UI/host state edits. For portable DSP copy only narrow source and tests after license audit; prefer `git restore --source <SHA> -- libs/bass/...` into a new worktree and explicit test commit, not entire PR merge. `git subtree` is unnecessary until proven.

## 3. Compatibility and release identity

- The three plugins use unique VST3 class identities, plugin filenames and parameter IDs. Do **not** reuse the old VstEngine FUID `com.ultimavox.vstengine` or `slotNN.pKKK` to create false compatibility. Freeze IDs before public release.
- Old `.vins` files do not automatically install into separate plugins, old Rack user projects are not automatically opened. Export/import data converter, if valuable, is a **separate versioned tool** with explicit diagnostics and tests; no silent fallback.
- New factory patches can be generated from source algorithms, but previous render references remain only QA materials. Do not bundle any reference sample without audited redistribute license.
- Product docs/PRs must mention exact source SHA for every transferred file; if major DSP quality improvement breaks parity, attach before/after render and an owner-approved intentional-change record.

## 4. Suggested Architecture Decision Record to record in GitHub

> **ADR: Move to three independent instruments.** Starting with this decision, `VOX Trance Synthesis`, `VOX Drums`, and `VOX FX` are three VST3 instruments with shared build-time libraries and independent GUI/DSP/preset/state. The old Rack 1.0 + `.vins` release direction (#77/#78) is archived as a historical branch and may donate code after audited extraction. It is not the required runtime nor a release blocker for the new products. No automatic merging or deleting of legacy PRs/branches is authorized. Production-ready status requires standalone host tests, not old Rack CI.

**Current operational state:** this document does not itself create tags/branches, close issues, move files, or change release gate statuses in GitHub. Perform step 1–4 through explicit git actions with validation and record their links.
