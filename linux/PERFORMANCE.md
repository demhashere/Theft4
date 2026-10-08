# Linux arm64 performance: measurements and targets

Measured on the target machine: MacBook Air M2 (8 GB, fanless), Fedora Asahi
Remix (16K pages), Mesa Honeykrisp Vulkan, niri (Wayland). The scene is a busy
Broker street, the same spot for every capture. The tools are in
`linux/tools/` (see its README for capture commands).

## Where frame time goes (busy street, 1506x847 render, before the subset-scope fix)

| Metric | Value |
|---|---|
| Frame interval (median / p90) | 34 ms / 42-47 ms (~30 fps) |
| GPU time per frame (coarse timestamps) | 24-25 ms |
| Render worker thread | **95% busy, single thread, ~22 ms CPU per frame** |
| GTA render (producer) thread | ~50-85% busy; capture 20-25 ms per frame, of which 10-14 ms is back-pressure waiting for the worker |
| Draws per frame | 3,000-4,400 (12-16K commands) |

Render worker CPU per frame (self time, busy street):

| Operation | ms/frame |
|---|---|
| `vkQueueSubmit` (one per frame; Honeykrisp does real work here) | 2.9 |
| upload-capacity | 2.5 |
| constant-binding | 2.1 |
| vk-draw | 1.8 |
| prepare-textures | 1.7 (1.3 self) |
| record-command | 1.5 self (12 inclusive) |
| buffer-upload | 1.2 |
| record-indexed | 1.0 self (7.9 inclusive) |
| common-draw-bindings | 1.0 |
| constant-arena-capacity | 0.9 |
| shared-draw-constants | 0.9 |
| persistent-buffer-lookup | 0.5 |

In slow frames every one of these grows about 40% per draw, not only with draw
count: that is SoC power throttling (see below), not an algorithmic cliff.

GPU per phase (detailed timestamps; they inflate on tile-based GPUs, so trust
the proportions): shadows 34% (~2,240 shadow draws/frame), deferred lights
31%, G-buffer 15%, forward scene 12%, reflections/water/radar ~6%.

## Fixed already (on this branch)

- 0xE0000000 heap 4 KB offset on 16K-page hosts (`REX_EMULATED_PHYS_HOST_OFFSET`).
  Root cause of streaming stalls, lost cutscene audio and broken geometry.
- Attachment-subset scope reuse (`gta4_native_superset_scope_reuse`): render
  passes in the city went from ~230 to ~70 per frame (deferred lights no
  longer split every draw into its own pass).
- `-mcpu=apple-m1` (inline LSE atomics), iOS runtime switches
  (`THEFT4_GRAPHICS_PREPARATION`, `THEFT4_CONSTANT_REUSE`, `THEFT4_XMA_INLINE`,
  `THEFT4_HARDWARE_SMAA`, `THEFT4_FUSED_SMAA`) on by default on Linux.

Remaining pass structure after the fix (`THEFT4_PASS_STATS=1`, busiest window):
71 passes/frame, of which 26 end at resolves (copy to texture), about 39 hold
no draws at all (clear-only or empty), and 20 still hold a single draw.

## Target 1: the render worker is the CPU bottleneck

The iOS build ships a set of CPU-side renderer optimizations that are compiled
out on Linux. Each `Native*Enabled()` function in
`src/graphics/gta4_native/graphics_system.cpp` is gated on
`__APPLE__ && THEFT4_LAB_BUILD && TARGET_OS_IPHONE` and returns false
elsewhere:

| Switch (env on iOS) | What it changes |
|---|---|
| `NativeCommandStreamEnabled` (`THEFT4_COMMAND_STREAM`) | Batched state packets in the producer->worker transport (around line 4680) |
| `NativeFrameAssemblyEnabled` (`THEFT4_FRAME_ASSEMBLY`) | Frame-assembly request caches, prepared indices, hot-cache reuse |
| `NativeParallelTextureConversionEnabled` (`THEFT4_PARALLEL_TEXTURE_CONVERSION`) | Converts untiled textures on a helper thread |
| `NativeRendererEfficiencyEnabled` (`THEFT4_RENDERER_EFFICIENCY`) | Barrier batching, sparse indexed walks, fixed-derivation reuse |
| `NativeCpuCleanupRequested` (`THEFT4_CPU_CLEANUP`) | Frees completed frame CPU records off the worker |
| `NativeMemoryRecoveryEnabled` (`THEFT4_MEMORY_RECOVERY`) | Responds to memory pressure by trimming caches |
| `NativePrewarmTargetReuseEnabled` (`THEFT4_PREWARM_TARGET_REUSE`) | Reuses resolved targets during pipeline prewarm |

The helpers they rely on, `native_preparation_task.h` and
`native_deferred_cleanup.h`, use Grand Central Dispatch on Apple and fall back
to serial or no-op elsewhere. So on Linux all of that work stays on the
already saturated worker thread.

## Target 2: tile-based GPU efficiency

Apple GPUs load and store every attachment per render pass. Remaining
opportunities:
- About 39 draw-less passes per frame: fold clears into the next pass's
  `loadOp = CLEAR` or `vkCmdClearAttachments` inside an open pass, and skip
  empty passes.
- 26 resolves per frame end the active pass. Where a resolve copies the
  attachment that was just rendered, consider in-pass handling or at least
  avoiding a redundant load/store.
- Load/store ops: use `DONT_CARE` where content is proven dead (the renderer
  already tracks aspect content and writer serials).
- About 375 barriers per frame (before the scope fix); batch them where
  ordering allows. Part of this is `NativeRendererEfficiencyEnabled`.

## Target 3: power, which is the real ceiling on fanless machines

Under sustained load the M2 Air alternates between ~16-18 W and a 7 W firmware
clamp every 15-20 s (fps 35 -> 13). Fewer wasted CPU cycles leave more of the
power budget for the GPU, and avoid polling:
- `src/core/threading_posix.cpp`: `WaitMultiple` polls every 1 ms with
  `nanosleep`. `threading_mac.cpp` contains a corrected, notification-based
  version (`WaitMultipleCorrected`, `MultiWaitWakeState`, gated on
  `RuntimeWaitFixesEnabled()`). It isn't ported to POSIX, and the Linux app
  never calls `ConfigureRuntimeWaitFixes`.

## Target 4: memory on Honeykrisp

Honeykrisp reports one 3.66 GiB heap and **no `VK_EXT_memory_budget`**, so
`QueryNativeTextureHeapBudgets()` returns unavailable and texture eviction
never runs proactively (only on allocation failure). With 8 GB of unified RAM
the system is already swapping. A fallback budget (heap size and/or
`/proc/meminfo` MemAvailable, `/proc/pressure/memory`) would let
`EvictNativeTextureImages` and memory recovery work on Linux.

## How to measure a change

FPS alone is unreliable here because of power throttling. Compare per-frame
costs instead, at the same spot, on the same settings, warmed up:
1. Launch with `--diagnostics --diagnostics_categories=native-profiler`, then
   arm a 600-frame capture with gdb (`linux/tools/README.md`).
2. Run `linux/tools/profile_frames.py` and `profile_cpu_ops.py` on
   `~/.local/share/LibertyRecomp/Diagnostics`, and compare worker ms per
   operation, GPU ms and producer back-pressure between the A and B runs.
3. Run `THEFT4_PASS_STATS=1` for render-pass counts.
4. Run `linux/tools/fps_watch.py` for frame rate against thread placement over time.
