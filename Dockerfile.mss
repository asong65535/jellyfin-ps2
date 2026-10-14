# Build image with ps2sdk rebuilt for a smaller TCP_MSS.
#
# WHY
#
# This stack loses or alters bytes in maximum-size Ethernet frames. Proven
# with a packet capture: the server sends every response complete, the console
# acknowledges all of it, and the application still receives a hole. Lowering
# the MTU at the far end makes it vanish entirely, but that needs both ends of
# a direct cable, so it is a diagnostic rather than a fix.
#
# The console invites those frames itself: its SYN advertises "mss 1460", from
# TCP_MSS in ps2sdk's lwipopts.h. Every runtime route to change it is closed,
# each one tested rather than assumed:
#
#   netif MTU at runtime   capture still showed mss 1460
#   TCP_MAXSEG             constant does not exist in this SDK
#   SO_RCVBUF              rejected on hardware
#   custom lwipopts.h      our build never compiles lwIP
#
# So the SDK is rebuilt. Everything in that header derives from TCP_MSS, so
# one number is all that changes.
#
# WHY THE WHOLE SDK
#
# Rebuilding only ee/network/tcpip and dropping libps2ip.a over the stock one
# builds and links, and the app then hangs: ARP resolves over and over and TCP
# never starts. ps2sdk's own headers matched, and so did the DHCP and DNS
# flags, so the likely culprit is the lwIP revision, which
# download_dependencies.sh pins and which has moved since the base image was
# built. Building everything from one tree removes the question.
#
# USE
#   docker build -f Dockerfile.mss -t jfps2-mss .
#   then point docker-compose.yml at image: jfps2-mss

FROM ps2dev/ps2sdk-ports:latest

# cmake and pkgconf are for gsKit, which builds with cmake and looks for a
# cross pkg-config during configure.
RUN apk add --no-cache build-base git bash curl cmake pkgconf python3 \
                      py3-pillow font-noto ttf-dejavu

RUN mkdir -p /opt/thirdparty && \
    curl -fsSL -o /opt/thirdparty/jsmn.h \
      https://raw.githubusercontent.com/zserge/jsmn/master/jsmn.h && \
    test -s /opt/thirdparty/jsmn.h

# 1260 leaves an Ethernet frame of 1314, well short of the 1514 maximum that
# the driver mishandles. It is also what the working MTU 1300 test produced,
# so it is the value with evidence behind it.
ARG NEW_MSS=1260

ENV PS2SDK=/usr/local/ps2dev/ps2sdk
ENV PS2SDKSRC=/tmp/ps2sdk

# Two changes, and the second is what makes the first survivable.
#
# lwipopts.h never defines PBUF_POOL_BUFSIZE, so lwIP computes it:
#
#     LWIP_MEM_ALIGN_SIZE(TCP_MSS + 40 + PBUF_LINK_HLEN)
#
# and ps2sdk sets MEM_ALIGNMENT to 64. At TCP_MSS 1460 that rounds to 1536,
# which holds a whole 1514-byte Ethernet frame. At 1260 it falls to 1344,
# which does not, and SMAP's receive path then fails for every frame: a
# capture showed ARP replies arriving on the wire and lwIP never acting on
# them, so the console retried ARP forever and TCP never started.
#
# Pinning it at 1536 keeps the receive geometry fixed while TCP_MSS only
# changes what we advertise, which is the whole point of the exercise.
RUN git clone https://github.com/ps2dev/ps2sdk /tmp/ps2sdk && \
    F=/tmp/ps2sdk/ee/network/tcpip/src/include/lwipopts.h && \
    sed -i "s/^#define TCP_MSS  *1460/#define TCP_MSS                 ${NEW_MSS}/" "$F" && \
    sed -i "/^#define TCP_MSS/a #define PBUF_POOL_BUFSIZE       1536" "$F" && \
    grep -n "TCP_MSS\|PBUF_POOL_BUFSIZE" "$F"

# Fetches lwIP and fatfs. Needs bash and network access.
RUN cd /tmp/ps2sdk && ./download_dependencies.sh

# Everything, installed over the stock tree so headers and libraries agree.
RUN cd /tmp/ps2sdk && \
    make -j"$(nproc)" && \
    make install && \
    rm -rf /tmp/ps2sdk

# gsKit has to be rebuilt against the new SDK.
#
# The stock one was compiled against the older kernel headers and still
# references GetRomNameWithIODriver and configGetTvScreenTypeWithIODriver,
# which the newer ps2sdk has renamed. Linking the app fails on those two
# symbols otherwise, which is the first thing that goes wrong after
# installing a newer SDK over the base image.
RUN git clone https://github.com/ps2dev/gsKit /tmp/gsKit && \
    cd /tmp/gsKit && \
    make -j"$(nproc)" && \
    make install && \
    rm -rf /tmp/gsKit

WORKDIR /src