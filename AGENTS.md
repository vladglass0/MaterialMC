# MaterialMC agent notes

## Scope and permissions
- Read `PLAN.md` before continuing migration work: it defines the current scope, ordered milestones and next step. Update its checkboxes only after the stated verification, not merely after implementing code.
- Do not commit, push, reset, or rewrite Git history without explicit user permission. The migration includes substantial uncommitted work; preserve it.
- Current migration target is Linux React UI + system WebView, retaining the C++ backend. Windows/macOS WebView implementation is out of scope; skins/capes are deferred.
- FTB (modern, legacy and App), Technic and ATLauncher are excluded from further React migration. Do not resume those providers merely because their backend code exists.
- Keep the Qt GUI fallback until the required Linux workflows reach tested parity; excluding a feature is not permission to delete its existing implementation.

## Codebase navigation (graphify)
- For architecture, dependency or call-flow questions, load the `graphify` skill first. If `graphify-out/graph.json` exists, query the existing graph with `graphify query "<question>"` rather than rebuilding it.
- Treat the graph as a navigation aid: verify relevant source before editing, especially during the uncommitted migration. If the CLI is unavailable, use the skill's graph traversal fallback; do not rebuild/install tooling just to answer a small question.
- Rebuild/update the graph only when needed or requested; exclude build artifacts and dependency trees. A bare `/graphify .` may scan a large corpus, so follow the skill's size checks before extraction.

## Architecture and contracts
- `launcher/Application.cpp` selects the UI; `launcher/api/WebUiHost.cpp` owns the WebView, router and API modules. Linux uses WebKitGTK 4.1, not QtWebEngine or bundled Chromium.
- Launcher operations remain in existing C++ tasks/models. React calls named RPC operations; do not reimplement installation, authentication or launch logic in TypeScript.
- For an RPC change, update `frontend/src/types/methods.ts`, `frontend/src/api/client.ts` (including `CONTRACT`) and event types as needed. Register new modules in `WebUiHost` and `launcher/CMakeLists.txt`.
- `system.methods` exposes the actual backend contract; `checkContract()` compares it with TypeScript in Vite dev mode. A successful TS build does not verify backend registration.
- Preserve bridge security: no generic shell/filesystem/network API, no webpage-supplied local paths. Use native file pickers or backend-owned handles; sensitive command/path settings require native confirmation.
- Production UI is served from `materialmc://app/`; preserve Vite `base: "./"` and the host CSP. Page-side fetches are not a substitute for backend provider APIs.
- Backend questions use `launcher/interaction/UserInteraction.*` and `api/PromptApi.*`; React renders them in `PromptHost.tsx`. Avoid adding launcher-specific Qt dialogs back into migrated tasks.
- New instance tasks must go through `InstanceList::wrapInstanceTask` and `TaskTracker`. Flush dirty component profiles before emitting creation success: staging commit moves the directory synchronously, so delayed saves otherwise target a deleted `.tmp` path.
- Use literal `t("Text %1", arg)` / `tn("%n …", count)` from `frontend/src/i18n`, not a context-first signature. `useI18nVersion()` subscribes components to translation changes.
- `frontend/scripts/extract-i18n.mjs` generates ignored `src/i18n/keys.generated.json`; edit source calls/context mappings, not generated JSON. npm dev/build/typecheck scripts run extraction automatically.

## Build and focused checks
- Read `docs/react-webview.md` for bridge/security/runtime details, but verify feature-status claims against code. README links largely describe upstream Prism, not this migration.
- For the existing local Ninja Debug tree: `cmake --build build --parallel 4`. Executable: `build/materialmc`; do not run a stale `prismlauncher` binary.
- Frontend-only verification: `npm --prefix frontend run typecheck` or `npm --prefix frontend run build`. There is no frontend test/lint script in `package.json`.
- CMake runs `npm ci` from the committed lockfile, builds `frontend/dist`, then stages it into `build/frontend`. A standalone npm build does not update the UI served by `build/materialmc`; run CMake afterward.
- Fresh system-dependency build: `cmake -S . -B build-local -G Ninja -DCMAKE_BUILD_TYPE=Debug -DLauncher_ENABLE_WEBUI=ON -DBUILD_TESTING=ON`. Needs the C++23/Qt toolchain, Node/npm and `webkit2gtk-4.1`/`gtk+-3.0` development packages; explicit WEBUI=ON prevents silent Qt-only configuration.
- `CMakePresets.json` is the vcpkg + Ninja Multi-Config CI path. Do not apply presets to the existing single-config Ninja `build/`; inspect its cache or use a separate build directory.
- Local `build/CMakeCache.txt` currently has `BUILD_TESTING=OFF`. `ctest --test-dir build` finding no tests is not a passing test suite. With tests enabled, use `ctest --test-dir <dir> --output-on-failure --no-tests=error`; focused example: `ctest --test-dir <dir> -R '^ModrinthUrl$' --output-on-failure --no-tests=error`.
- Java helper CMake files request `-source 7 -target 7`; JDK 21 rejects that. The local tree uses untracked `build/tools/javac` to compile with `--release 8` via `Java_JAVAC_EXECUTABLE`/`CMAKE_Java_COMPILER`. Do not claim Java 7 compatibility from this workaround or assume it exists in a fresh checkout.
- CI disables PCH for cached Debug builds (`Launcher_USE_PCH=OFF`): comment-only header edits previously produced stale sccache PCH checksums. Preserve this unless the cache interaction is addressed.
- C++ formatting is `.clang-format` (Chromium-derived, 4 spaces, 140 columns). Clang-tidy CI generates `autogen`/`autorcc` with PCH off before diff-based analysis; there is no standalone frontend lint pipeline.

## Runtime verification
- Dev UI: `npm --prefix frontend run dev`, then `MATERIALMC_DEV_URL=http://127.0.0.1:5173 ./build/materialmc -d "$PWD/build/webui-test-data"`. Dev URLs are loopback-only and gated to Debug/explicit dev-enabled builds.
- Always use an absolute, isolated `-d` directory for destructive/install tests; config is `materialmc.cfg`. Never test imports/removals against the user's normal launcher data.
- Local untracked harness, if present: from `build/webui-test-data`, run `./e2e.sh e2e-s3-api.js 90` or `./e2e.sh e2e-s3-legacy.js 150`. It temporarily injects JavaScript into the staged bundle and must not run concurrently with another harness or a frontend staging build.
- Harness exit status alone is insufficient: inspect `E2E DONE`/`E2E FAIL` and Critical lines in `build/webui-test-data/e2e.log`. Startup errors from older fixtures must be distinguished from failures of the tested instance.
- `canLaunch: true` and task completion are not proof Minecraft launches. For imports, inspect committed `mmc-pack.json`, not only API details; verify launch separately.
- Browser preview without the native host returns `BRIDGE_UNAVAILABLE`; it cannot establish RPC/install correctness. Screenshots require a real graphical session; the local screenshot helpers use Hyprland/grim.
