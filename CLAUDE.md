# Theft4 Linux arm64 port: working notes

This branch (`linux-arm64-asahi`) ports the Graine desktop app
(`glue/rexglue-sdk-main/gta4-recomp`) to Linux arm64. Read `LINUX_PORT.md`
first: it lists every change, the build commands and the known issues.

## The machine

- MacBook Air M2, 8 GB, fanless. Fedora Asahi Remix with a 16K-page kernel,
  Mesa Honeykrisp Vulkan, niri (Wayland), fish shell.
- Installed build: `~/Applications/theft4/{bin,Resources}`, launched by the
  fish function `theft4` (see `linux/theft4.fish`), also from Pegasus (`gta-iv`).
- Game data and config: `~/.local/share/LibertyRecomp` (`native.toml` holds
  flat cvar keys).
- Logs (only with `--diagnostics`): `~/Applications/theft4/bin/logs`.

## Working rules

- **Cloud sessions can't run or test the game.** Only the Asahi machine can.
  Make code changes on this branch and say exactly what to rebuild. The user
  builds, installs and tests locally.
- Batch fixes before a long rebuild; audit the code for every related issue
  first instead of discovering them one rebuild at a time.
- Rebuild scope, from fastest to slowest:
  - `ninja ... rexgpu-gta4-native` (about 5 min): renderer only,
    `src/graphics/gta4_native`.
  - `ninja ... LibertyRecomp` for changes in `gta4-recomp/src`: a few minutes.
  - SDK/runtime headers such as `include/rex/platform.h`, or
    `gta4-recomp/generated/gta4_init.h`: full rebuild, about 15-25 minutes at
    `-j3`.
  - Keep `VCPKG_ROOT=$PWD/thirdparty/vcpkg` exported or CMake re-configures fail.
- Measure before optimizing. The tools are in `linux/tools/`; the native
  profiler and `THEFT4_PASS_STATS=1` are described there.

## Lessons from the port

- The worst bug was the 0xE0000000 heap's 4 KB host offset on 16K-page
  hosts. The generated code and the runtime disagreed, causing random memory
  corruption (cutscene audio dying, streaming stalls, graphics glitches). Keep
  `REX_EMULATED_PHYS_HOST_OFFSET` as the single source of truth.
- iOS uses the Metal frontend and MoltenVK (`portabilitySubset`); Linux takes
  the less-tested plain Vulkan paths. When something works on iOS but not
  Linux, diff the portability branches first (the texture swizzle bug was one).
- The M2 GPU is tile-based: render-pass breaks are expensive. The
  attachment-subset scope reuse took the city from about 230 to about 70
  passes per frame.
- Performance swings between ~35 and ~13 fps at high settings are SoC power
  throttling (about 17 W, then a 7 W clamp), not a game bug.

## Open ideas

- Apply the 360 output gamma ramp in the native renderer (brightness).
- The Advanced menu's Save serializes every non-default cvar, including
  command-line flags such as `--diagnostics`; it should only save menu settings.
- Lower per-draw CPU cost in the render worker (it is single-threaded and
  about 95% busy in the city).
