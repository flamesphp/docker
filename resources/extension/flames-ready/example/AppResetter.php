<?php

/**
 * Example reset handler.
 *
 * Called AFTER every handled request.
 * Use this to wipe per-request state so the next request starts clean:
 * clear static caches, reset global variables, release per-request
 * resources, etc.
 */
class AppResetter
{
    public static function reset(): void
    {
        // Clear any per-request static caches
        // RequestCache::clear();

        // Reset per-request service container bindings
        // Container::resetRequest();

        // Release per-request database transactions (if any)
        // Database::rollbackIfActive();

        error_log('[Flames Ready] Worker reset – requests handled: '
            . xflames_ready_get_request_count());
    }
}
