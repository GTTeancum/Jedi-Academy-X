# XEMU Debugging Process Used Here

## Short Answer

This Jedi Academy XEMU work did not use UDP debugging.

The working process used:

- XEMU/QEMU HMP monitor over TCP (`-monitor tcp:127.0.0.1:<port>,server,nowait`)
- QMP over TCP for optional screenshots (`-qmp tcp:127.0.0.1:<port>,server,nowait`)
- In-game debug breadcrumbs stored in volatile globals and a RAM log mirror
- File logging to the Xbox HDD when the guest filesystem was healthy

Important: `scripts/ja_xemu_smoke.py` disables XEMU networking in the edited config unless `--keep-net` is passed. Any guest-side UDP logger will fail under this wrapper unless that behavior is changed or `--keep-net` is used.

## Why Not UDP

UDP would depend on the guest network stack, XEMU network configuration, host firewall/routing, and the title reaching enough runtime initialization to send packets. Most of the failures we were chasing happened before that was trustworthy.

The monitor/memory polling path works earlier and during more failure modes:

- No guest network required
- No socket setup required in the title
- Can read boot-phase globals even when the game is hung
- Can dump RAM mirrors after filesystem logging fails

## Launch Pattern

The helper ultimately launches XEMU with a mounted ISO plus monitor and optional QMP ports.

Example shape:

```powershell
python scripts\ja_xemu_smoke.py `
  --iso C:\Programming\GitHub\Jedi-Academy-X\build\xemu\JediAcademyX_SP_normal.iso `
  --hdd C:\Games\Emulators\Xemu\HDD\xbox_hdd.qcow2 `
  --xemu-exe C:\Games\Emulators\Xemu\xemu_ja.exe `
  --name ja_sp_normal `
  --port 4482 `
  --duration 30 `
  --interval 5 `
  --poll-xblog `
  --sample-registers
```

The PowerShell wrapper used for most SP tests is:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_sp_xemu_smoke.ps1 `
  -NormalBoot `
  -Repack `
  -Duration 35 `
  -Interval 5 `
  -MonitorScreenshots `
  -PollXBlogLite `
  -SkipFinalDumps `
  -Name sp_normal_check
```

Use unique monitor/QMP ports per project or isolated XEMU copy. Stale XEMU instances can hold the HDD image or collide with monitor ports.

## Guest-Side Breadcrumbs

The title writes normal breadcrumbs through `XBLog_Write` / `XBLog_Writef`.

Outputs:

- `OutputDebugStringA`
- `E:\ja_sp_log.txt` on the emulated Xbox HDD, with fallbacks in `xb_log.cpp`
- Volatile RAM mirror/counters used by the XEMU smoke helper

The useful pattern for another project is:

```cpp
extern "C" volatile unsigned int g_XboxBootPhase;
extern "C" volatile unsigned int g_XboxLogWriteCount;
extern "C" volatile unsigned int g_XboxHeartbeatCount;
extern "C" char g_XboxLogMirror[32768];
```

Then update these around suspect calls:

```cpp
g_XboxBootPhase = 0x1200;
LogWrite("before renderer init");

RendererInit();

g_XboxBootPhase = 0x1201;
LogWrite("after renderer init");
```

Build with a map file. The host helper can resolve symbol VAs from the map, convert to physical addresses using the observed XEMU delta, and read them through the HMP monitor.

## Host-Side Polling

`scripts/ja_xemu_smoke.py` does the host-side work:

- Connects to the HMP monitor TCP port
- Resolves symbols from `code\x_exe\Release\default.map`
- Reads memory with monitor commands such as `xp`
- Polls `_g_SPXBBootPhase`, frame heartbeat, FPS counters, and related telemetry
- Dumps physical memory regions when requested

Reports are written under:

```text
scripts\output\*.report.txt
scripts\output\*.xemu.txt
scripts\output\*_final_registers.txt
```

## Screenshot Path

Screenshots were not UDP either.

The helper tries:

- QMP `screendump`
- HMP `screendump`
- Guest framebuffer dump, when framebuffer telemetry is available

This avoids desktop/window screenshots and keeps captures tied to the emulator.

## If You Actually Need UDP

This repo does not prove a UDP debugging setup. If another project needs UDP, check these first:

- Do not let the helper rewrite XEMU networking off; pass `--keep-net` or remove that config rewrite.
- Verify XEMU's selected network backend and host reachability before debugging the title.
- Run a host UDP listener on the expected port before booting.
- Do not use `127.0.0.1` as the destination from the Xbox guest; that refers to the guest, not the host.
- Log before and after socket creation, bind/connect, and send calls to a non-network channel too, or UDP failures will be opaque.

For early boot, crashes, hangs, and renderer bring-up, the monitor plus RAM mirror method was the reliable path here.
