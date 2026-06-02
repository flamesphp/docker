FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y
RUN apt-get install -y --no-install-recommends ca-certificates
RUN apt-get install -y --no-install-recommends curl
RUN apt-get install -y --no-install-recommends gnupg
RUN apt-get install -y --no-install-recommends software-properties-common

RUN add-apt-repository ppa:ondrej/php -y && apt-get update -y

RUN echo "Kernel: Install Nginx and PHP 8.5 CLI"
RUN apt-get install -y --no-install-recommends nginx
RUN apt-get install -y --no-install-recommends cron
RUN apt-get install -y --no-install-recommends libnginx-mod-http-headers-more-filter
RUN apt-get install -y --no-install-recommends openssl
RUN apt-get install -y --no-install-recommends php8.5-cli
RUN apt-get install -y --no-install-recommends php8.5-common
RUN apt-get install -y --no-install-recommends php8.5-bcmath
RUN apt-get install -y --no-install-recommends php8.5-ctype
RUN apt-get install -y --no-install-recommends php8.5-curl
RUN apt-get install -y --no-install-recommends php8.5-fileinfo
RUN apt-get install -y --no-install-recommends php8.5-gd
RUN apt-get install -y --no-install-recommends libjpeg-dev
RUN apt-get install -y --no-install-recommends libpng-dev
RUN apt-get install -y --no-install-recommends libwebp-dev
RUN apt-get install -y --no-install-recommends libgif-dev
RUN apt-get install -y --no-install-recommends libavif-dev
RUN apt-get install -y --no-install-recommends php8.5-intl
RUN apt-get install -y --no-install-recommends php8.5-mbstring
RUN apt-get install -y --no-install-recommends php8.5-mysql
RUN apt-get install -y --no-install-recommends php8.5-pgsql
RUN apt-get install -y --no-install-recommends php8.5-sqlite3
RUN apt-get install -y --no-install-recommends php8.5-xml
RUN apt-get install -y --no-install-recommends php8.5-xsl
RUN apt-get install -y --no-install-recommends php8.5-zip

RUN echo "Kernel: Install MongoDB extension"
RUN apt-get install -y --no-install-recommends php8.5-mongodb

RUN echo "Kernel: Install extra extensions"
RUN apt-get install -y --no-install-recommends php8.5-igbinary
RUN apt-get install -y --no-install-recommends php8.5-redis
RUN apt-get install -y --no-install-recommends libyaml-dev
RUN apt-get install -y --no-install-recommends php8.5-yaml
RUN apt-get install -y --no-install-recommends libmagickwand-dev
RUN apt-get install -y --no-install-recommends php8.5-imagick
RUN apt-get install -y --no-install-recommends wkhtmltopdf
RUN apt-get install -y --no-install-recommends ffmpeg

RUN echo "Kernel: Install RocksDB"
RUN apt-get install -y --no-install-recommends librocksdb-dev

RUN echo "Kernel: Install build tools for C extensions"
RUN apt-get install -y --no-install-recommends build-essential
RUN apt-get install -y --no-install-recommends php8.5-dev

RUN echo "Kernel: Compile c-async-service"
COPY vendor/flamesphp/c-async-service /tmp/c-async-service
RUN cd /tmp/c-async-service \
 && phpize8.5 \
 && ./configure --with-php-config=/usr/bin/php-config8.5 \
 && make -j$(nproc) \
 && make install \
 && EXT=$(ls modules/*.so | head -1 | xargs -I{} basename {} .so) \
 && echo "extension=${EXT}.so" > /etc/php/8.5/mods-available/${EXT}.ini \
 && phpenmod -v 8.5 ${EXT} \
 && rm -rf /tmp/c-async-service

RUN echo "Kernel: Compile c-ready-service"
COPY vendor/flamesphp/c-ready-service /tmp/c-ready-service
RUN cd /tmp/c-ready-service \
 && phpize8.5 \
 && ./configure --enable-cflames-ready-service --with-php-config=/usr/bin/php-config8.5 \
 && make -j$(nproc) \
 && make install \
 && EXT=$(ls modules/*.so | head -1 | xargs -I{} basename {} .so) \
 && echo "extension=${EXT}.so" > /etc/php/8.5/mods-available/${EXT}.ini \
 && phpenmod -v 8.5 ${EXT} \
 && rm -rf /tmp/c-ready-service

RUN echo "Kernel: Generate self-signed certificate"
RUN openssl req -x509 -nodes -days 3650 -newkey rsa:2048 \
    -keyout /etc/ssl/private/flames-selfsigned.key \
    -out    /etc/ssl/certs/flames-selfsigned.crt \
    -subj   "/C=BR/ST=SP/L=SaoPaulo/O=Flames/CN=localhost"

RUN echo "Nginx: Inject configuration"
RUN rm -f /etc/nginx/sites-enabled/default
COPY vendor/flamesphp/docker/resources/app/nginx_flames/nginx-main.conf /etc/nginx/nginx.conf
COPY vendor/flamesphp/docker/resources/app/nginx_flames/nginx.conf /etc/nginx/sites-available/flames.conf
RUN ln -s /etc/nginx/sites-available/flames.conf /etc/nginx/sites-enabled/flames.conf

COPY vendor/flamesphp/docker/resources/app/nginx_flames/opcache.ini /etc/php/8.5/cli/conf.d/99-opcache-settings.ini
COPY vendor/flamesphp/docker/resources/app/nginx_flames/performance.ini /etc/php/8.5/cli/conf.d/98-performance.ini
COPY vendor/flamesphp/docker/resources/app/nginx_flames/cron.sh /usr/local/bin/flames-cron
COPY vendor/flamesphp/docker/resources/app/nginx_flames/ready-watchdog.sh /usr/local/bin/flames-ready-watchdog
RUN chmod +x /usr/local/bin/flames-cron /usr/local/bin/flames-ready-watchdog
RUN printf '* * * * * root /usr/local/bin/flames-cron\n' > /etc/cron.d/flames
RUN chmod 0644 /etc/cron.d/flames

COPY vendor/flamesphp/docker/resources/app/nginx_flames/start.sh /start.sh
RUN chmod +x /start.sh

WORKDIR /var/www/html

EXPOSE 80 443

CMD ["/start.sh"]
