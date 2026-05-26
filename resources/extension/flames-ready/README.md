# Flames Ready

A PHP C extension that brings FrankenPHP-style **persistent worker** behaviour
to a standard **Apache + PHP** stack.

Instead of spinning up a fresh PHP environment for every request, Flames Ready
keeps PHP workers alive between requests.  Application classes and shared state
are loaded once per worker process; only the per-request "dirty" state is reset
between requests.

---

## How it works

```
Worker process starts
        │
        ▼
   [ LOAD callbacks ]   ← called once: bootstrap app, load classes, warm caches
        │
        ▼  ┌─────────────────────────────────────────────────────┐
   ┌────┤  │                                                     │
   │    ▼  ▼                                                     │
   │  Incoming request is handled (normal PHP execution)         │
   │    │                                                         │
   │    ▼                                                         │
   │  [ RESET callbacks ]  ← clear per-request state             │
   │    │                                                         │
   └────┘  (worker waits for next request, classes stay in RAM)   │
           └─────────────────────────────────────────────────────┘
```

---

## PHP API

### Register a load callback

```php
xflames_ready_register_load(string $class, string $method): bool
```

Registers a static method to be called **once** when the worker becomes active.
Use this to bootstrap your application.

```php
xflames_ready_register_load('App\Bootstrap', 'load');
xflames_ready_register_load('App\Database',  'connect');
```

---

### Register a reset callback

```php
xflames_ready_register_reset(string $class, string $method): bool
```

Registers a static method to be called **after every handled request**.
Use this to wipe per-request state so the next request starts clean.

```php
xflames_ready_register_reset('App\RequestContext', 'clear');
xflames_ready_register_reset('App\Database',       'rollbackIfActive');
```

---

### Worker loop (CLI mode)

```php
xflames_ready_handle_request(callable $handler): int
```

Enters the persistent request loop (intended for CLI worker processes
that receive requests from a custom dispatcher, socket server, or
a tool like RoadRunner).

- Invokes load callbacks once before the first request.
- Calls `$handler()` for each request.
- Calls reset callbacks after each `$handler()` call.
- Stops when `$handler` returns `false` or `max_requests` is reached.
- Returns the total number of requests handled.

```php
xflames_ready_handle_request(function (): bool {
    $req = fgets(STDIN);
    if (!$req) return false;

    // handle request …

    return true; // keep looping
});
```

---

### Utility functions

```php
xflames_ready_is_ready(): bool          // true if load callbacks were already called
xflames_ready_get_request_count(): int  // total requests handled by this worker
```

---

## Integration modes

### Mode A – Apache mod_php / PHP-FPM (recommended)

Register your callbacks in your application's entry point
(e.g. `public/index.php`) **before** any request logic.  The extension hooks
into PHP's `RINIT` and `RSHUTDOWN` to call them automatically:

| Hook        | When                                       | Action               |
|-------------|--------------------------------------------|----------------------|
| `RINIT`     | First request of this worker process       | Invoke load callbacks |
| `RSHUTDOWN` | After every request                        | Invoke reset callbacks |

```php
// public/index.php
xflames_ready_register_load('Bootstrap', 'load');
xflames_ready_register_reset('RequestState', 'clear');

// … rest of your application …
```

PHP-FPM keeps worker processes alive across requests (`pm.max_requests`
controls how many requests a worker handles before being recycled).
Combine with `opcache.validate_timestamps = 0` for maximum performance.

---

### Mode B – CLI worker loop

Useful for long-running PHP processes that receive requests from an
external dispatcher (RoadRunner, ReactPHP, custom socket server, etc.).

```php
// worker.php
require 'vendor/autoload.php';

xflames_ready_register_load('App\Bootstrap',   'load');
xflames_ready_register_reset('App\RequestCtx', 'clear');

xflames_ready_handle_request(function (): bool {
    $payload = readRequestFromSocket();
    if ($payload === null) return false;

    handleRequest($payload);
    return true;
});
```

Run multiple workers in parallel:

```bash
for i in $(seq 1 8); do php worker.php & done
```

---

## INI settings

| Setting                       | Default | Description |
|-------------------------------|---------|-------------|
| `flames_ready.worker_mode`    | `0`     | `1` = disable automatic RINIT/RSHUTDOWN hooks (use manual loop only) |
| `flames_ready.preload_once`   | `1`     | `1` = call load callbacks only once per worker process |
| `flames_ready.max_requests`   | `0`     | Max requests per worker before loop exits (0 = unlimited) |

```ini
; /usr/local/etc/php/conf.d/50-flames-ready.ini
extension=flames_ready.so

flames_ready.worker_mode  = 0
flames_ready.preload_once = 1
flames_ready.max_requests = 0
```

---

## Build & install

### Using Docker (recommended)

```yaml
# docker-compose.yml
services:
  php:
    build:
      context: ./flames-ready
```

### Manually inside the container / on the server

```bash
cd flames-ready
phpize
./configure --enable-flames-ready
make -j$(nproc)
make install
echo "extension=flames_ready.so" > /usr/local/etc/php/conf.d/50-flames-ready.ini
```

### Verify

```bash
php -m | grep flames_ready
php -r "var_dump(xflames_ready_is_ready());"
```

---

## Comparison with FrankenPHP

| Feature                        | FrankenPHP                  | Flames Ready                          |
|--------------------------------|-----------------------------|---------------------------------------|
| Web server                     | Replaces Apache/nginx       | Works **with** Apache + PHP-FPM       |
| Worker process management      | Built-in (Go runtime)       | Delegated to PHP-FPM / OS             |
| Class preloading                | Worker script stays alive   | OPcache + load callbacks              |
| Per-request reset               | Manual in worker loop       | Automatic (RSHUTDOWN) or manual loop  |
| Language                       | Go + PHP C extension        | Pure PHP C extension                  |
| Requires server change         | Yes (replaces web server)   | **No** – drop-in extension            |

---

## Extension name

`flames_ready` (module) / `Flames Ready` (display name)

Functions are prefixed with `xflames_ready_` to avoid conflicts with
other extensions.
