# Публикация VOX MUSIC Workbench на GitHub Pages

Это статический браузерный референс из PR #160, не нативный релиз и не визуальное согласование по #127/#135. Деплой не использует Arena, Vercel, отдельную ветку `gh-pages` или слияние PR.

## Адреса и проверка публикации

Ожидаемый адрес проекта: `https://ultima-vox.github.io/vox-electronic-engine/`. Точки входа:

- `https://ultima-vox.github.io/vox-electronic-engine/#drums`
- `https://ultima-vox.github.io/vox-electronic-engine/#fx`
- `https://ultima-vox.github.io/vox-electronic-engine/#system`

Это **целевые URL**, не подтверждение, что Pages уже включён. Успешный job **Publish to GitHub Pages** должен вернуть этот HTTPS-адрес. Затем **Verify the public HTTPS site** без локального сервера проверяет, что `/vox-electronic-engine/workbench-version.json` отдаёт точный SHA деплоя, и прогоняет Chromium на самом публичном сайте. Только после обоих успехов SHA можно считать опубликованным и проверенным. Статический сайт на Pages продолжает обслуживаться GitHub после завершения сессии Arena.

## Однократная настройка владельцем репозитория

1. **Settings → Pages → Build and deployment → Source → GitHub Actions**. Не выбирайте `Deploy from a branch` и не создавайте workflow через шаблон, который коммитит в `main`. Для стандартного `github.io` используется HTTPS; не добавляйте custom domain для этой конфигурации.
2. **Settings → Environments → `github-pages` → Deployment branches and tags → Selected branches and tags**. Разрешите правило типа **Branch** только для точного имени `arena/ce951751-vox-electronic-engine`, без wildcard. Если `github-pages` ещё нет, создайте это окружение или откройте автоматически созданное после первого запуска. Не снимайте существующие review/approval gates для деплоя.
3. **Actions → VOX MUSIC Pages → запуск нужного SHA → Re-run all jobs**. Повтор всех jobs пересоберёт артефакт с тем же SHA и новым именем по номеру попытки, в том числе если предыдущий уже истёк. Если окружение требует approval, владелец подтверждает только запуск ожидаемой ветки/SHA. При ограничительной политике Actions разрешите официальные actions, перечисленные в [workflow](../../../.github/workflows/vox-music-pages.yml); расширять глобальные права `GITHUB_TOKEN` или добавлять PAT не требуется.

При подготовке Pages был выключен (`has_pages: false`), а API управления Pages и окружением вернул `403 Resource not accessible by integration`. Зелёные browser-тесты или загруженный `dist` в Actions не заменяют настройку владельцем и не означают публичного деплоя.

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

Обычный Vite-запуск без `VOX_BASE_PATH` по-прежнему использует корень `/`. Workspace и сохранённые FX Programs хранятся в браузере на origin Pages, а локальный аудиоимпорт и скачивание JSON/MIDI не требуют backend. Данные `localStorage` со старого preview-домена браузер сам не переносит; FX Program можно перенести экспортом и импортом JSON.
