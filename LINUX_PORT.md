# Theft4 on Linux arm64 (Asahi Linux / Apple Silicon)

This branch builds the maintained desktop app (`glue/rexglue-sdk-main/gta4-recomp`,
the Graine consumer) natively for Linux on 64-bit ARM, and fixes what breaks on
Apple Silicon running Asahi Linux: 16K kernel pages, the Honeykrisp Vulkan
driver, and the tile-based GPU.

Tested on a MacBook Air M2 (8 GB) with Fedora Asahi Remix (16K-page kernel),
Mesa 26.2 Honeykrisp, the niri Wayland compositor, the EU (PAL) disc and
Title Update 8. Audio, cutscenes and rendering are correct. In busy city
areas it runs at roughly 30-40 fps at 1707x960 with bilinear upscaling (see
the known issues below for fanless machines).

## What changed

**Build and platform**
- `LIBERTY_RECOMP_GRAINE_LINUX` (top-level `CMakeLists.txt`) builds the Graine
  desktop app on Linux; the legacy `LibertyRecomp/` app no longer builds.
  `gta4-recomp/src/platform_services_linux.cpp` replaces the three macOS
  `.mm` service files (no Game Center, no user music, microphone authorized).
- `THEFT4_LAB_BUILD` is defined for the renderer on Linux; the non-lab path is stale.
- `toolchains/linux-clang.cmake` derives the target architecture from
  `VCPKG_TARGET_TRIPLET`. It previously came out empty, which silently dropped
  FFmpeg's aarch64 code.
- `cmake/asahi-compat/PythonInterp`: CMake 4 no longer ships `FindPythonInterp`,
  which glslang needs.
- The stale bundled o1heap in `thirdparty/CMakeLists.txt` is disabled (it
  shadowed the SDK's o1heap 3.0). The DXC path in `src/graphics/CMakeLists.txt`
  is architecture-generic.
- `REXGLUE_AARCH64_CPU` (in `cmake/rexglue_helpers.cmake`) tunes the
  recompiled code for a specific core, e.g. `apple-m1` for inline LSE atomics.
- The Linux app sets the iOS build's renderer and audio switches by default:
  `THEFT4_GRAPHICS_PREPARATION`, `THEFT4_CONSTANT_REUSE`, `THEFT4_XMA_INLINE`,
  `THEFT4_HARDWARE_SMAA`, `THEFT4_FUSED_SMAA`. An explicit environment value
  overrides each one.

**16K pages** (`src/core/memory_posix.cpp`, `src/system/xmemory.cpp`)
- Protect and commit round outward to host pages and decommit rounds inward.
  A misaligned commit inside a reservation goes straight to the aligned
  `mprotect` path.
- **The 0xE0000000 physical heap's 4 KB offset.** On hosts whose mapping
  granularity is above 4 KB, the view is mapped at the rounded-down offset and
  every access adds 0x1000. The memory runtime already did this, but the
  generated code (`REX_PHYS_HOST_OFFSET`) and `GuestPtr` only did on
  Windows/Darwin. On Linux arm64 the game's CPU code and the GPU/XMA/kernel
  side therefore disagreed by 4 KB, which caused cross-heap memory corruption.
  This was the root cause of cutscene audio cutting out, streaming stalls, sky
  streaks and broken lights/shadows. One switch, `REX_EMULATED_PHYS_HOST_OFFSET`
  in `include/rex/platform.h`, now drives all four places (generated header,
  codegen template, `GuestPtr`, heap and view mapping).
- Guest memory uses `memfd_create` instead of named `shm_open`. Named segments
  leaked into `/dev/shm` on every crash until the next run died with SIGBUS.

**Honeykrisp / Vulkan** (`src/graphics/gta4_native/graphics_system.cpp`)
- Vertex input locations are compacted. Honeykrisp exposes 16 vertex
  attributes, but the usage-to-location table spans 0-39.
- `k_24_8` maps to `D32_SFLOAT_S8_UINT` because Honeykrisp has no D24S8.
- Texture views use the identity swizzle, as on MoltenVK
  (`gta4_native_guest_texture_view_swizzle`, default false). Applying the
  guest swizzle in the view swapped red and blue on ARGB textures (taillights,
  sparks).
- `gta4_native_triangle_fans` (default true) gates triangle-fan draws. GTA's
  depth-only visibility draws use them; disabling them stalls cutscenes.

**Performance**
- **Attachment-subset render-scope reuse** (`gta4_native_superset_scope_reuse`,
  default true). Draws bind only the attachments they write, so GTA IV's
  deferred lights, which alternate stencil-only setup draws with colour draws,
  started a new render pass for nearly every draw (about 230 passes per frame
  in the city). Tile-based GPUs load and store every attachment per pass. The
  renderer now keeps the active pass when a draw needs only a subset of its
  attachments, bound from the same guest surfaces, and records it with a zero
  write mask for the extra ones. That brings it down to about 70 passes per
  frame.

**Presentation**
- `ui_event_wait_timeout_ms` (Linux default 4 ms) plus
  `linux/patches/sdl3-events-x11-wakeup.patch`: Mesa's X11 WSI shares SDL's
  display connection and could swallow SDL's wakeup, freezing presentation
  for SDL's 3-second poll interval.
- Native Wayland surface (`WaylandWindowSurface`, `VK_KHR_wayland_surface`).
  The default stays X11/XWayland; set `SDL_VIDEO_DRIVER=wayland` to opt in.
- `gta4_native_upscaler = "bilinear"`: renders at the `gta4_fsr1_quality`
  scale and scales up without FSR's edge reconstruction.
- `gta4_motion_blur` (default true): false selects the title's blur-free
  composite pass. This is the same mapping as the iOS launcher setting.
- `THEFT4_DEPTH_OF_FIELD=0` (already supported) disables depth-of-field blur.

**Installer**
- The EU (PAL) disc (media `4A53F9F6`) with TU8 is accepted alongside the USA
  release. The EU and USA TU8 executables recompile to identical code.

**Diagnostics**
- `THEFT4_PASS_STATS=1`: per-600-frame render-pass statistics. See
  `linux/tools/README.md`, which also covers the measurement scripts.

## Requirements

- clang, cmake (4.x works), ninja, git, python3, and the X11/Wayland
  development headers SDL3 needs.
- Fedora packages this build additionally needed: `nasm libdecor-devel
  libtool perl-IPC-Cmd perl` (the full `perl` package; OpenSSL's build uses
  FindBin).
- **DXC for linux-arm64.** There's no upstream binary. Build DXC v1.9.2609
  from source and place the stripped results in the XenosRecomp submodule:
  `tools/XenosRecomp/thirdparty/dxc-bin/bin/arm64/dxc-linux` and
  `tools/XenosRecomp/thirdparty/dxc-bin/lib/arm64/libdxcompiler.so`.
- About 8 GB of RAM plus swap. Use `-j3` on 8 GB machines.

## Build

```sh
git submodule update --init --recursive   # the iOS/console-only submodules can be skipped
git -C glue/rexglue-sdk-main/thirdparty/sdl3 apply ../../../../linux/patches/sdl3-events-x11-wakeup.patch

export VCPKG_ROOT=$PWD/thirdparty/vcpkg VCPKG_FORCE_SYSTEM_BINARIES=1
cmake --preset linux-arm64-release \
  -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
  -DPythonInterp_DIR=$PWD/cmake/asahi-compat/PythonInterp \
  -DREXGLUE_AARCH64_CPU=apple-m1 \
  "-DCMAKE_C_FLAGS=-fPIC -mcpu=apple-m1" "-DCMAKE_CXX_FLAGS=-fPIC -mcpu=apple-m1"
ninja -C out/build/linux-arm64-release -j3 LibertyRecomp rexgpu-gta4-native rexgpu-xenos
```

Keep `VCPKG_ROOT` exported for later `ninja` runs as well. CMake
re-configures fail with "VCPKG_ROOT is not defined" without it.

## Install layout

```
<prefix>/bin/LibertyRecomp                 out/build/linux-arm64-release/LibertyRecomp/bin/
<prefix>/bin/librexruntime.so              glue/rexglue-sdk-main/out/linux-arm64/
<prefix>/bin/librexgpu-gta4-native.so      glue/rexglue-sdk-main/out/linux-arm64/
<prefix>/bin/librexgpu-xenos.so            glue/rexglue-sdk-main/out/linux-arm64/
<prefix>/bin/libTracyClient.so             glue/rexglue-sdk-main/out/linux-arm64/
<prefix>/Resources/aes_key.bin             LibertyRecompLib/aes_key.bin
<prefix>/Resources/button_prompts/         LibertyRecompLib/private/button_prompts/
<prefix>/Resources/font_atlases/           LibertyRecompLib/font_atlases/
```

The executable finds its libraries through `RUNPATH $ORIGIN`, and finds
`Resources` next to `bin`. Run it from `bin` (see `linux/theft4.fish`).

On first launch the installer asks for the game disc image and the TU8 file;
game data goes to `~/.local/share/LibertyRecomp`. The first boot also builds
the pipeline cache, which can take several minutes with a black screen.

Settings are read from `~/.local/share/LibertyRecomp/native.toml`. Start from
`linux/native.toml.example`. The in-game Advanced graphics menu writes that
file when you choose **Save**.

## Known issues

- **Fanless Macs throttle hard.** Under sustained load the SoC firmware
  alternates between about 17 W and a 7 W clamp (MacBook Air M2), giving
  about 35 fps and then about 13 fps every 15-20 seconds. Linux has no
  equivalent of macOS's gradual performance control. Use the bilinear
  settings in the example config and consider `gta4_frame_limit = 30`.
- The Advanced menu's Save writes **every** non-default cvar, including
  command-line flags such as `--diagnostics`. Remove those lines afterwards.
- The native renderer doesn't apply the 360's output gamma ramp.
- The default `gta4_multiplayer_backend = "community"` retries an online
  profile sync every few seconds when the server is unreachable. It runs on a
  background thread; set `"offline"` to silence it.
