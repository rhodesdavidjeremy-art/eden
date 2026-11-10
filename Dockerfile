# Eden Research Reader as a website: the real app runs unchanged on a virtual screen and is shown in the
# browser through noVNC behind a password-protected gateway (web/gateway.py).
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive LANG=C.UTF-8

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build qt6-base-dev qt6-qpa-plugins libsqlite3-dev libgl-dev \
      python3 xvfb x11vnc x11-utils x11-apps openbox novnc tini curl ca-certificates \
      fontconfig fonts-dejavu-core fonts-noto-color-emoji \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# Build the GUI. The image build fails here if anything does not compile.
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DEDEN_BUILD_TESTS=OFF -DEDEN_BUILD_UI=ON \
      -DCMAKE_DISABLE_FIND_PACKAGE_Qt6Multimedia=ON -DCMAKE_CXX_FLAGS=-DEDEN_WEB_BUILD \
 && cmake --build build -j"$(nproc)" \
 && test -x build/bin/eden_reader
# The sidecar and the gateway are tested as part of the image build too.
RUN (cd python && python3 -m unittest discover -s tests -t .) \
 && python3 -m unittest tests.web.test_gateway

RUN mkdir -p /opt/eden /opt/eden-web /data/files \
 && cp build/bin/eden_reader /opt/eden/ \
 && cp -r python /opt/eden/python \
 && cp web/gateway.py web/entrypoint.sh web/openbox-rc.xml /opt/eden-web/ \
 && chmod +x /opt/eden-web/entrypoint.sh \
 && useradd --uid 10001 --home-dir /data --shell /usr/sbin/nologin eden \
 && chown -R eden:eden /data /opt/eden /opt/eden-web \
 && cd / && rm -rf /src

USER eden
ENV EDEN_WEB_PORT=8080
EXPOSE 8080
VOLUME /data
HEALTHCHECK --interval=30s --timeout=5s --start-period=40s CMD curl -fsS http://127.0.0.1:8080/healthz || exit 1
ENTRYPOINT ["/usr/bin/tini", "--", "/opt/eden-web/entrypoint.sh"]
