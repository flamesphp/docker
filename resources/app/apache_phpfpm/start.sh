#!/bin/bash
set -e

XDEBUG_MODE=$([ "${XDEBUG:-true}" = "true" ] && echo "debug,develop" || echo "off")
echo "xdebug.mode=${XDEBUG_MODE}" >> /etc/php/8.5/fpm/conf.d/99-xdebug-settings.ini

IDE_KEY=$(echo "${IDE:-phpstorm}" | tr '[:lower:]' '[:upper:]')
echo "xdebug.idekey=${IDE_KEY}" >> /etc/php/8.5/fpm/conf.d/99-xdebug-settings.ini

cron
php-fpm8.5 --daemonize --allow-to-run-as-root
exec apache2ctl -D FOREGROUND
