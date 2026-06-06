FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        chromium \
        xvfb \
        x11vnc \
        novnc \
        websockify \
        nginx \
        socat \
        tini \
        curl \
        procps \
        dbus \
        dbus-x11 \
        xclip \
        autocutsel \
        libegl1 \
        libgbm1 \
        libgl1 \
        libgl1-mesa-dri \
        mesa-vulkan-drivers \
        pulseaudio \
        pulseaudio-utils \
        libasound2 \
        ffmpeg \
        fonts-liberation \
        fonts-noto-color-emoji \
        ca-certificates \
        xdotool \
        python3 \
        python3-websockets \
    && rm -rf /var/lib/apt/lists/* \
    && rm -f /etc/nginx/sites-enabled/default \
    && rm -f /etc/chromium/master_preferences

RUN useradd --create-home --shell /bin/bash --uid 1001 flames \
    && usermod -aG audio flames \
    && mkdir -p /var/lib/flames-chromium/Default /home/flames/.runtime/pulse \
    && chown -R flames:flames /var/lib/flames-chromium /home/flames

COPY vendor/flamesphp/docker/resources/service/stealth/nginx-novnc.conf /etc/nginx/novnc.conf
COPY vendor/flamesphp/docker/resources/service/stealth/novnc-index.html /opt/flames-novnc/index.html
COPY vendor/flamesphp/docker/resources/service/stealth/chromium/policies.json /etc/chromium/policies/managed/flames.json
COPY vendor/flamesphp/docker/resources/service/stealth/chromium/Preferences /opt/flames-chromium/Default/Preferences
COPY vendor/flamesphp/docker/resources/service/stealth/chromium/Bookmarks /opt/flames-chromium/Default/Bookmarks
COPY vendor/flamesphp/docker/resources/service/stealth/chromium/local-state.json /opt/flames-chromium/local-state.json
COPY vendor/flamesphp/docker/resources/service/stealth/start.sh /start.sh
COPY vendor/flamesphp/docker/resources/service/stealth/tab-guard.sh /tab-guard.sh
COPY vendor/flamesphp/docker/resources/service/stealth/chromium-launch.sh /chromium-launch.sh
COPY vendor/flamesphp/docker/resources/service/stealth/pulse-setup.sh /pulse-setup.sh
COPY vendor/flamesphp/docker/resources/service/stealth/pulse-start-flames.sh /pulse-start-flames.sh
COPY vendor/flamesphp/docker/resources/service/stealth/run-as-flames.sh /run-as-flames.sh
COPY vendor/flamesphp/docker/resources/service/stealth/audio-stream.sh /audio-stream.sh
COPY vendor/flamesphp/docker/resources/service/stealth/cursor-server.py /cursor-server.py

RUN chmod +x /start.sh /tab-guard.sh /chromium-launch.sh /pulse-setup.sh /pulse-start-flames.sh /run-as-flames.sh /audio-stream.sh /cursor-server.py

EXPOSE 9222 5900 6080 8090 9230

ENTRYPOINT ["/usr/bin/tini", "--"]
CMD ["/start.sh"]
