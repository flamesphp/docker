<?php

/**
 * Flames Ready – Example worker script.
 *
 * -----------------------------------------------------------------------
 * Mode A – Apache mod_php / PHP-FPM (recommended for Apache stacks)
 * -----------------------------------------------------------------------
 * Drop this file on your include path and require it from your
 * application's entry point (public/index.php).  The extension's
 * RINIT/RSHUTDOWN hooks call load/reset callbacks automatically.
 *
 * -----------------------------------------------------------------------
 * Mode B – Standalone CLI worker (FrankenPHP-style loop)
 * -----------------------------------------------------------------------
 * Run:  php worker.php
 * The worker loads the application once and loops handling requests.
 *
 * -----------------------------------------------------------------------
 */

declare(strict_types=1);

require_once __DIR__ . '/AppLoader.php';
require_once __DIR__ . '/AppResetter.php';

// Register lifecycle callbacks (persistent for the entire worker lifetime).
xflames_ready_register_load('AppLoader',   'load');
xflames_ready_register_reset('AppResetter', 'reset');

// -----------------------------------------------------------------------
// Mode A: Apache mod_php / PHP-FPM
//   The extension calls load/reset automatically via RINIT/RSHUTDOWN.
//   Nothing else is needed here – just registering the callbacks above
//   is sufficient.  Your normal request-handling code continues below.
// -----------------------------------------------------------------------
if (PHP_SAPI !== 'cli') {
    // Application entry point – handle the current request normally.
    // require_once __DIR__ . '/../public/index.php';
    return;
}

// -----------------------------------------------------------------------
// Mode B: CLI worker loop (for custom dispatcher / socket server)
// -----------------------------------------------------------------------
$handled = xflames_ready_handle_request(function (): bool {
    // Simulate reading a request from a dispatcher (e.g. RoadRunner,
    // ReactPHP, or your own TCP/Unix socket server).
    $requestData = fgets(STDIN);
    if ($requestData === false || trim($requestData) === 'quit') {
        return false; // stop the loop
    }

    // Handle the request
    $response = 'Handled: ' . trim($requestData) . PHP_EOL;
    fwrite(STDOUT, $response);

    return true; // keep looping
});

echo "Worker finished. Total requests handled: {$handled}" . PHP_EOL;
