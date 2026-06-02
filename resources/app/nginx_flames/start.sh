#!/bin/bash
set -e

mkdir -p /var/run/flames-ready

cat > /etc/php/8.5/cli/conf.d/97-flames-ready.ini <<EOF
[flames_ready_service]
flames_ready_service.workers        = ${READY_WORKERS:-0}
flames_ready_service.worker_ttl     = ${READY_WORKER_TTL:-300}
flames_ready_service.worker_timeout = ${READY_WORKER_TIMEOUT:-900}
EOF

cron

cd /var/www/html && /usr/bin/php8.5 ./forge --ready >> /proc/1/fd/1 2>> /proc/1/fd/2 &

exec nginx -g 'daemon off;'
