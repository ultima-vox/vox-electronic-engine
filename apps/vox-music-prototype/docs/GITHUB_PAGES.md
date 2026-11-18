# Публикация VOX MUSIC Workbench на GitHub Pages

Это статический браузерный референс из PR #160, не нативный релиз и не визуальное согласование по #127/#135. Деплой не использует Arena, Vercel, отдельную ветку `gh-pages` или слияние PR.

## Адреса и проверка публикации

Публичный адрес: [VOX MUSIC Workbench](https://ultima-vox.github.io/vox-electronic-engine/). Точки входа: [Drums](https://ultima-vox.github.io/vox-electronic-engine/#drums), [FX](https://ultima-vox.github.io/vox-electronic-engine/#fx), [Design System](https://ultima-vox.github.io/vox-electronic-engine/#system).

Первый успешный деплой — `7e9d704c0bcccd96ada9c7ecd95bade67ac4085b`: [публичная HTTPS-проверка SHA и 26/26 Chromium](https://github.com/ultima-vox/vox-electronic-engine/actions/runs/44296408470/job/134780977376). После **Publish to GitHub Pages** job **Verify the public HTTPS site** без локального сервера сверяет точный SHA по [workbench-version.json](https://ultima-vox.github.io/vox-electronic-engine/workbench-version.json) и повторно тестирует сам публичный сайт. Маркер показывает текущий опубликованный коммит, а не обязательно первый. Статический сайт на Pages обслуживается GitHub независимо от Arena.

## Однократная настройка (уже выполнена владельцем)

1. **Settings → Pages → Build and deployment → Source → GitHub Actions**. Не выбирайте `Deploy from a branch` и не создавайте workflow через шаблон, который коммитит в `main`. Для стандартного `github.io` используется HTTPS; не добавляйте custom domain для этой конфигурации.
2. **Settings → Environments → `github-pages` → Deployment branches and tags → Selected branches and tags**. Разрешите правило типа **Branch** только для точного имени `arena/ce951751-vox-electronic-engine`, без wildcard. Если `github-pages` ещё нет, создайте это окружение или откройте автоматически созданное после первого запуска. Не снимайте существующие review/approval gates для деплоя.
3. **Actions → VOX MUSIC Pages → запуск нужного SHA → Re-run all jobs**. Повтор всех jobs пересоберёт артефакт с тем же SHA и новым именем по номеру попытки, в том числе если предыдущий уже истёк. Если окружение требует approval, владелец подтверждает только запуск ожидаемой ветки/SHA. При ограничительной политике Actions разрешите официальные actions, перечисленные в [workflow](../../../.github/workflows/vox-music-pages.yml); расширять глобальные права `GITHUB_TOKEN` или добавлять PAT не требуется.

История: первая попытка не опубликовала сайт, пока Pages был выключен. После настройки владельцем и повтора всех jobs публикация прошла. Зелёная сборка или загруженный `dist` сами по себе не доказывают, что сайт обновился.

## Обновления и границы доступа

Обычный push проверенного изменения в `arena/ce951751-vox-electronic-engine` автоматически запускает [VOX MUSIC Pages](../../../.github/workflows/vox-music-pages.yml). Он берёт точный SHA push, проверяет `npm ci`, TypeScript, unit, Vite build и Chromium под `/vox-electronic-engine/`. После всех проверок загружается только `apps/vox-music-prototype/dist` с маркером SHA; `node_modules`, исходные ZIP, логи и тестовые результаты не входят в сайт. Только job публикации получает `pages: write` и `id-token: write`; у build и HTTPS-проверки лишь `contents: read`. Нет триггера `pull_request`/`pull_request_target`; fork и другие refs не входят в публикацию. PR-проверки остаются отдельным workflow без Pages-доступа. Храните доверенную ветку для следующих обновлений; deployment не требует слияния с `main`.

Для локальной проверки префикса из `apps/vox-music-prototype/` с уже установленным Chromium:

```sh
npm ci
npm run typecheck
npm test
VOX_BASE_PATH=/vox-electronic-engine/ npm run build
VOX_BASE_PATH=/vox-electronic-engine/ VOX_TEST_PREVIEW=1 VOX_VISUAL_CAPTURE=0 npm run test:e2e
```

Обычный Vite-запуск без `VOX_BASE_PATH` по-прежнему использует корень `/`. На origin Pages в браузере сохраняются только layout Workspace в Drums и явно сохранённые данные FX (User Programs и избранное). Музыкальные правки Drums и импортированные сэмплы сбрасываются при перезагрузке страницы. Аудиоимпорт, экспорт JSON и скачивание демо MIDI в фиксированном 4/4 не требуют backend. Данные `localStorage` со старого preview-домена браузер сам не переносит; FX Program можно перенести экспортом и импортом JSON.
