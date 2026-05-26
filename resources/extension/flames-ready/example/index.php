<?php

declare(strict_types=1);

// ── Flames Ready – register once per worker ───────────────────────────
if (!xflames_ready_is_ready()) {

    xflames_ready_register_load('AppBoot', 'load');
    xflames_ready_register_reset('AppReset', 'reset');

}

// ── Request handling ──────────────────────────────────────────────────
echo '<h1>Flames Ready</h1>';
echo '<p>Worker PID: ' . getmypid() . '</p>';
echo '<p>Requests handled by this worker: ' . xflames_ready_get_request_count() . '</p>';
echo '<p>App loaded: ' . (xflames_ready_is_ready() ? 'yes' : 'no') . '</p>';
echo '<p>Message: ' . AppBoot::getMessage() . '</p>';


// ── Load handler ──────────────────────────────────────────────────────
class AppBoot
{
    private static string $message = '';

    public static function load(): void
    {
        // Runs ONCE per worker process.
        // Load config, connect to DB, warm up caches, etc.
        self::$message = 'Loaded at ' . date('H:i:s');

        error_log('[Flames Ready] load – pid ' . getmypid());
    }

    public static function getMessage(): string
    {
        return self::$message;
    }
}

// ── Reset handler ─────────────────────────────────────────────────────
class AppReset
{
    public static function reset(): void
    {
        // Runs AFTER every request.
        // Clear per-request state so the next request starts clean.
        error_log('[Flames Ready] reset – requests so far: ' . xflames_ready_get_request_count());
    }
}
