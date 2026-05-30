#!/bin/bash
set -e

cron
php-fpm8.5 --daemonize --allow-to-run-as-root

cd /var/www/html && /usr/bin/php8.5 ./forge --boot &

exec nginx -g 'daemon off;'
