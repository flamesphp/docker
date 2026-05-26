FROM php:8.5-cli
USER root

RUN apt-get update

# Install system dependencies
RUN apt-get install -y apt-utils
RUN apt-get install -y vim
RUN apt-get install -y git
RUN apt-get install -y unzip
RUN apt-get install -y zip
RUN apt-get install -y zlib1g-dev
RUN apt-get install -y libbz2-dev
RUN apt-get install -y mariadb-client
RUN apt-get install -y libpng-dev
RUN apt-get install -y libonig-dev
RUN apt-get install -y libxml2-dev
RUN apt-get install -y libwebp-dev
RUN apt-get install -y libfreetype6-dev
RUN apt-get install -y libjpeg62-turbo-dev
RUN apt-get install -y libpng-dev
RUN apt-get install -y libzip-dev
RUN apt-get install -y libicu-dev
RUN apt-get install -y libxslt-dev
RUN apt-get install -y g++
RUN apt-get install -y autoconf
RUN apt-get install -y make

# Install PHP Extensions (default)
RUN docker-php-ext-install -j$(nproc) iconv
RUN docker-php-ext-install mysqli
RUN docker-php-ext-install pdo
RUN docker-php-ext-install pdo_mysql
RUN docker-php-ext-install zip
RUN docker-php-ext-install bz2
RUN docker-php-ext-install pcntl
RUN docker-php-ext-install mbstring
RUN docker-php-ext-install exif
RUN docker-php-ext-install pcntl
RUN docker-php-ext-install bcmath
RUN docker-php-ext-install xsl

# PHP Extension GD
RUN docker-php-ext-configure gd --enable-gd --with-freetype --with-jpeg --with-webp
RUN docker-php-ext-install gd

# PHP Extension INTL
RUN docker-php-ext-configure intl
RUN docker-php-ext-install -j$(nproc) intl

# PHP XDEBUG
#RUN pecl install xdebug
#RUN docker-php-ext-enable xdebug

# PHP INI
COPY data/php-flames-ready/php.ini /usr/local/etc/php/conf.d/z-flames.ini

# Flames Ready extension
COPY extension/flames-ready /usr/src/flames-ready
RUN cd /usr/src/flames-ready \
    && phpize \
    && ./configure --enable-flames-ready \
    && make -j$(nproc) \
    && make install \
    && rm -rf /usr/src/flames-ready

RUN echo "extension=flames_ready.so"           >  /usr/local/etc/php/conf.d/50-flames-ready.ini \
 && echo "flames_ready.worker_mode  = 1"        >> /usr/local/etc/php/conf.d/50-flames-ready.ini \
 && echo "flames_ready.preload_once = 1"        >> /usr/local/etc/php/conf.d/50-flames-ready.ini \
 && echo "flames_ready.max_requests = 0"        >> /usr/local/etc/php/conf.d/50-flames-ready.ini \
 && echo "flames_ready.socket       = 9001"     >> /usr/local/etc/php/conf.d/50-flames-ready.ini

# Clear cache
RUN apt-get clean && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /var/www/html
