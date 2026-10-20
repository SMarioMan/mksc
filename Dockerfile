# syntax=docker/dockerfile:1
FROM debian:trixie-slim@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential binutils-arm-none-eabi gcc-arm-none-eabi \
    ca-certificates git libpng-dev python3 \
    && rm -rf /var/lib/apt/lists/*

# The matching compiler revision used by the clean-ROM hash check.
ARG AGBCC_REVISION=da598c1d918402c42c0c0d7128ba14567f3175e9
RUN --mount=type=secret,id=network_ca \
    set -eu; if [ -f /run/secrets/network_ca ]; then export GIT_SSL_CAINFO=/run/secrets/network_ca; fi; \
    git init /opt/agbcc && git -C /opt/agbcc remote add origin https://github.com/pret/agbcc.git \
    && git -C /opt/agbcc fetch --depth=1 origin "$AGBCC_REVISION" \
    && git -C /opt/agbcc checkout --detach FETCH_HEAD \
    && test "$(git -C /opt/agbcc rev-parse HEAD)" = "$AGBCC_REVISION" \
    && cd /opt/agbcc && ./build.sh

ENV DEVKITARM=/usr PYTHONDONTWRITEBYTECODE=1 AGBCC_REVISION=${AGBCC_REVISION}
WORKDIR /opt/mksc
COPY . .
RUN cd /opt/agbcc && ./install.sh /opt/mksc \
    && cd /opt/mksc && make tools libraries && chmod -R a+rX /opt/mksc

# ROM/patch inputs are mounted at runtime, never copied into image layers.
ENTRYPOINT ["python3", "/opt/mksc/tools/release/build.py"]
CMD ["--base-rom", "/inputs/baserom.gba", "--music-restoration-ips", "/inputs/MusicRestore.ips", "--output", "/out"]
