# OPL reference: restoring PFS access after ELF launch

## Objective for the CLI agent

Fix the target ELF's ability to access files on an existing APA/PFS HDD after launch. First establish exactly where access fails. Do not keep changing boot-path strings until driver/RPC/mount readiness is proven.

Inspected October 21, 2026:

- Open PS2 Loader: `ps2homebrew/Open-PS2-Loader`, commit `b3a2199795fe89ba7e2dd7b843354472dff2c3eb`.
- Supporting SDK implementation: `ps2dev/ps2sdk`, commit `329e2bb43287e16af16c4dde1c614e2662c24468`.

These are source findings, not console test results. OPL consumes an installed SDK and does not pin the SDK commit above in its Makefile. Check the actual SDK libraries/IRXs used by the target and by the launcher build when relevant.

## Main conclusion

OPL does not provide a persistent PFS service contract to an arbitrary ELF it launches. Its app-launch path keeps the source device available to read the ELF, then invokes the SDK ELF loader. In the inspected implementation of that loader, the IOP is reset before entering the target ELF, and only ROM memory-card modules are reloaded there. HDD/PFS/FileXio state must be rebuilt by the target.

Partition information passed in `argv[0]` identifies what to remount. It does not preserve a mount, an RPC binding, or open descriptors.

This establishes a launch/reset failure mechanism. It does not establish the exact bug in the target application without its source and runtime stage results.

## 1. The OPL call chain

### PFS used by OPL's interface and apps

1. `src/opl.c:reset()` calls `sysReset(...)`.
2. `src/system.c:sysReset()` tears down EE RPC clients, resets/synchronizes the IOP, reinitializes RPC, calls `sceCdInit(SCECdINoD)`, initializes LOADFILE and IOP heap services, applies SBV patches, loads IOMANX/FileXio and other app services, then calls `fileXioInit()`.
3. `src/hddsupport.c:hddLoadModules()` initializes DEV9, loads the ATA driver and OPL's `xhdd` helper. OPL's BDM subsystem has already been initialized by the normal reset path.
4. `hddLoadSupportModules()` checks disk signatures before APA startup, loads APA, checks HDD status, loads PFS, selects the OPL partition, and mounts it at `pfs0:`.
5. Ordinary files are opened through that mount using standard `open()`/`opendir()` and related calls.

### Launching an app ELF

`src/appsupport.c:appLaunchItem()` opens the configured ELF path to verify it, identifies its source mode, and, for HDD mode, builds a partition string such as `hdd0:+OPL:`. It then calls:

```c
deinit(UNMOUNT_EXCEPTION, mode);
LoadELFFromFileWithPartition(filename, partition, argc, argv);
```

The cleanup exception preserves the source mount long enough to load the executable. It does not promise that the mount survives the SDK loader.

The OPL Makefile links `-lelf-loader-nocolour`. In the inspected SDK this variant uses the legacy `ee/elf-loader` implementation with debug colors disabled. `LoadELFFromFileWithPartition()` selects the reset path. The embedded loader loads the ELF into EE memory, resets and synchronizes the IOP, loads `rom0:SIO2MAN`, `rom0:MCMAN`, `rom0:MCSERV`, then calls `ExecPS2()`.

### HDD game loading is a separate mechanism

`modules/iopcore/cdvdman/device-hdd.c` reads HDLoader partition specifications and translates virtual disc-sector requests into ATA transfers. The game sees an emulated CD/DVD interface. This does not supply an ordinary `pfs0:` filesystem service to a homebrew app. Use the frontend initialization and app-launch path as references for this task, not the game's CDVD emulation core.

## 2. Exact driver binaries matter

OPL's embedded C symbol names hide the actual files selected by its Makefile:

| OPL embedded symbol | Actual IRX input | Consequence |
| --- | --- | --- |
| `ps2atad_irx` | `ata_bd.irx` | BDM-enabled ATA driver; requires BDM |
| `ps2hdd_irx` | `ps2hdd-osd.irx` | OSD APA variant |
| `ps2fs_irx` | `ps2fs-osd.irx` | OSD PFS variant |
| `xhdd_irx` | OPL's `modules/hdd/xhdd/xhdd.irx` | Extra ATA controls/raw signature-read helper, not a PFS RPC server |

Do not infer binary contents from a C symbol. Record the full source filename, SDK revision, and preferably hash of each embedded IRX.

For a minimal ATA/PFS reader, either use a coherent plain SDK stack (`ps2atad.irx`, `ps2hdd.irx`, `ps2fs.irx`), or deliberately reproduce OPL's variants and dependencies. If using `ata_bd.irx`, load `bdm.irx` before it. BDM's FAT filesystem module is not itself necessary just to read PFS, though OPL loads it for other sources.

The OSD variants are not a generic cure for broken RPC. Their options differ: the inspected APA OSD variant changes password handling and exposes partition-LBA metadata; the PFS OSD variant adds inode-related behavior/metadata used by OPL. A plain read on an ordinary accessible PFS partition does not inherently require OPL's game-specific metadata features. A mount returning an access error on a protected partition does justify checking the APA variant/password behavior.

## 3. Rebuild the target's own access after the final reset

Use one initialization owner. Copy launch arguments into owned memory early, retain a durable partition identifier, and perform HDD initialization after the application's last planned startup IOP reset. Do not mount successfully and then let the normal network/audio initialization reset the IOP again.

A proposed minimal sequence, adapted from OPL rather than copied wholesale:

1. Complete the target's normal IOP reset/synchronization and invalidate old EE RPC clients when resetting an already initialized target process.
2. Initialize SIF RPC, CDVD without disc initialization (`sceCdInit(SCECdINoD)`), LOADFILE, and IOP heap services. Apply the load-module-buffer patch and any needed prefix patch. Check results where APIs provide them.
3. Load IOMANX and FileXio. Bind with `fileXioInit()` only after FileXio's startup succeeded. Keep `FXIO_WAIT` for this probe.
4. Load DEV9 once. Reuse that same DEV9 instance for later Ethernet initialization.
5. Load BDM if using `ata_bd.irx`, then the selected ATA driver. If following OPL's disk-signature check, load its compatible `xhdd` helper and perform that check before loading APA.
6. Load APA with explicit capacity arguments. Require a usable APA disk status.
7. Load PFS with explicit mount/file/cache limits.
8. Mount a known existing PFS partition at a valid, owned `pfsN:` unit.
9. Prove a root-directory read and a small raw FileXio file read. Then prove the equivalent libc read.
10. Add settings parsing and network/audio startup, retesting after each initialization stage.

OPL's `sysLoadModuleBuffer()` checks both `SifExecModuleBuffer()` outputs: the returned module ID must be nonnegative and the module entry result must be zero for these drivers. Log both values; do not collapse failures into one generic PFS message. Also log the exact arguments' byte count. An already-loaded module table belongs to the current IOP lifetime and must be cleared after reset.

`fileXioInit()` can wait indefinitely while no FileXio RPC server exists. Merely calling it is not evidence that a server was loaded. Avoid binding after a known module failure. Every major step needs a visible before/after marker so a hang identifies the blocking stage.

## 4. Mount-unit and argument traps

The inspected SDK's PFS defaults are **one mount**, two open slots, and eight metadata buffers. One mount means only `pfs0:` is valid. Selecting `pfs1:` requires at least `-m 2`; selecting `pfs3:` requires at least `-m 4`. Choosing another mount number to avoid conflicts does not allocate another mount slot.

OPL's APA argument array requests `-o 4 -n 20`. Its PFS array visually requests `-o 10 -n 40`, but starts with an empty NUL argument. Do not copy that leading empty argument blindly: the inspected SDK parser stops at an argument whose first byte is not `'-'`, so the options may remain unapplied if that empty argument reaches the parser. This is a source-level compatibility observation, not a demonstrated cause of the target's failure. Check the driver's actual startup banner/capacities.

For the target's probe, construct straightforward NUL-separated arguments without a leading empty argument:

```c
static const char apa_args[] = "-o\0" "4\0" "-n\0" "20";
static const char pfs_args[] = "-m\0" "1\0" "-o\0" "8\0" "-n\0" "24";
/* Pass sizeof(array) to SifExecModuleBuffer(), not strlen(array). */
```

These are proposed probe capacities, not measured performance tuning. Keep the first test on `pfs0:` after a controlled reset, with no inherited mounts to guess about.

## 5. Separate partition identity from the boot filename

OPL normally discovers its data partition from `hdd0:__common` → `OPL/conf_hdd.cfg` → `hdd_partition`, falling back to `+OPL`. This is an OPL configuration policy, not a universal location for every application.

The inspected SDK loader creates the target's `argv[0]` by concatenating the partition prefix and the ELF filename. For example, if OPL supplies `hdd0:+OPL:` and `pfs0:APPS/myapp/myapp.elf`, the target receives a composite path such as:

```text
hdd0:+OPL:pfs0:APPS/myapp/myapp.elf
```

Other launchers may supply `pfs:/...`, `pfsN:/...`, a different composite convention, or incomplete information. Log what the actual launcher supplies instead of assuming one representation.

For the example above, resolve independently:

| Field | Resolved value |
| --- | --- |
| Partition to mount | `hdd0:+OPL` |
| Target-owned mount | `pfs0:` |
| ELF-relative directory | `/APPS/myapp/` |
| Settings file to open after mounting | `pfs0:/APPS/myapp/settings.txt` |

Never pass the entire composite launch string to `fopen()` as though it were a PFS file path. Do not include the delimiter colon from `hdd0:+OPL:` in the partition identity passed to mount. A bare `pfs0:/...` path cannot by itself recover the backing partition after reset; use an explicit configured partition or a scoped discovery policy.

## 6. Diagnostic checkpoints: locate the first failing layer

Use on-screen or other already-working logging. Network-only logs are insufficient if network startup resets the IOP or depends on the broken initialization sequence.

| Checkpoint | Evidence to record | If it fails |
| --- | --- | --- |
| ELF entry | `argc`, copied launch path, chosen partition | Investigate argument parsing before fallback search |
| Last reset | Before/after reset and sync; reset count | Fix reset lifecycle before filesystem work |
| Each IRX | Input filename, module ID, entry result | Investigate imports, variant, arguments, IOP memory |
| FileXio bind | Before/after `fileXioInit()` | Missing/nonfunctional FileXio server or RPC lifecycle |
| Driver registry | `fileXioGetDeviceList()` contains `hdd` and `pfs` | Registered drivers are missing; this does not test a mount |
| APA status | `fileXioDevctl("hdd0:", HDIOC_STATUS, ...)` result | HDD/APA readiness; zero is the usable formatted state |
| APA directory | `fileXioDopen("hdd0:")`, `Dread()` names/types | Partition service/selection issue |
| PFS mount | `fileXioMount("pfs0:", partition, FIO_MT_RDONLY)` result | Check unit capacity, partition name/type, access, filesystem |
| PFS directory | `fileXioDopen("pfs0:/")`, one `Dread()` | Tests mounted PFS separately from settings filenames |
| Raw file read | `fileXioOpen(path, FIO_O_RDONLY)` + `fileXioRead()` | File path/content access rather than libc |
| Libc read | Separate `fopen(path, "rb")` + `fread()`, `errno` | If raw read works, inspect libc/backend/path normalization |
| Later app init | Repeat root/file read after network/audio setup | Detect resets, DEV9 shutdown, or changed global I/O state |

Close every directory/file with its matching API. A descriptor from `fileXioOpen()` must use `fileXioRead()`/`fileXioClose()`; a libc descriptor must use libc. `FIO_O_*` and `O_*` differ. Do not switch the shared client to asynchronous `FXIO_NOWAIT` while libc callers expect blocking results.

For an independent RPC/module check, SDK `smod_get_mod_by_name()` uses IRX internal names, not filenames. At the inspected revision, useful names are `IOX/File_Manager_Rpc`, `dev9`, `atad`, `hdd_driver`, and `pfs_driver`. Inspect the matching SDK before hardcoding names. A module's presence alone still does not prove its devices or RPC requests work.

## 7. Requested implementation strategy

Build a tiny diagnostic mode in the target, or a minimal separate probe ELF using the same SDK/embedded modules. First boot it from USB and read a known small file from one explicitly selected, existing HDD partition. Then launch the same probe from HDD through the actual launcher. Finally integrate the proven sequence into the full app and repeat reads after network/audio startup.

Do not add format/create-partition behavior from OPL to the target. OPL's existing code checks disk signatures before APA loading because APA startup may write to a non-APA disk; preserve equivalent format awareness where disk format is not already established. Read-only application use is not a forensic zero-write guarantee for driver journal handling.

Deliver the patch plus the first failing/successful checkpoint, exact IRX inputs, SDK version, actual launch argument convention, and console evidence. Do not report a successful build as proof of PFS communication on hardware.

## Source references

- [OPL reset, module loading, and FileXio initialization](https://github.com/ps2homebrew/Open-PS2-Loader/blob/b3a2199795fe89ba7e2dd7b843354472dff2c3eb/src/system.c#L103-L288)
- [OPL HDD interface/module loading, disk signature checks, and PFS mount](https://github.com/ps2homebrew/Open-PS2-Loader/blob/b3a2199795fe89ba7e2dd7b843354472dff2c3eb/src/hddsupport.c#L178-L345)
- [OPL actual embedded driver filenames](https://github.com/ps2homebrew/Open-PS2-Loader/blob/b3a2199795fe89ba7e2dd7b843354472dff2c3eb/Makefile#L622-L641)
- [OPL BDM initialization](https://github.com/ps2homebrew/Open-PS2-Loader/blob/b3a2199795fe89ba7e2dd7b843354472dff2c3eb/src/bdmsupport.c#L109-L137)
- [OPL app ELF launch](https://github.com/ps2homebrew/Open-PS2-Loader/blob/b3a2199795fe89ba7e2dd7b843354472dff2c3eb/src/appsupport.c#L358-L417)
- [OPL cleanup selection and deinitialization](https://github.com/ps2homebrew/Open-PS2-Loader/blob/b3a2199795fe89ba7e2dd7b843354472dff2c3eb/src/opl.c#L1631-L1678)
- [OPL raw-sector HDD game backend](https://github.com/ps2homebrew/Open-PS2-Loader/blob/b3a2199795fe89ba7e2dd7b843354472dff2c3eb/modules/iopcore/cdvdman/device-hdd.c)
- [SDK loader variant used by OPL](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/elf-loader-nocolour/Makefile)
- [SDK loader's reset selection](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/elf-loader/src/elf.c)
- [SDK composite argv construction and IOP reset before ExecPS2](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/elf-loader/src/loader/src/loader.c#L99-L155)
- [SDK ATA BDM build option](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/dev9/ata_bd/Makefile)
- [SDK ATA imports, including conditional BDM dependency](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/dev9/atad/src/imports.lst)
- [SDK PFS options/parser and default capacities](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/hdd/pfs/src/pfs.c)
- [SDK PFS mount unit validation](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/hdd/pfs/src/pfs_fio.c#L109-L142)
- [SDK APA variant differences](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/hdd/apa/src/apa-opt.h)
- [SDK PFS variant differences](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/iop/hdd/pfs/src/pfs-opt.h)
- [SDK FileXio RPC binding and libc backend setup](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/rpc/filexio/src/fileXio_rpc.c)
