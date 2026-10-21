# PS2SDK HDD access — handoff for a Codex CLI agent

## Task and scope

Add optional HDD file access to the existing PS2 ELF, using the project's installed PS2SDK and existing initialization conventions. First inspect the repository and implement the smallest integration needed by its file consumers. This brief covers an internal ATA HDD with the standard APA partition table and PFS filesystem, typically on an expansion-bay PS2. It does not require launching another ELF.

An external USB HDD is a different transport and usually uses the USB/BDM filesystem stack. A FAT/exFAT-formatted internal disk also needs a different stack. Do not infer the filesystem from the fact that the device is an HDD. An unmodified slim console does not gain an internal ATA interface by loading these modules.

Research basis: direct inspection of [ps2dev/ps2sdk](https://github.com/ps2dev/ps2sdk) at commit `329e2bb43287e16af16c4dde1c614e2662c24468`, retrieved October 20, 2026. These findings are source-reviewed, not tested on a console. Verify interfaces against the SDK actually used to build this project; do not silently upgrade it.

## 1. Architecture and required modules

The EE application talks over SIF RPC to the IOP. FileXio forwards requests through IOMANX to PFS, which accesses an APA partition through the HDD/ATA/DEV9 drivers.

| Layer | Installed SDK module | Purpose |
| --- | --- | --- |
| Extended IOP file manager | `iomanX.irx` | Registers filesystem/block drivers and extended operations |
| EE/IOP file RPC | `fileXio.irx` | Makes IOMANX operations available to the EE |
| Hardware interface | `ps2dev9.irx` | DEV9 interface shared by ATA HDD and Ethernet |
| ATA | `ps2atad.irx` | ATA disk access |
| APA | `ps2hdd.irx` | Exposes APA disks/partitions as `hdd0:` |
| PFS | `ps2fs.irx` | Mounts a PFS partition as `pfs0:`, etc. |

One valid load order is:

1. Initialize SIF RPC and complete the application's normal IOP reset, if it uses one.
2. Apply `sbv_patch_enable_lmb()` before loading embedded IRXs with `SifExecModuleBuffer()`. Retain other loader patches already required by the application. The SDK sample also applies `sbv_patch_disable_prefix_check()`.
3. Load `iomanX.irx`, then `fileXio.irx`.
4. Load `ps2dev9.irx`, `ps2atad.irx`, `ps2hdd.irx`, then `ps2fs.irx`.
5. Call `fileXioInit()` after the FileXio server has successfully started; check its result.
6. Check HDD status, mount the selected partition, then open files through its PFS mount.

Inspect existing initialization before adding this sequence. If network support already loads DEV9, reuse that single instance. Do not reload DEV9 or reset the IOP after network/audio/controller services have started just to enable HDD access. `poweroff.irx` and EE `-lpoweroff` are relevant when implementing the optional shutdown callback, but are not unconditional imports of the basic PFS stack at this revision.

For each module, distinguish the load call's return value/module ID from the module entry point result returned through `mod_res`. A nonnegative load return alone does not prove that a driver stayed resident. Check both against the SDK's module-result conventions and verify the expected device/service exists. On failure, disable HDD functionality and preserve the application's other sources.

**Initialization trap:** `fileXioInit()` retries RPC binding while the server is absent. Do not call it after a known FileXio module startup failure and expect a quick error.

## 2. Package the drivers inside the ELF

Embedding the IRXs avoids needing HDD access to load the very drivers required to read that HDD. It also makes the ELF independent of sidecar module paths when booted from memory card, USB, or HDD.

Adapt the existing build; this is the SDK sample's embedding pattern extended to the HDD modules:

```make
EE_LIBS += -lfileXio -lpatches

IRX_FILES += iomanX.irx fileXio.irx ps2dev9.irx ps2atad.irx ps2hdd.irx ps2fs.irx
EE_OBJS += $(IRX_FILES:.irx=_irx.o)

%_irx.c:
	$(PS2SDK)/bin/bin2c $(PS2SDK)/iop/irx/$*.irx $@ $*_irx
```

Use the project's normal PS2SDK Makefile includes and generated-file cleanup. Do not duplicate modules already embedded elsewhere. Match C symbol names exactly, including `iomanX` and `fileXio` capitalization. The SDK sample declares embedded buffers with 16-byte alignment. Include the generated objects in the link; including headers alone cannot load the IOP drivers.

`-lhdd` is optional: it provides EE convenience helpers for HDD information and filesystem management. It does not replace the IRXs and is not needed merely to mount a partition and read a file.

Use modest, explicit driver capacities when the app needs more than the defaults. Verified startup options:

| Driver | Options | Source defaults |
| --- | --- | --- |
| APA / `ps2hdd.irx` | `-o <open partition handles>`, `-n <APA cache buffers>` | 1 open handle, 3 buffers |
| PFS / `ps2fs.irx` | `-m <mount count>`, `-o <open file slots>`, `-n <metadata buffers>` | 1 mount, 2 file slots, 8 buffers |

For example, APA `-o 4 -n 8` and PFS `-m 2 -o 8 -n 32` are proposed starting capacities, not hardware-tested tuning. PFS warns when buffers are below `2 * maxOpen + 8`. A mounted PFS filesystem keeps an APA handle open, so budget APA handles for simultaneous mounts and partition enumeration too.

IRX arguments are separate NUL-terminated strings packed into one buffer, **not a space-separated command line**:

```c
static const char apa_args[] = "-o\0" "4\0" "-n\0" "8";
static const char pfs_args[] = "-m\0" "2\0" "-o\0" "8\0" "-n\0" "32";
/* Pass sizeof(apa_args) / sizeof(pfs_args), not strlen(). */
```

## 3. Mount partitions, then use file paths

| Meaning | Example |
| --- | --- |
| Disk / partition enumeration | `hdd0:` |
| APA partition passed as the mount source | `hdd0:+APPDATA` |
| PFS mount target selected by this process | `pfs0:` |
| File after mounting that partition | `pfs0:/settings.txt` |
| Nested file after mounting | `pfs0:/APPS/myapp/settings.txt` |

`+APPDATA` is an illustrative, **already existing** partition. Do not automatically create it. Existing installations may use another partition, including `__common`; configure or discover the intended one.

Mount direction is important:

```c
fileXioMount("pfs0:", "hdd0:+APPDATA", FIO_MT_RDONLY);
```

The first argument is the mountpoint; the second is the partition. A path such as `hdd0:+APPDATA/settings.txt` is not a PFS file path. `hdd0:` is not a flat directory containing the files from every partition.

Check the disk using:

```c
int status = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
```

The status is returned directly, not written into an output buffer. The standard ATA-backed APA startup code yields: `0` = accessible and APA-formatted; `1` = accessible but APA format not recognized; `2` = present but unlock failed; `3` = no usable ATA HDD detected. Negative values mean the request failed. Preserve the exact value in diagnostics; status `0` does not guarantee that a particular partition contains a mountable PFS filesystem.

For a partition selector, enumerate using `fileXioDopen("hdd0:")`, `fileXioDread()`, and `fileXioDclose()`. Select main PFS partitions: `entry.stat.mode == APA_TYPE_PFS` and no `APA_FLAG_SUB` in `entry.stat.attr`. Handle `Dread` results correctly: positive = entry, zero = end, negative = error. Do not present raw HDLoader game partitions (`APA_TYPE_HDL`) as ordinary PFS directories.

Prefer explicit partition selection over `hddGetFilesystemList()` for a minimal integration. That convenience helper omits system partitions in its current build and temporarily uses `pfs0:` while probing filesystems; it can interfere with an application that already owns that mountpoint.

## 4. Use modern libc file I/O correctly

The current headers require `NEWLIB_PORT_AWARE` for deliberate direct FileXio calls. Define it locally before including `fileXio_rpc.h` in the HDD integration unit. This acknowledges that low-level calls need care; it does not make raw FileXio file descriptors compatible with libc descriptors.

After successful `fileXioInit()`, the SDK installs FileXio as libcglue's path-operation backend. Therefore prefer ordinary `fopen`/`fread`/`fclose`, `open`/`read`/`close`, `stat`, and directory APIs for normal file contents. Continue using FileXio for mount/unmount and device controls.

Do not call the internal `_ps2sdk_fileXio_init()` manually. Do not mix a descriptor returned by `fileXioOpen()` with `read()`/`close()`, or one returned by `open()` with raw FileXio operations. Direct FileXio uses IOP `FIO_O_*` flags; libc uses `O_*`, and their numeric values differ. Raw FileXio errors are negative codes; libc generally reports `-1`/`NULL` plus `errno`.

Illustrative mount/read sequence; this is not a complete application:

```c
#include <stdio.h>
#include <hdd-ioctl.h>
#include <io_common.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

/* Preconditions: modules resident, fileXioInit() succeeded,
   hdd status is 0, and pfs0: is free/owned by this application. */
int rc = fileXioMount("pfs0:", "hdd0:+APPDATA", FIO_MT_RDONLY);
if (rc < 0) {
    /* Report mount failure; continue without HDD. */
} else {
    FILE *fp = fopen("pfs0:/settings.txt", "rb");
    if (fp != NULL) {
        /* Feed fp into the existing bounded parser/reader.
           Check fread(), ferror(), and fclose() results. */
        fclose(fp);
    } else {
        /* Report missing/unreadable file separately from mount failure. */
    }
    rc = fileXioUmount("pfs0:");
    /* Report cleanup failure; do not claim unmount succeeded on error. */
}
```

FileXio backend initialization affects other libc path operations too. Regression-test existing USB/memory-card access and any special device integrations after enabling it. Keep blocking mode for ordinary libc use; switching the shared FileXio client to `FXIO_NOWAIT` changes the completion semantics those wrappers expect.

## 5. Boot paths and IOP reset ownership

An ELF being loaded successfully from HDD does not prove that its filesystem drivers or mounts remain available after entry. The SDK's ELF loader can load the program and then reset the IOP before executing it.

Capture launch arguments before making initialization decisions. The legacy SDK loader explicitly builds a composite launch path by concatenating partition information and filesystem path, for example `hdd0:__common:pfs:/APPS/myapp/myapp.elf`. Actual launchers can differ. Do not treat that composite string as an ordinary `fopen` path or claim that every launcher passes this form.

For supported composite paths, parse into a durable partition identifier and a partition-relative file path. After loading the drivers, mount that partition at the app's chosen `pfsN:` and rewrite the path to it. For example:

`hdd0:__common:pfs:/APPS/myapp/myapp.elf` → mount `hdd0:__common` → `pfs0:/APPS/myapp/`.

A bare launch path such as `pfs0:/APPS/myapp/myapp.elf` does not identify the backing APA partition after a reset. Use an explicit configuration/launch argument or a carefully scoped discovery policy; do not guess the partition. An SDK CWD value is not a substitute for a mounted filesystem.

After **any** IOP reset, discard all old file handles, mount assumptions, and RPC state. Reload required modules, reinitialize the EE clients, and remount. Prefer one initialization owner and a small HDD context recording availability, selected partition, mountpoint, and mount ownership. If intentionally retaining the launcher IOP, inspect existing modules and mount ownership rather than blindly loading another copy or unmounting someone else's partition.

## 6. Writes, cleanup, and concurrent networking

Start with application reads and `FIO_MT_RDONLY`. Add writes only where the requested feature needs them. Do not introduce automatic disk formatting, partition creation/deletion, or raw sector writes. Note that a read-only application mount is not a forensic guarantee of zero disk writes: the inspected APA/PFS startup and mount paths include journal recovery/reset machinery.

For an intentionally writable mount (`FIO_MT_RDWR`), finish writers, check file flush/close results, call `fileXioSync("pfs0:", 0)` as appropriate, then unmount. The APA driver exposes `HDIOC_FLUSH` for the ATA cache; check results when using it in a write/shutdown path. `fflush()` alone does not flush all IOP filesystem/device caches.

Unmount only mounts owned by the application. PFS returns `-EBUSY` while file/directory handles remain open. Coordinate cleanup with worker threads; do not unmount during reads or shut down DEV9 while Ethernet is in use.

For power-button handling, inspect `poweroff.irx`, `poweroffInit()`, and `poweroffSetCallback()`. The SDK's callback guidance closes files and unmounts PFS before final shutdown. Integrate that with existing network/audio cleanup. Do not replace the normal "close file" operation with DEV9 shutdown or HDD poweroff.

For media streaming, use a bounded buffered reader and measure sustained reads alongside network/audio activity. Use 64-bit size/seek paths where needed; avoid narrowing lengths into `int` or using a directory listing's low 32-bit size alone. Do not change thread priorities or increase IOP buffers without measurements. The reviewed FileXio default transfer buffer is 16 KiB; larger buffers consume scarce IOP memory and are not required for basic access.

## 7. Execution instructions and acceptance checks

1. Read project instructions; inspect its PS2SDK revision, Makefile, embedded modules, reset sequence, DEV9/network initialization, file APIs, launch-path parser, and shutdown path.
2. Determine whether the requested HDD is standard ATA + APA/PFS. Reuse existing modules and initialization wherever possible.
3. Implement optional HDD initialization, explicit partition mounting, path translation where required, and owned-mount cleanup. Keep unrelated playback, networking, settings precedence, and file sources behaving as before unless this task requires a change.
4. Add diagnostics distinguishing module-load/start failure, disk status, partition/mount failure, file-open failure, and read failure. Keep sensitive settings out of logs.
5. Build using the actual PS2 toolchain. Verify embedded objects and symbols are linked. Report build results separately from hardware validation.
6. Validate on hardware: no HDD; valid APA/PFS disk; missing partition; missing file; read success; unmount/remount; the real launchers used from USB, memory card, and HDD. Exercise any supported IOP-reset path and existing file-source fallback behavior.
7. If writes are implemented, use a disposable test file and verify contents after close, unmount, remount, and normal restart. If streaming/network coexistence is relevant, test them together. Do not claim these checks passed without console evidence.

Deliver the patch, a concise explanation of initialization/mount ownership, the exact partition/path convention supported, build evidence, and outstanding hardware checks.

## Verified source map

All links below pin the inspected revision. Use equivalent files in the project's SDK when versions differ.

- [FileXio sample initialization and embedded modules](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/rpc/filexio/samples/main.c)
- [Sample Makefile / bin2c embedding](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/rpc/filexio/samples/Makefile.sample)
- [FileXio public EE API](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/rpc/filexio/include/fileXio_rpc.h)
- [FileXio initialization, RPC binding, and blocking state](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/rpc/filexio/src/fileXio_rpc.c)
- [FileXio libcglue backend and flag/descriptor conversion](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/rpc/filexio/src/fileXio_ps2sdk.c)
- [APA module dependencies and installed name](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/hdd/apa/Makefile)
- [ATA startup, APA options, and HDD status construction](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/hdd/apa/src/hdd.c)
- [APA file operations and devctl status/flush handling](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/hdd/apa/src/hdd_fio.c)
- [PFS startup options/default capacities](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/hdd/pfs/src/pfs.c)
- [PFS mount/unmount and sync behavior](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/hdd/pfs/src/pfs_fio.c)
- [PFS mount-time superblock/journal processing](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/hdd/libpfs/src/superWrite.c)
- [HDD/PFS command IDs and APA partition types](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/common/include/hdd-ioctl.h)
- [Mount flags and IOP file flags](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/common/include/io_common.h)
- [Optional libhdd helpers and their partition probing behavior](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/rpc/hdd/src/libhdd.c)
- [ELF loader composite argv path and reset behavior](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/elf-loader/src/loader/src/loader.c)
- [CWD and path normalization](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/libcglue/src/cwd.c)
- [Poweroff lifecycle/callback documentation](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/rpc/poweroff/include/libpwroff.h)
