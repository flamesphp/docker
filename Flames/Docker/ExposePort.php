<?php
declare(strict_types=1);


namespace Flames\Docker;

/**
 * Before docker compose up: pick host port per service (PORT, else PORT_FORWARDED).
 * Keeps mysql + mariadb (and others) on distinct ports at the same time.
 *
 * @internal
 */
final class ExposePort
{
    /** @var array<string, string> */
    private const PREFIX_TO_SERVICE = [
        'DATABASE_MYSQL'                  => 'mysql',
        'DATABASE_MARIADB'                => 'mariadb',
        'DATABASE_POSTGRESQL'             => 'postgresql',
        'DATABASE_MONGODB'                => 'mongodb',
        'DATABASE_MEMORY_KEYDB'           => 'keydb',
        'DATABASE_MEMORY_REDIS'           => 'redis',
        'DATABASE_MEMORY_DRAGONFLY'      => 'dragonfly',
        'DATABASE_MEMORY_VALKEY'          => 'valkey',
        'DATABASE_MEMORY_ROCKSDB'        => 'rocksdb',
        'DATABASE_SEARCH_MEILISEARCH'     => 'meilisearch',
        'DATABASE_SEARCH_OPENSEARCH'      => 'opensearch',
        'DATABASE_SEARCH_ELASTICSEARCH'   => 'elasticsearch',
    ];

    /**
     * @return list<string>
     */
    public static function prefixes(): array
    {
        return array_keys(self::PREFIX_TO_SERVICE);
    }

    public static function resolveAll(): void
    {
        /** @var array<int, true> $claimed */
        $claimed = [];

        foreach (self::prefixes() as $prefix) {
            self::resolveIntoEnvironment($prefix, $claimed);
        }
    }

    /**
     * @param array<int, true> $claimed
     */
    public static function resolveIntoEnvironment(string $prefix, array &$claimed = []): ?int
    {
        $primary  = self::envInt($prefix . '_PORT');
        $fallback = self::envInt($prefix . '_PORT_FORWARDED');
        $service  = self::PREFIX_TO_SERVICE[$prefix] ?? null;

        if ($primary === null && $fallback === null) {
            return null;
        }

        $resolved = self::pickPort($primary, $fallback, $service, $claimed);

        if ($resolved === null) {
            return null;
        }

        $claimed[$resolved] = true;
        self::setEnv($prefix . '_PORT', (string) $resolved);

        return $resolved;
    }

    /**
     * @param array<int, true> $claimed
     */
    private static function pickPort(?int $primary, ?int $fallback, ?string $service, array $claimed): ?int
    {
        if ($primary !== null
            && !isset($claimed[$primary])
            && self::canUsePort($service, $primary)) {
            return $primary;
        }

        if ($fallback !== null
            && $fallback !== $primary
            && !isset($claimed[$fallback])
            && self::canUsePort($service, $fallback)) {
            return $fallback;
        }

        if ($primary !== null && !isset($claimed[$primary])) {
            return $primary;
        }

        if ($fallback !== null && !isset($claimed[$fallback])) {
            return $fallback;
        }

        return $primary ?? $fallback;
    }

    private static function canUsePort(?string $service, int $port): bool
    {
        if (self::isAvailable($port)) {
            return true;
        }

        return $service !== null && self::isPublishedByFlamesService($service, $port);
    }

    public static function isAvailable(int $port): bool
    {
        if ($port <= 0 || $port > 65535) {
            return false;
        }

        $socket = @stream_socket_server('tcp://127.0.0.1:' . $port, $errno, $errstr);

        if ($socket === false) {
            return false;
        }

        fclose($socket);

        return true;
    }

    private static function isPublishedByFlamesService(string $service, int $hostPort): bool
    {
        static $cache = [];

        $cacheKey = $service . ':' . $hostPort;
        if (array_key_exists($cacheKey, $cache)) {
            return $cache[$cacheKey];
        }

        $container = self::containerName($service);
        if ($container === null) {
            return $cache[$cacheKey] = false;
        }

        $output = [];
        exec('docker port ' . escapeshellarg($container) . ' 2>/dev/null', $output, $code);

        if ($code !== 0) {
            return $cache[$cacheKey] = false;
        }

        foreach ($output as $line) {
            if (preg_match('/:' . preg_quote((string) $hostPort, '/') . '$/', $line) === 1) {
                return $cache[$cacheKey] = true;
            }
        }

        return $cache[$cacheKey] = false;
    }

    private static function containerName(string $service): ?string
    {
        $root = defined('ROOT_PATH') ? ROOT_PATH : getcwd() . DIRECTORY_SEPARATOR;
        $project = strtolower(basename(rtrim($root, '/\\')));

        if ($project === '') {
            return null;
        }

        return $project . '-' . $service . '-1';
    }

    private static function envInt(string $key): ?int
    {
        $value = getenv($key);

        if ($value === false) {
            $value = $_ENV[$key] ?? null;
        }

        if ($value === null || $value === '') {
            return null;
        }

        return (int) $value;
    }

    private static function setEnv(string $key, string $value): void
    {
        putenv($key . '=' . $value);
        $_ENV[$key]    = $value;
        $_SERVER[$key] = $value;
    }
}
