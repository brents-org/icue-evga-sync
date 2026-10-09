# icue-evga-sync

Makes an EVGA RTX 30-series graphics card follow whatever lighting iCUE is running. It
was built for an RTX 3080 Ti FTW3 Ultra, and works on any EVGA Ampere board with the
"v3" RGB controller. You set effects in iCUE as usual; a small background process reads
the colors iCUE is rendering on a Corsair device and writes them to the card's RGB
controller about 20 times a second.

Native C++17, three interfaces:

- **iCUE SDK v4**, read-only. It never requests control, so iCUE and other SDK clients
  carry on as normal. `CorsairGetLedColors` returns what each Corsair LED is showing,
  including iCUE's own effects.
- **NvAPI I2C** (`NvAPI_I2CWriteEx`/`ReadEx`, resolved from the driver's `nvapi64.dll`).
  The card's RGB MCU sits at I2C address `0x2D` on GPU port 1. No admin rights or extra
  driver needed.
- **EVGA "v3" RGB protocol**, using the register map from OpenRGB's
  `EVGAGPUv3Controller`. That map came from Precision X1 captures. The card shows up as
  four zones: Front Logo, End Plate Logo, Back Logo, and the ARGB header.

## Install

Download the latest `icue-evga-sync-setup-<version>.exe` from
[Releases](https://github.com/brents-org/icue-evga-sync/releases) and run it. It is a
per-user installer for Windows 10/11 x64, so no admin rights are needed:

- installs to `%LOCALAPPDATA%\Programs\icue-evga-sync`
- starts at every logon
- appears in Settings → Apps, where you can uninstall it

Running a newer setup upgrades in place, and `--quiet` installs without dialogs. Before
you start, turn on **iCUE → Settings → General → Enable SDK**.

The setup exe is not code-signed, so SmartScreen may ask you to confirm the first time.
Each release carries a `SHA256SUMS.txt` to check it against.

Releases are cut from the maintainer's development repository and published here as one
snapshot commit plus a tag. Versions are CalVer, `vYEAR.MINOR.PATCH`: MINOR counts
feature releases within the year and PATCH counts fixes, both starting again at `.0.0`
each January. The tag's build runs on GitHub-hosted runners (see
`.github/workflows/release.yml`).

## Why it isn't a device inside iCUE

iCUE 5 does load third-party device plugins (`plugins\<vendor>\*.dll`, run by
`iCUEDevicePluginHost.exe`). But that ABI is undocumented, and the host `WinVerifyTrust`s
every plugin. A home-built plugin would only load if iCUE's signature check were
bypassed or a self-signed root were trusted machine-wide. This project uses the public
SDK instead. The cost: you pick lighting on a Corsair device and the card mirrors it,
instead of the card having its own tile in iCUE.

## Build

```cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release [-DICUE_EVGA_SYNC_VERSION=1.2.3]
cmake --build build
ctest --test-dir build --output-on-failure
```

CMake downloads the iCUE SDK 4.0.84 zip from Corsair's GitHub release at configure time
and checks its SHA-256, so the SDK is not committed here. The installer bundles
Corsair's `iCUESDK.x64_2019.dll` from that zip's `redist` folder. The build produces:

| File | What |
|---|---|
| `icue-evga-sync.exe` | the daemon, a windows-subsystem app with no console |
| `icue-evga-syncctl.exe` | the same code as a console app, used for the commands below |
| `iCUESDK.x64_2019.dll` | Corsair's SDK client, copied next to the exes and delay-loaded |
| `icue-evga-sync-setup.exe` | the installer: the three files above embedded as resources |
| `sync_core_tests.exe` | unit tests for the packet layout and the mapping logic |

## Use

```cmd
icue-evga-syncctl --probe      :: GPU ids, card firmware, zone modes and LED counts (read-only)
icue-evga-syncctl --list       :: Corsair devices iCUE exposes, LED counts, live colors
icue-evga-syncctl run          :: sync in the foreground, Ctrl+C restores the card
icue-evga-syncctl --install    :: copy to %LOCALAPPDATA%\Programs\icue-evga-sync, start at logon
icue-evga-syncctl --stop       :: ask the daemon to restore the card and exit
icue-evga-syncctl --uninstall  :: stop, delete the logon task, Apps & features entry and binaries
icue-evga-syncctl --version
```

`--install` (and the setup exe, which runs the same code) does three things:

- registers a Task Scheduler logon task called `icue-evga-sync`, which runs as you, in
  your session, at least privilege, with no 72-hour time limit, and restarts on failure
- adds an `HKCU` Apps & features entry
- starts the daemon

The config and log live in `%LOCALAPPDATA%\icue-evga-sync\`, and an uninstall leaves
them in place. Uninstalling from Settings runs the installed exe, which hands off to a
temp copy of itself, because Windows won't let a running exe delete its own file. The
copy is in turn deleted by a hidden `cmd` a couple of seconds after it exits.

## Config

`icue-evga-sync.ini` is written with comments on first run, and edits are picked up
within a second. Each `[zoneN]` section picks a Corsair device (`device=` takes part of
the model name or `type:<kind>`; leave it empty for the first case device) and LEDs on
it (`leds=auto|all|0-7,12`). `auto` splits the device's LEDs evenly across the zones
that use it, so an effect sweeping across your fans also sweeps across the card. Each
zone's color is the RMS average of its LEDs, which keeps mixed saturated colors bright
instead of fading them toward grey.

The ARGB header zone is off by default, because it reports 60 LEDs whether or not
anything is plugged in.

## Behaviour worth knowing

- **It only ever writes volatile registers.** It never writes `0x90`, which saves to
  flash. Your Precision X1 / flash-saved lighting is untouched and comes back after a
  power cycle.
- **It hands the card back.** At startup it snapshots the card's modes and static
  colors. It restores them, and reads them back to check, whenever iCUE goes away, on
  `--stop`, at logoff, and on exit. Set `restore=0` to keep the last colors instead.
- **It paces I2C traffic.** The MCU silently drops about 5% of transactions that arrive
  back-to-back, and NvAPI still reports success. Every transfer is spaced at least 2 ms
  apart, using a high-resolution waitable timer rather than a spin. All four zones are
  re-sent every 2 s anyway, so a dropped frame can't stick.
- **CPU.** Each zone write costs about 1 ms of CPU inside the NVIDIA driver, which polls
  the bus. With an animated iCUE effect, three zones at 20 fps come to about 7% of one
  core (about 0.3% of a 24-thread desktop). With static lighting it's close to zero, because changes
  of 2/255 or less aren't sent. The process runs at below-normal priority with EcoQoS.
  `fps=` is the knob.
- **Sleep and resume** cut power to the MCU, which forgets the unlock and modes. The
  daemon catches `WM_POWERBROADCAST` (or a gap of more than 10 s between ticks) and
  re-initialises.
- **Supported boards** are the 60 EVGA Ampere boards OpenRGB lists for this controller
  (`src/supported_cards.inc`, generated from OpenRGB's detector). On any other board it
  refuses before sending a single write.

## Credits and license

The EVGA v3 register map and the supported-board list come from
[OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB)'s `EVGAGPUv3Controller` and its
detector. Corsair's iCUE SDK is © Corsair. This project is MIT-licensed; see
[LICENSE](LICENSE). It isn't affiliated with Corsair, EVGA or NVIDIA.
