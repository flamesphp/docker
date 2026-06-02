#!/bin/bash
cd /var/www/html
/usr/bin/php8.5 ./forge --cron >> /proc/1/fd/1 2>> /proc/1/fd/2
