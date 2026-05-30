FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -y
RUN apt-get install -y --no-install-recommends ca-certificates
RUN apt-get install -y --no-install-recommends curl
RUN apt-get install -y --no-install-recommends gnupg
RUN apt-get install -y --no-install-recommends software-properties-common

RUN add-apt-repository ppa:ondrej/php -y && apt-get update -y

RUN echo "Kernel: Install Apache2, mod_security and PHP 8.5 FPM"
RUN apt-get install -y --no-install-recommends apache2
RUN apt-get install -y --no-install-recommends cron
RUN apt-get install -y --no-install-recommends openssl
RUN apt-get install -y --no-install-recommends libapache2-mod-security2
RUN apt-get install -y --no-install-recommends php8.5-fpm
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
RUN apt-get install -y --no-install-recommends php8.5-xdebug
RUN apt-get install -y --no-install-recommends php8.5-zip

RUN echo "Kernel: Install MongoDB extension"
RUN apt-get install -y --no-install-recommends php8.5-mongodb

RUN echo "Kernel: Generate self-signed certificate"
RUN openssl req -x509 -nodes -days 3650 -newkey rsa:2048 \
    -keyout /etc/ssl/private/flames-selfsigned.key \
    -out    /etc/ssl/certs/flames-selfsigned.crt \
    -subj   "/C=BR/ST=SP/L=SaoPaulo/O=Flames/CN=localhost"

RUN echo "Apache: Enable modules"
RUN a2enmod rewrite headers ssl proxy_fcgi setenvif security2

RUN echo "Apache: Enable PHP-FPM conf"
RUN a2enconf php8.5-fpm

RUN echo "ServerName localhost" >> /etc/apache2/apache2.conf

RUN echo "Apache: Configure security headers"
RUN a2disconf security 2>/dev/null || true
COPY image/apache_phpfpm/security.conf /etc/apache2/conf-available/flames-security.conf
RUN a2enconf flames-security

RUN echo "Apache: Inject http.conf"
COPY image/apache_phpfpm/http.conf /etc/apache2/sites-available/flames.conf

RUN echo "Apache: Enable virtualhost"
RUN a2dissite 000-default default-ssl 2>/dev/null || true \
 && a2ensite flames

COPY image/apache_phpfpm/xdebug.ini /etc/php/8.5/fpm/conf.d/99-xdebug-settings.ini

COPY image/apache_phpfpm/cron.sh /usr/local/bin/flames-cron
RUN chmod +x /usr/local/bin/flames-cron
RUN printf '* * * * * root /usr/local/bin/flames-cron\n' > /etc/cron.d/flames
RUN chmod 0644 /etc/cron.d/flames

COPY image/apache_phpfpm/start.sh /start.sh
RUN chmod +x /start.sh

WORKDIR /var/www/html

EXPOSE 80 443

CMD ["/start.sh"]
