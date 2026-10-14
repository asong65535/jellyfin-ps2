# Jellyfin PS2

A native Jellyfin client for the PlayStation 2. Browse your Jellyfin library,
watch films and series, and listen to music on real PS2 hardware, launched
from a USB stick.

Written in C with ps2sdk and gsKit. Video is decoded by the PS2's own MPEG-2
hardware (the IPU); Jellyfin transcodes everything into a form the console can
play.

## Features

- **Users:** "Who is watching?" with the server's users. Sign in once with
  your password; the app remembers each signed-in user.
- **HOME:** Continue Watching (with progress) and Next Up.
- **Library:** Movies, Series (seasons and episodes), Music (artists and
  albums), and Search, with posters and descriptions.
- **Video:** 640x480 MPEG-2 playback with sound in sync, pause, -10 s / +20 s,
  a time bar, Resume from where you stopped (saved in Jellyfin), audio track
  choice, and a soft ending.
- **Subtitles:** text tracks (SRT, ASS, WebVTT) are drawn by the PS2 itself,
  as quick to start and seek as no subtitles; picture-based tracks (PGS,
  VobSub) are burned in by the server. French and other Latin-1 accents are
  supported.
- **Series:** auto-play of the next episode, with a 5-second countdown once
  an episode has really ended.
- **Music:** album playback with play, pause, stop and track skipping.
- **Settings in one file:** `settings.txt` beside the ELF holds the server,
  the network setup and the signed-in users. It is optional.

## Requirements

- A PS2 with network access: a slim (Ethernet built in), a fat console with
  the official Network Adaptor, or a PSX (DESR).
- A **FAT32 USB stick**. Small and old works best: the PS2's USB 1.1 port is
  fussy, and many USB 3 sticks or sticks over 32 GB are not recognised. Use
  the front port.
- A way to launch an ELF: uLaunchELF/wLaunchELF, FreeMcBoot, PS2BBL or OPL.
- A **Jellyfin server** on the same network, reachable over plain HTTP (the
  PS2 has no TLS), with its usual transcoding (ffmpeg) available.

## Installation

1. Download `jellyfin-ps2.elf` from the
   [Releases](https://github.com/jimmyBizMobile/jellyfin-ps2/releases) page.
2. Copy it to a folder on the USB stick, for example `mass:/APPS/JELLYFIN/`.
3. Optionally put a `settings.txt` next to it (see the end of this page).
   Without it, the PS2 asks your router for an address and you type the
   server address once.
4. Launch the ELF, enter your server address (for example
   `http://192.168.0.20:8096`), press START, then pick your user.

Give the console a couple of seconds after signing in before switching it
off: that is when the sign-in is being saved to the stick.

## If something goes wrong

- **"Playback stopped unexpectedly (D)"** (or another letter) on a film's
  page: a part of the app stopped responding during video playback. The
  app set it aside and stopped the film instead of freezing; play it again
  to continue from where it stopped. If it keeps happening, please open an
  issue with the letter: D video decoder, S video sound, N video network
  reader, R background reports.
- **The screen turns solid yellow or white during video playback** and
  nothing responds: the screen part itself is stuck (yellow) or waiting for
  something that never comes (white). Switch the console off and on, and
  please report the colour and what you were doing.

## Building from source

Builds run in Docker, with a published image that contains the exact
toolchain the app needs:

```sh
git clone https://github.com/jimmyBizMobile/jellyfin-ps2
cd jellyfin-ps2
docker compose run --rm build
```

The result is `jellyfin-ps2.elf` in the project folder. The version shown on
the splash screen comes from `VERSION` in the `Makefile`; to set it for one
build:

```sh
docker compose run --rm build -c "make VERSION=1.0.1"
```

### What is in the build image, and why

The image is `ghcr.io/jimmybizmobile/jfps2-sdk:1.0`, pinned by digest in
`docker-compose.yml`. It is ps2dev's `ps2sdk-ports` toolchain with two fixes
that stock ps2sdk does not have. Without them the app builds, but does not
work properly on hardware.

**1. The network stack and full-size Ethernet frames**

ps2sdk's lwIP-based network stack lost or altered bytes in maximum-size
Ethernet frames: a packet capture showed the server sending every response
complete, and the application still receiving holes. The console invites
those frames itself, because its TCP handshake advertises an MSS of 1460.
None of the runtime routes to change that work on this SDK (the netif MTU,
`TCP_MAXSEG`, `SO_RCVBUF`, a custom `lwipopts.h`), so the SDK is rebuilt:

- `TCP_MSS` 1460 -> **1260** in `ee/network/tcpip/src/include/lwipopts.h`,
  so no frame comes near the size the driver mishandles.
- `PBUF_POOL_BUFSIZE` pinned at **1536**. lwIP otherwise derives it from
  `TCP_MSS`, and at 1260 it falls to 1344 bytes, too small for a 1514-byte
  frame: nothing is received at all.
- The **whole** ps2sdk is rebuilt and installed, then **gsKit** rebuilt
  against it. Replacing only the network library leaves mismatched lwIP
  revisions and the app hangs.

`Dockerfile.mss` performs these steps and documents each one.

**2. libmpeg's colour-conversion handler**

ps2sdk's libmpeg converts each decoded picture in runs of 1023 macroblocks.
When a picture needs a second run, its DMA handler (`_mpeg_dmac_handler` in
`libmpeg_core.c`) programmed the two IPU DMA channels with each other's
transfer lengths, so any picture over 1023 macroblocks never finished:
playback hung above about 640x400. The fixed library is
`third_party/libmpeg-fixed.a`, and `Dockerfile.release` copies it over the
stock one.

### Rebuilding the image yourself

```sh
docker build -f Dockerfile.mss -t jfps2-mss .
docker build -f Dockerfile.release -t ghcr.io/jimmybizmobile/jfps2-sdk:1.0 .
```

`Dockerfile.mss` clones the current ps2sdk and gsKit, so a rebuild today may
differ from the published image. The published image is the supported way to
build.

### Developer switches (`src/config.h`)

- `DEBUG_UI` (default 0): set to 1 to get the on-screen video readout
  (Square during playback), technical details on error screens, and
  `jfdebug.txt` / `jfstuck.txt` written beside the ELF when something fails.
  Release builds must keep it at 0.
- `DEBUG_BOOT_LOG`, `DEBUG_NET_LOG`, `DEBUG_POSTER_INFO`: more detailed
  logs for start-up, networking and artwork.

`settings.txt` also accepts two undocumented tuning keys for how a film ends,
`end_early_ms` (default 200) and `end_margin_kb` (default 32). libmpeg can
hang on the very last bytes of a stream, so playback stops just before them.

## Credits

- [ps2sdk](https://github.com/ps2dev/ps2sdk) and
  [gsKit](https://github.com/ps2dev/gsKit) by the ps2dev community.
- [libmad](https://www.underbit.com/products/mad/) for MP3 and MP2 audio.
- [jsmn](https://github.com/zserge/jsmn) for JSON parsing.
- The [Jellyfin](https://jellyfin.org) project.
- Other Jellyfin clients for limited hardware, whose code informed this one,
  notably ORBIT (PS2), Pocketfin (PSP), and clients for PS3 and Wii U.

---

## Using Jellyfin PS2: settings.txt

The full guide, also shipped as `tuto.txt`:

```
JELLYFIN PS2 - HOW TO SET UP settings.txt
=========================================

settings.txt tells Jellyfin PS2 where your Jellyfin server is and how the
PS2 joins your network. You write it on a computer, then copy it to the
USB stick.


1. WHERE TO PUT IT
------------------

Put settings.txt in the same folder as the Jellyfin PS2 ELF. For
example:

    mass:/APPS/JFPS2/jfps2.elf
    mass:/APPS/JFPS2/settings.txt

The file is optional. Without it, the PS2 asks your router for an
address, and you type the server address once on the connect screen.
The app then creates settings.txt for you and remembers the server, and
later the users who sign in (see section 7).


2. HOW THE FILE IS WRITTEN
--------------------------

One setting per line, written as  name=value  for example:

    server=192.168.0.20:8096

- Lines starting with # are comments and are ignored.
- Blank lines are ignored.
- Spaces around = are fine, and upper or lower case does not matter.
- A setting you leave out uses its default.
- Put comments on their own line, not at the end of a setting.


3. THE SETTINGS
---------------

server
    Your Jellyfin server's address and port, e.g. 192.168.0.20:8096
    Use the IP address of the computer running Jellyfin, not a name.
    http:// is optional. HTTPS is not supported.
    8096 is Jellyfin's usual port.
    Default: none, the connect screen asks for it.

network
    How the PS2 gets its own address:
      dhcp    Ask the router. The usual choice on a home network.
      static  Use the ip, mask and gateway lines below. Needed when the
              PS2 is cabled straight to a computer, with no router.
      auto    Ask the router for 5 seconds, then use the static lines if
              no router answers. Handy if you move the PS2 between a
              router and a direct cable. Without an ip line, auto works
              exactly like dhcp.
    Default: auto

ip
    The PS2's own address, for static (or auto's fallback).
    Pick a free address on the same network as the server: if the
    server is 192.168.50.1, use 192.168.50.something, not taken by
    another device.

mask
    The network mask. Default: 255.255.255.0 (right for almost every
    home network).

gateway
    Usually your router's address. Default: the server's address, which
    is right for a direct cable.

video
    480i or 480p. Default: 480i.
    480p is not used yet. When it is, the app will always start in
    480i and ask you to confirm 480p, so you can never get stuck with a
    black screen.

4. EXAMPLES
-----------

A) PS2 and server on a home router (most people):

    server=192.168.0.20:8096
    network=dhcp

B) PS2 cabled straight to the computer running Jellyfin:

    server=192.168.50.1:8096
    network=static
    ip=192.168.50.90
    mask=255.255.255.0
    gateway=192.168.50.1

   The computer needs a fixed address on that cable too, e.g. on a Mac:
     sudo networksetup -setmanual "<your Ethernet adapter>" 192.168.50.1 255.255.255.0

C) Sometimes on a router, sometimes on a direct cable:

    server=192.168.50.1:8096
    network=auto
    ip=192.168.50.90

   Note: the server's address usually differs between the two setups,
   so you may still need to change the server line when you move.

Tip: on a router, ask it to always give the server computer the same
address (a "DHCP reservation" in the router's settings), so the server
line never goes out of date.


5. CHECKING IT WORKED
---------------------

The connect screen shows, at the bottom right, the PS2's address and
how it got it:

    PS2  192.168.0.34  (from router)
    PS2  192.168.50.90  (static)
    PS2  192.168.50.90  (static, no router)    <- auto fell back

Just above the button hints, a red line appears if a line in
settings.txt could not be used, with its line number, for example:

    settings.txt: not an address (line 7)
    settings.txt: video= must be 480i or 480p (line 9)
    settings.txt: network=static needs a valid ip= line

Only the first problem is shown. Fix it, restart, and check again.

"No settings.txt beside the ELF: defaults" means the file was not found
next to the ELF: check the folder and the file name.


6. COMMON ERRORS WHEN CONNECTING
--------------------------------

No answer from the router
    No router gave the PS2 an address. Check the cable and the router,
    or on a direct cable use network=static with an ip line.

No address set
    network=static but no valid ip line. Add one.

No network link
    The Ethernet cable is not connected, or a fat PS2's network adapter
    is not seated.

HTTPS not supported
    Use http:// with the server's local IP address.

Could not write ...
    The app could not save the server address (write-protected stick?).
    Playback still works, but the address is not remembered.


7. USERS AND SIGNING IN
-----------------------

After connecting, the app shows "Who is watching?": the users from your
Jellyfin server's login page, plus "Other user" for accounts hidden from
it. Nothing can be browsed or played until someone signs in.

- A user with a password is asked for it once, on the on-screen keyboard.
- A user without a password signs in directly.
- Once signed in, the user shows "Signed in" and starts at once next time.
- Triangle on a signed-in user signs them out, here and on the server.
- After signing in, the app opens on HOME (Continue Watching and Next
  Up). Circle on HOME, or at the top of the library, returns to "Who is
  watching?".

The app saves each sign-in in settings.txt, one line per user:

    user=Name|user id|access token|device id

You never need to write these lines; the app adds and removes them.

Each time the app saves settings.txt, it first keeps the previous version
as settings.bak in the same folder. If settings.txt is ever found empty
(the console switched off in the middle of a save), the app restores it
from settings.bak at the next start and says so on the connect screen.
Avoid switching off right after signing in: give it a couple of seconds. The
password itself is never saved, only the access token Jellyfin gives back.

About the token: it lets anyone with the USB stick use that account, the
same as a signed-in phone would. If the stick is lost, end the sign-in in
Jellyfin's dashboard (Devices), or remove the line from settings.txt.

Each user signed in on this PS2 appears as its own device in Jellyfin's
dashboard, so signing one user in never signs another out.
```

## Example settings.txt

Also in the repository as `settings.txt.example`:

```
# Jellyfin PS2 settings - copy to settings.txt beside the ELF and edit.
# Lines starting with # are comments. Missing lines use their defaults.
# See tuto.txt for every setting.

# Jellyfin server: address and port (http:// is optional)
server=192.168.0.20:8096

# PS2 network: auto, dhcp or static
network=dhcp

# For a direct cable (no router) use instead:
#   network=static
#   ip=192.168.50.90
#   mask=255.255.255.0
#   gateway=192.168.50.1

# Video output: 480i or 480p (480p is not used yet)
video=480i

# Signed-in users are added below by the app (user=...).
```
