# CONTEXT — состояние миграции GUI на React (рабочие заметки)

Краткая сводка для продолжения работы в новой сессии. Объём и порядок задач — в `PLAN.md`,
правила и команды — в `AGENTS.md`, архитектура моста — в `docs/react-webview.md`.

## Объём (актуальный, из PLAN.md / AGENTS.md)

- Цель: Linux, React + TypeScript в системном WebView (WebKitGTK 4.1), C++-логика сохраняется.
- **Не делаем:** FTB (modern/legacy/App), Technic, ATLauncher в React; Windows/macOS WebView;
  скины/плащи (отложены). Backend-код этих провайдеров не удалять.
- Qt GUI остаётся fallback (`--qt-gui`) до проверенного паритета; удаление Qt — только после него.
- Коммиты/пуши — только по явной просьбе пользователя.

> Первоначальный план сессии (удалить Qt, WebView2/WKWebView, все провайдеры) **заменён** этим объёмом.

## Что уже сделано (закоммичено в `299e32ca8`)

**Инфраструктура**
- `launcher/interaction/UserInteraction.*` — сервис вопросов пользователю (вместо Qt-диалогов в бэкенде):
  `ask` (асинхронно), `askBlocking` (вложенный QEventLoop), `notify`, `confirm`, `message`,
  `chooseOptionalMods`, `confirmUntrustedMods`. Без presenter — нативный QMessageBox / ответ по умолчанию.
- `launcher/modplatform/helpers/BlockedModsWatcher.*` — логика бывшего BlockedModsDialog +
  `interaction::resolveBlockedMods()` (живое обновление prompt'а, «Add folder» через нативный диалог).
- Удалены Qt-диалоги BlockedMods/UntrustedMods/OptionalMod/NetworkJobFailed; бэкенд-задачи
  (Flame/Modrinth/ATL/FTB install, InstanceTask, InstanceDirUpdate, JavaCommon, EnsureAvailableMemory,
  ResourceFolderModel/ModFolderModel, NetJob, ShortcutUtils) переведены на `interaction::*`.
- `api/PromptApi` — presenter для веба: события `prompt.show/update/close`, методы
  `prompts.pending/answer/action`. React: `components/PromptHost.tsx` (+ `RichText.tsx` — безопасный рендер
  HTML-подмножества из Qt-строк).
- `ApiRouter::addDeferred` — обработчик выполняется на следующей итерации цикла (нужно для
  обработчиков, которые спрашивают пользователя / показывают нативные диалоги).
- i18n: фронтенд `t("Text %1", arg)` / `tn("%n mod(s)", n)` (`frontend/src/i18n/index.ts`);
  `scripts/extract-i18n.mjs` (запускается в dev/build/typecheck) → `src/i18n/keys.generated.json` (ignored);
  `i18n/qt-contexts.json` — снимок контекстов Qt-строк Prism (сгенерирован `lupdate` по HEAD до удаления
  диалогов; регенерация: `node scripts/qt-contexts.mjs <file.ts>`). C++: `api/I18nApi` —
  `i18n.info`, `i18n.catalog` (переводы через `TranslationsModel::rawTranslation`, плюрализация по образцам n),
  событие `i18n.changed`. Проверено на ru (включая множественные формы).

**Инстансы (InstanceApi)** — rename с вопросом о переименовании папки (возвращает новый id),
группы (`renameGroup/deleteGroup/setGroupCollapsed`), `overview` (canUndoTrash, общее время, свёрнутые группы),
`undoTrash`, полный `copy` (все InstanceCopyPrefs + иконка), `copyInfo` (reflink/link), профайлеры,
ярлыки (`shortcutTargets/createShortcut`), remove с проверкой linked через prompt, launch с `profiler`.
LaunchInteraction в InstanceApi теперь спрашивает через prompts: выбор аккаунта, демо, имя игрока
(если не передано), ожидание профайлера. Проверка пути инстансов (`checkInstancePathForProblems`) вынесена
в `InstanceDirUpdate.cpp`.

**Иконки** — `system.icons` с категориями, `icons.add` (нативный выбор), `icons.remove`, событие `icons.changed`.

**Компоненты (ComponentApi)** — порт VersionPage/InstallLoaderDialog: list/versions/setVersion/
installLoader (конфликты загрузчиков через prompt)/setEnabled/remove/move/customize/revert/addEmpty/
addFiles(jarMods|customJar|agents|components, нативный выбор)/edit/reload/downloadAll; событие
`components.changed`. React: вкладка «Version» (`components/VersionView.tsx`), `VersionSelect.tsx`.

**Настройки (SettingsApi)** — полный allow-list (~95 ключей), типы Bool/Int/String/Json/StringList/Path;
чувствительные ключи → нативное подтверждение; секреты (токены, пароль прокси) write-only
(`_secretsSet`); `settings.reset`, `settings.pickFolder`, `settings.pickFile`; прокси применяется
через `Application::updateProxySettings`. UI страницы настроек ещё не расширен под новые ключи.

**Модпаки (ModpackApi, `components/ModpackCreate.tsx`)** — Modrinth/CurseForge search/versions/install,
`importFile` (нативный выбор) и `importUrl` (http/https). В React показаны только custom/import/modrinth/curseforge.
Исправлен отложенный save PackProfile после переноса staging-каталога.

**React-компоненты** — `IconPicker`, `Menu` (checked/section/контекстное меню `useContextMenu`),
`Toasts` (action-кнопка, напр. «Undo»), `InstanceActions` (copy/rename/group/icon/shortcut/profiler/
launchMenu/instanceMenu), `InstanceCard` (split-кнопка запуска, double-click по `EditInstanceOnDoubleClick`,
drag&drop в группы), `InstancesPage` (группы, свёртка, undo trash, общее время), сторы `overview`/`settings`.

## Проверка (рецепт)

- Сборка: `cmake --build build --parallel 4` → бинарник `build/materialmc` (не `prismlauncher`!).
  Локально нужен `build/tools/javac` (обёртка `--release 8`), уже прописан в кэше CMake.
- Тестовые данные: `build/webui-test-data/` (конфиг `materialmc.cfg`); всегда абсолютный `-d`.
  Файлы `*_nomigrate.txt` там глушат вопрос о миграции данных Prism.
- Харнесс: `cd build/webui-test-data && ./e2e.sh <script.js> <timeout>` — инжектит скрипт в
  `build/frontend/index.html`, лог в `e2e.log`; скрипт пишет через `__materialmcNative.post({method:"E2E ..."})`.
  Скрипты: `e2e-i18n.js`, `e2e-s1.js` (инстансы), `e2e-s2.js` (компоненты), `e2e-s3-*.js` (модпаки).
  Prompts в тестах авто-отвечаются подпиской на `prompt.show`.
- Скриншоты: `MATERIALMC_WEBUI_ROUTE=/path WAIT=4 ./shot.sh out.png` (Hyprland + grim).
- Экран миграции/мастер Qt появляется, если запуск без абсолютного `-d`.

## Известные нюансы

- Критические логи `Couldn't open .../mmc-pack.json for writing` после rename/remove в тестах — от
  отложенного save по старому пути (`loadedPackProfile` + неудачный онлайн-resolve в тестовых данных).
- `tn()` без перевода: англ. fallback заменяет `(s)` по числу.
- Обработчики, которые блокируют (askBlocking, QFileDialog, SensitiveChange), регистрировать через `addDeferred`.

## Следующие шаги (по PLAN.md, этап 1)

1. Проверить реальную установку CurseForge и импорт ZIP/`.mrpack` (файл и URL); смотреть `mmc-pack.json`.
2. Managed Pack: API (версии + changelog, update, update from file/URL через `InstanceImportTask`
   с extraInfo pack_id/pack_version_id/original_instance_id) и вкладка в React (логика из `ManagedPackPage.cpp`).
3. URL-схемы (`modrinth://`, `curseforge://`, `materialmc://import`) и drag&drop: вынести логику
   `MainWindow::processURLs` в бэкенд; сейчас `Application.cpp` (TODO(webui)) открывает Qt-окно.
4. Проверить запуск установленной сборки.
Далее: этап 2 (моды: зависимости/review, обновления, смена версии, lock, фильтры, datapacks),
этап 3 (экспорт), этап 4 (UI настроек и Java), этап 5 (миры/серверы/скриншоты/логи, аккаунты без скинов,
первый запуск, апдейтер).
