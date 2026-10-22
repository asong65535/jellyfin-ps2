# Episode pagination regression checks

Run with the repository's pinned PS2SDK image (it includes a host C compiler
and the same `jsmn.h` used by the PS2 build):

```sh
docker compose run --rm build -c 'sh tests/run-host.sh'
docker compose run --rm build
```

Alternatively, run `JSMN_INCLUDE=/path/to/jsmn sh tests/run-host.sh` with a
host compiler. `CC` may include sanitizer flags. The runner builds temporary
executables, replaces only two unrelated MIPS COP0 reads in a temporary copy
of `jf.c`, and discards unused PS2 thread functions at link time. Production
loaders, JSON parsing, text conversion, and track-screen callbacks run unchanged.
HTTP, PS2 sleep, and UI drawing are mocked; these are not socket or hardware tests.

## Behavior covered

- 0, 1, 6, 7, 32, 33, 100, 256, and 300 source episodes, both with and
  without totals; exact page offsets, request counts, order, IDs, titles,
  runtime, overview, and a graceful 256-entry cap.
- Malformed/incomplete JSON, token exhaustion, a full HTTP response buffer,
  transport failure, truncated response status, HTTP error, missing/invalid
  `Items`, duplicate/overlapping pages, incorrect offsets, excess records,
  premature empty pages, changing/invalid/overflowing totals.
- Existing JSON and transport retries recovering successfully; failures on
  later pages clear the partial list and return an explicit error.
- Missing, empty, invalid, or oversized IDs/titles advance the source offset
  without adding unusable entries. An entirely unusable nonempty page fails
  explicitly, bounding the number of requests. Missing or overflowing runtime
  values default safely to zero.
- Music album loading at 33 and 300 source tracks, including container and
  artist preservation.
- D-pad navigation through all 256 entries in both directions, opening each
  correct detail ID, returning with the selection visible, at most eight
  rendered rows, three-digit counts, empty seasons, and music selection at 255.

Before the fix, the loader failed the harness's assertion that episode requests
carry `Limit` and `StartIndex`.

## Verified on 2026-10-21

Both host suites and the supported Docker PS2SDK build passed. Both suites
also passed with AddressSanitizer and UndefinedBehaviorSanitizer on the host,
using `jsmn.h` copied from the pinned image. The image's own sanitizer build
could not link because its libasan has an undefined
`__sanitizer::struct_sock_fprog_sz` symbol; the normal image tests/build work.

The baseline full build warned about SDK DMA macro redefinitions and libmpeg's
ABI/float-mode mismatch. The final incremental link retained the same libmpeg
warnings; no new warning appeared in the changed source files.

Memory checks from the final PS2 ELF:

```text
sizeof(JfTrack) = 168
jf_tracks: address 0x002f8010, size 0x0000a800 = 43,008 bytes
array growth: 37,632 bytes
text       data      bss       total
1,384,252  181,432   3,605,220  5,170,904 bytes
_end = 0x005ee864
_stack_size = 0x00020000 (128 KiB)
```

The linker ends static allocation below 6 MiB. Against the PS2's 32 MiB EE
memory, approximately 25.94 MiB of address space remains above `_end` after
allowing for the main stack, before other runtime/heap reservations. This is
not a measurement of free memory during playback. No per-episode response
buffers were added.

The fix was implemented on `fix/ui-network-priority` and continued on
`fix/minor-bug-fixes` for follow-up work. The priority block in
`config.h`, `main.c`, `video.c`, playback buffers, HTTP transport, and HTTP/JSON
limits retain their existing values/behavior. Music uses the same larger
static array; its fetch and playback logic is unchanged apart from safe
runtime conversion shared with episodes.

## Remaining hardware checks and limits

The user reports that their largest season, approximately 50 episodes, loaded
on PS2 hardware without issues. Larger seasons and the remaining hardware
checks are unverified: open episodes 33/100/256 where available, return from
detail, and play both an episode and a music album while checking UI
responsiveness and free memory. Capture the requests/responses to verify
ordering and complete pages.

Six records bound the request's item count, not its byte size. Exceptionally
large metadata still produces a controlled buffer/token error; reduce
`JF_FETCH_EPISODES` if needed. The unchanged retry path and pinned SDK's MSS
fix remain necessary for historical TCP segment loss. Mock tests cannot prove
that the new requests arrive intact on a particular network.

The loader rejects inconsistent totals/offsets, repeated IDs, premature endings,
and nonempty pages with no usable records rather than showing partial success.
Library changes during pagination may therefore require reloading the season;
the endpoint does not provide a snapshot. Lists beyond 256 are intentionally
capped. The optional season overview remains best-effort, as before.
