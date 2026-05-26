<?php

/**
 * Example load handler.
 *
 * Called ONCE when the PHP worker process becomes active.
 * Use this to bootstrap your application: load config, instantiate
 * shared services, warm caches, establish DB connections, etc.
 */
class AppLoader
{
    public static function load(): void
    {
        // Load application config (once per worker lifetime)
        // Config::load(__DIR__ . '/../config/app.php');

        // Establish a shared database connection
        // Database::connect(Config::get('db'));

        // Warm up class autoloader / preload critical classes
        // require_once __DIR__ . '/../vendor/autoload.php';

        error_log('[Flames Ready] Worker loaded – pid ' . getmypid());
    }
}
