# INTRUCTIONS.md

AI-oriented reference for the **flamesphp/docker** package. Use this file when vibe-coding changes to Docker services, port exposure, or the Stealth VNC cursor stack.

## Repository at a glance

| Area | Path | Role |
|------|------|------|
| PHP library | `Flames/Docker/` | Runtime helpers: detect Docker, resolve host ports before `docker compose up` |
| App stacks | `resources/app/` | Nginx/Apache + PHP-FPM/mod_php Docker images and compose fragments |
| Databases | `resources/database/` | MySQL, MariaDB, PostgreSQL, MongoDB compose fragments |
| Memory stores | `resources/memory/` | Redis, KeyDB, Valkey, Dragonfly, Kvrocks |
| Search | `resources/search/` | Meilisearch, OpenSearch, Elasticsearch |
| Stealth service | `resources/service/stealth/` | Headless Chromium + noVNC + cursor overlay for remote browser automation |
| SonarQube service | `resources/service/sonarqube/` | SonarQube Community Build + dedicated PostgreSQL for code quality analysis |

**Composer package:** `flamesphp/docker` (PHP `>=8.5`, MIT).

**Environment convention for exposed services:** each service uses `{PREFIX}_PORT` (preferred) and `{PREFIX}_PORT_FORWARDED` (fallback). Example: `DATABASE_MYSQL_PORT`, `DATABASE_MYSQL_PORT_FORWARDED`. Compose files reference the resolved value, e.g. `"${DATABASE_MYSQL_PORT:-${DATABASE_MYSQL_PORT_FORWARDED:-3307}}:3306"`.

---

## PHP classes

Namespace: `Flames\Docker`

### `Flames\Docker\Docker`

Static utility to detect whether PHP is running inside a Docker container. Result is cached for the lifetime of the request/process.

| Method | Visibility | Signature | Behavior |
|--------|------------|-----------|----------|
| `isDocker` | public static | `isDocker(): bool` | Returns `true` if `/.dockerenv` exists on the filesystem, otherwise `false`. The first call stores the result in a private static `$isDocker` property; subsequent calls return the cached value without re-checking the file. |

**Typical use:** branch application bootstrap, paths, or logging when code runs in a container vs. on the host.

```php
use Flames\Docker\Docker;

if (Docker::isDocker()) {
    // running inside a container
}
```

---

### `Flames\Docker\ExposePort`

`@internal` — resolves host ports for Flames Docker services **before** `docker compose up`, so multiple databases/memory/search services can run on distinct host ports without manual conflict resolution.

Maps environment prefixes to Docker Compose service names:

| Prefix | Compose service |
|--------|-----------------|
| `DATABASE_MYSQL` | `mysql` |
| `DATABASE_MARIADB` | `mariadb` |
| `DATABASE_POSTGRESQL` | `postgresql` |
| `DATABASE_MONGODB` | `mongodb` |
| `DATABASE_MEMORY_KEYDB` | `keydb` |
| `DATABASE_MEMORY_REDIS` | `redis` |
| `DATABASE_MEMORY_DRAGONFLY` | `dragonfly` |
| `DATABASE_MEMORY_VALKEY` | `valkey` |
| `DATABASE_MEMORY_KVROCKS` | `kvrocks` |
| `DATABASE_SEARCH_MEILISEARCH` | `meilisearch` |
| `DATABASE_SEARCH_OPENSEARCH` | `opensearch` |
| `DATABASE_SEARCH_ELASTICSEARCH` | `elasticsearch` |

Container naming convention: `{project}-{service}-1`, where `project` is the lowercase basename of `ROOT_PATH` (or `getcwd()` if `ROOT_PATH` is undefined).

#### Public methods

| Method | Signature | Behavior |
|--------|-----------|----------|
| `prefixes` | `prefixes(): array` | Returns the list of all supported environment prefixes (keys of `PREFIX_TO_SERVICE`). |
| `resolveAll` | `resolveAll(): void` | Iterates every prefix and calls `resolveIntoEnvironment()` with a shared `$claimed` map so no two services get the same host port in one run. |
| `resolveIntoEnvironment` | `resolveIntoEnvironment(string $prefix, array &$claimed = []): ?int` | Reads `{PREFIX}_PORT` and `{PREFIX}_PORT_FORWARDED` from the environment. If both are missing, returns `null`. Otherwise picks a port via `pickPort()`, marks it in `$claimed`, writes the chosen port back to `{PREFIX}_PORT` via `setEnv()`, and returns the port integer. |
| `isAvailable` | `isAvailable(int $port): bool` | Returns `false` for ports outside `1–65535`. Otherwise tries to bind `tcp://127.0.0.1:{port}` with `stream_socket_server()`. If binding succeeds, closes the socket and returns `true` (port is free on localhost). |

#### Private methods

| Method | Signature | Behavior |
|--------|-----------|----------|
| `pickPort` | `pickPort(?int $primary, ?int $fallback, ?string $service, array $claimed): ?int` | Port selection priority: (1) primary if not already claimed and `canUsePort()` allows it; (2) fallback if different from primary, not claimed, and usable; (3) primary if not claimed (even if busy); (4) fallback if not claimed; (5) `primary ?? fallback`. |
| `canUsePort` | `canUsePort(?string $service, int $port): bool` | Returns `true` if `isAvailable($port)` **or** the port is already published by the same Flames service container (`isPublishedByFlamesService`). Allows reusing a port when that service’s container is already running. |
| `isPublishedByFlamesService` | `isPublishedByFlamesService(string $service, int $hostPort): bool` | Runs `docker port {container}` (cached per `service:hostPort`). Parses output lines ending with `:{hostPort}` to detect if the Flames container already exposes that host port. |
| `containerName` | `containerName(string $service): ?string` | Builds `{project}-{service}-1` or returns `null` if project name is empty. |
| `envInt` | `envInt(string $key): ?int` | Reads an integer from `getenv()` or `$_ENV`. Returns `null` if unset or empty. |
| `setEnv` | `setEnv(string $key, string $value): void` | Sets the variable in `putenv()`, `$_ENV`, and `$_SERVER` so downstream PHP and child processes see the resolved port. |

**Resolution flow:**

```
resolveAll()
  └─ for each prefix
       └─ resolveIntoEnvironment(prefix, &$claimed)
            ├─ read PREFIX_PORT + PREFIX_PORT_FORWARDED
            ├─ pickPort() → canUsePort() → isAvailable() | isPublishedByFlamesService()
            ├─ claim port in $claimed
            └─ setEnv(PREFIX_PORT, resolved)
```

**Typical use:** call `ExposePort::resolveAll()` early in the Flames CLI/bootstrap path before merging compose files or starting containers.

---

## Python classes (Stealth service)

Path: `resources/service/stealth/cursor-server.py`

Per-tab cursor synchronization for the Stealth VNC stack: draws an X11 overlay cursor (white arrow, orange flash on click), moves the real X cursor via `xdotool`, and exposes a small HTTP API for Chromium tabs registered over CDP.

Global module state used by both classes: `registry` (tab id → `(x, y)`), `visible_target`, `focused_target`, `overlay` (`CursorOverlay` instance), `DISPLAY`, `CDP_URL`.

---

### `CursorOverlay`

Xlib-drawn cursor window with transparent background (X SHAPE extension + Pillow alpha mask). Always kept above other windows while visible.

**Class constants**

| Name | Value | Meaning |
|------|-------|---------|
| `CANVAS` | `96` | Overlay window size in pixels |
| `TIP_X`, `TIP_Y` | `4`, `4` | Arrow tip offset inside the canvas |
| `ARROW` | tuple of points | Polygon vertices for the arrow shape |

#### `__init__(self, display_name: str) -> None`

Connects to the X display, allocates white/black/orange colors, creates an override-redirect window, initializes SHAPE support, paints the initial arrow, and starts a background thread that raises the window every 50ms while visible.

#### Private helpers

| Method | Behavior |
|--------|----------|
| `_alloc(r, g, b)` | Allocates an X color from RGB 0–255 via the default colormap. |
| `_init_shape()` | Queries X SHAPE extension; sets module-level `shape_error` on failure. |
| `_gc_set(color_pixel, line_width=1)` | Updates the graphics context foreground and line width. |
| `_polyline(points)` | Draws a closed polyline outline (compat: `polyline` or `poly_line`). |

#### Drawing

| Method | Behavior |
|--------|----------|
| `_draw_arrow()` | Draws black outline, then fills the arrow white or orange depending on `_orange` (click flash state). |
| `_make_alpha()` | Builds a Pillow RGBA image; alpha channel becomes the SHAPE mask (opaque where the arrow is drawn). |
| `_apply_shape(alpha)` | Converts alpha to an X pixmap and applies SHAPE bounding + clip masks so non-arrow pixels are transparent. |
| `_paint()` | Clears canvas, redraws arrow, reapplies shape mask. |

#### Window management

| Method | Behavior |
|--------|----------|
| `_set_keep_above()` | Sets `_NET_WM_WINDOW_TYPE_NOTIFICATION` and `_NET_WM_STATE_ABOVE` so the overlay stays on top. |
| `_start_raise_loop()` | Daemon thread: every 50ms calls `raise_window()` while `_visible`. |
| `_raise()` | Raises overlay window and flushes the display connection. |
| `_window_origin(x, y)` | Converts cursor tip coordinates to top-left window position (`round(x) - TIP_X`, same for Y). |

#### Public API

| Method | Behavior |
|--------|----------|
| `show()` | Sets `_visible = true`, maps window at current `_x/_y`, raises, repaints. |
| `hide()` | Sets `_visible = false`, unmaps window. |
| `move(x, y)` | Updates position; maps window if needed; reconfigures geometry, raises, repaints. |
| `click(x, y)` | Like `move`, but sets `_orange = true` for click feedback, then starts `_flash_restore` in a daemon thread. |
| `_flash_restore()` | After `CLICK_FLASH_DURATION` (0.35s), clears orange and repaints if still visible. |

Thread-safe: all public methods acquire `self._lock`.

---

### `Handler` (`BaseHTTPRequestHandler`)

JSON HTTP API for tab cursor registration and movement. Default port from `STEALTH_CURSOR_PORT` (default `9230`). Access log suppressed via overridden `log_message`.

#### Request helpers

| Method | Behavior |
|--------|----------|
| `log_message(format, *args)` | No-op (silences default stderr logging). |
| `_read_json()` | Reads `Content-Length` bytes from the body and parses JSON object; empty body → `{}`. |
| `_ok(payload=None)` | Sends HTTP 200 with JSON (`{"ok": true}` or custom payload). |
| `_bad(message)` | Sends HTTP 400 with `{"error": message}`. |

#### HTTP routes

| Method | Path | Body | Behavior |
|--------|------|------|----------|
| `GET` | `/health` | — | Returns status: `visible`, `focused`, tab count, overlay state, errors, `DISPLAY`. |
| `POST` | `/register` | `{ "targetId": string, "x"?: float, "y"?: float }` | Stores tab position in `registry` (defaults 960, 540). |
| `POST` | `/unregister` | `{ "targetId": string }` | Removes tab; clears `visible_target` / `focused_target` if matched; hides overlay when no tabs left. |
| `POST` | `/move` | `{ "targetId", "x", "y" }` | Updates registry and calls `apply_move()` (xdotool + overlay). |
| `POST` | `/click` | `{ "targetId", "x", "y" }` | Updates registry and calls `apply_click()` (xdotool + orange flash). |
| `POST` | `/focus` | `{ "targetId" }` | Sets visible/focused target and applies cursor at registered or default position. |

Unknown paths → HTTP 404.

**Background workers (module functions, not class methods):**

- `init_overlay()` — waits for Chromium CDP, retries `CursorOverlay` creation up to 20 times.
- `poll_visible_tab()` — polls CDP tab list; when a registered tab’s `document.visibilityState` is `"visible"`, syncs cursor via `apply_cursor()`.
- `main()` — starts overlay init, polling thread, and `ThreadingHTTPServer` on `0.0.0.0:{port}`.

---

## Related scripts (no classes)

These files support Stealth but are procedural scripts, not classes:

| File | Purpose |
|------|---------|
| `dismiss-theme-bar.py` | Dismisses Chromium “Installed theme” infobar via CDP navigation |
| `cursor-server.py` | Entry point: `python3 cursor-server.py` or via `start.sh` |
| `start.sh`, `chromium-launch.sh`, etc. | Container boot orchestration |

When editing Stealth behavior, prefer changing `cursor-server.py` classes or their HTTP contract rather than shell wrappers unless startup order is involved.

---

## Guidelines for AI-assisted changes

1. **Port changes** — Update the matching `PREFIX_TO_SERVICE` entry in `ExposePort` if adding a new compose service with `{PREFIX}_PORT` / `{PREFIX}_PORT_FORWARDED`. Keep prefix names aligned with `.yml` files under `resources/`.
2. **Docker detection** — `Docker::isDocker()` only checks `/.dockerenv`. Do not reintroduce OS checks unless you have a concrete cross-platform requirement.
3. **Stealth cursor** — HTTP API paths and JSON field names (`targetId`, `x`, `y`) are consumed by browser-side scripts; keep backward compatibility or update all callers.
4. **Scope** — `resources/` holds Docker assets (Dockerfiles, configs, compose fragments); `Flames/Docker/` holds the small PHP API consumed by the main Flames framework. Match existing naming and env var patterns in sibling files.
5. **Do not** commit secrets (`.env`, passwords). Compose files use env var placeholders with safe defaults for local dev.

---

## Quick reference: file → class map

```
Flames/Docker/Docker.php          → Flames\Docker\Docker
Flames/Docker/ExposePort.php      → Flames\Docker\ExposePort
resources/service/stealth/cursor-server.py
                                  → CursorOverlay, Handler
```
