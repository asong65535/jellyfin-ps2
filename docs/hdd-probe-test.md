# HDD filesystem checkpoint test

The standalone probe isolates HDD access from Jellyfin settings parsing,
launch-path translation, USB drivers, controllers, audio and networking.
The target is explicit and identical for USB and HDD launches:

```text
APA partition: hdd0:__common
PFS file:      /APPS/JELLYFIN/settings.txt
Mounted file: pfs0:/APPS/JELLYFIN/settings.txt
```

The console tester confirmed this partition and file. No fallback search,
backup recovery, settings parsing or file creation runs in the probe.

## Build and run

1. Ensure the existing APA/PFS HDD has a nonempty, readable file at the
   path above. Back up settings before testing. File contents are never
   displayed; the probe compares up to 64 bytes read through each API.
2. Build on `feature/hdd-settings` using the pinned Docker image:

   ```sh
   docker compose run --rm build -c 'make -B hdd-probe'
   ```

   Outputs: `jellyfin-ps2-hdd-probe.elf` and
   `build/hdd-probe-irx-manifest.txt`. `-B` rebuilds embedded objects so the
   recorded SDK-input hashes correspond to this build's inputs.
3. Copy the ELF to USB and launch it with the actual launcher. The HDD
   remains installed. Photograph the final screen, or the screen where it
   hangs. Do not infer success from seeing a checkpoint's `WAIT` marker.
4. Power off, then launch the **same ELF** from HDD using
   wLaunchELF-r3z 4.8.0. Photograph the screen again. No target-file or
   partition edits are needed between these runs.
5. Report both results, the first `FAIL` row or last `WAIT` row, the full
   module-load results, the displayed launch argument, console/adapter
   model and launcher/version. Include the IRX manifest if rebuilding
   with another environment.

The probe parks on its results; power off to leave it. It closes files and
directories, unmounts only its own mount, and releases FileXio before parking.
Cleanup errors remain visible and prevent a successful result. This performs
application reads only. APA startup/PFS journal recovery may still write disk
metadata; this is not a forensic zero-write tool. It never formats or creates
partitions.

## Checkpoints

Every operation receives a visible `WAIT` marker before it runs and an `OK`
or `FAIL` result afterward. Rows retain their results; later startup cannot
erase an earlier return code. The probe stops on the first failure and marks
downstream checkpoints `NOT RUN`, then attempts cleanup.

| Checkpoint | Successful evidence | Failure narrows the problem to |
| --- | --- | --- |
| IOP reset, CDVD, services, patches | One reset/sync; no-disc CDVD init; LOADFILE/heap; patch results | Startup/RPC prerequisites |
| Each embedded IRX | Exact filename, module ID, entry result, input size and argument-byte count | Driver loading/startup, dependencies or IOP memory |
| FileXio binding | `fileXioInit()` returns; client stays in `FXIO_WAIT` | FileXio server or RPC lifecycle |
| Driver registry | Both `hdd` and `pfs` appear | Filesystem driver registration |
| HDD status | `HDIOC_STATUS` returns `0` directly | ATA/APA disk readiness |
| Partition enumeration | Full APA iteration completes; `__common` is found with mode `0x100`, without the subpartition flag | APA directory service or partition identity/type |
| PFS mount | `pfs0:` mounts `hdd0:__common` read-only | PFS mount, access or filesystem validity |
| Root directory read | `pfs0:/` opens, a `Dread` succeeds or reaches end, and directory closes | Mounted PFS directory operations |
| Raw file read | FileXio opens the explicit file, reads 1–64 bytes and closes | File access through the raw FileXio path |
| Libc read | A separate `fopen`/`fread`/`fclose` reads the same prefix without errors | Libc backend or path normalization if raw reading passed |

Raw FileXio descriptors use only FileXio operations and `FIO_O_RDONLY`.
Libc reads use a separate `FILE *` opened with `"rb"`. Return codes and
`errno` are shown where relevant, but file bytes and tokens are not logged.

## Exact inputs and lifecycle

The build embeds the installed SDK's plain binaries:

```text
iomanX.irx
fileXio.irx
ps2dev9.irx
ps2atad.irx
ps2hdd.irx
ps2fs.irx
```

These are the same plain variants used by the application. No `ata_bd`, BDM,
OSD variant or OPL `xhdd` is substituted. The published image is pinned in
`docker-compose.yml`; its installed SDK tree contains no source Git revision.
The research commits in the handoffs are reference revisions, not a claim
that the installed SDK has that revision. The manifest records SHA-256 hashes
and full input paths for the actual binaries used here.

APA receives `-o 4 -n 20`; PFS receives `-m 1 -o 8 -n 24`. Arguments are
NUL-separated, have no leading empty argument, and include their full array
size. These are explicit probe capacities from the OPL handoff, not measured
performance tuning. Only `pfs0:` is used.

The probe resets the IOP exactly once before loading drivers and never resets
it between binding, mounting and reading. It binds FileXio only after its
server module started successfully. DEV9 loads once. No app services start
after the checks, so the probe does not establish networking/audio coexistence.

## Evidence limits and next step

Host tests exercise checkpoint ordering, failure isolation, descriptor cleanup,
short raw reads, raw/libc comparison and hidden file contents. A successful
host test or cross-build does not prove PS2 hardware communication.

Current console evidence establishes that an earlier application build reported
`hdd(load=-400,start=0)`. It does not establish which later filesystem layers
work. No successful probe checkpoint has been observed on console yet.

Once both probe launches pass, compare the proven initialization sequence
with the application and retest at its initialization boundaries. If any
integration retains an HDD mount through later initialization, repeat root
and file reads afterward. Do not change settings-path precedence to compensate
for an unproven driver/RPC/mount layer.

Source context: [OPL/PFS handoff](opl-pfs-after-elf-launch-handoff.md).
