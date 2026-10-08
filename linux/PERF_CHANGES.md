# perf/linux-renderer: what changed and how to A/B it

Branch `perf/linux-renderer`, based on `linux-arm64-asahi` (4ca1fc1). Every
behaviour change is **off by default on Linux** and switched at launch by an
environment variable or a `native.toml` cvar. With nothing set, the build
behaves like `linux-arm64-asahi`, apart from the "always on" refactors in
section C, which produce identical output.

Nothing here has run on the M2. It was compiled and unit-tested on x86_64
only (see "What was verified").

## Rebuild

From the repository root, in fish:

```fish
set -x VCPKG_ROOT $PWD/thirdparty/vcpkg
ninja -C out/build/linux-arm64-release -j3 LibertyRecomp rexgpu-gta4-native
```

Then copy the three changed binaries into the install:

```fish
cp out/build/linux-arm64-release/LibertyRecomp/bin/LibertyRecomp ~/Applications/theft4/bin/
cp glue/rexglue-sdk-main/out/linux-arm64/librexruntime.so ~/Applications/theft4/bin/
cp glue/rexglue-sdk-main/out/linux-arm64/librexgpu-gta4-native.so ~/Applications/theft4/bin/
```

| What changed | Target | Time |
|---|---|---|
| `src/graphics/gta4_native/*` (all renderer changes) | `rexgpu-gta4-native` (plugin only) | ~5 min |
| `src/core/threading_posix.cpp` | `rexruntime`, pulled in by `LibertyRecomp` | a few min |
| `gta4-recomp/src/gta4_app.cpp` | `LibertyRecomp` | a few min |

No SDK header that the generated code includes was touched, and nothing in
`gta4-recomp/generated/`, so no full 25-minute rebuild is needed.

## How to switch things

- **Environment variables (`THEFT4_*`):** set them for one launch, e.g.
  `env THEFT4_COMMAND_STREAM=1 THEFT4_CPU_CLEANUP=1 theft4`. The `theft4`
  fish function passes the environment through. Only the exact value `1`
  enables a switch.
- **Cvars:** add to `~/.local/share/LibertyRecomp/native.toml`, e.g.
  `gta4_native_lean_upload_planning = true`. Remove the line, or set it to
  `false`, for the A run.

Every launch logs the switch state. Grep the log for `gta4-native-stream`,
`gta4-native-work`, `gta4-native-preparation`, `Theft4 runtime waits` and
`gta4-native-memory`.

## A/B procedure

This is PERFORMANCE.md's "How to measure", applied to each item below. FPS
alone is useless because of power throttling, so compare per-frame costs at
the same spot (busy Broker street), with the same settings, after warming up:

1. Launch with `--diagnostics --diagnostics_categories=native-profiler`. Use
   only that category: the native-trace, native-probes and
   native-translucency categories disable parallel preparation and binding
   reuse. Then arm a capture:
   `gdb -q -batch -p (pgrep -x LibertyRecomp) -ex 'call (int)rex_gta4_native_profile_start()' -ex detach`.
2. Run `linux/tools/profile_frames.py ~/.local/share/LibertyRecomp/Diagnostics`
   and `linux/tools/profile_cpu_ops.py .../native-performance-cpu.csv`. Compare
   worker ms per operation, GPU ms and producer back-pressure. Compare
   `self_ms / calls` per operation, not totals.
3. Run with `THEFT4_PASS_STATS=1` for render-pass counts.
4. Run `linux/tools/fps_watch.py` for frame rate against thread placement,
   and `linux/tools/thread_cpu.py PID 30` for per-thread CPU and cores. The
   worker is now named "Theft4 render" and the helpers "Theft4 constant",
   "Theft4 texture " and "Theft4 cleanup".

Profiler caveats, found while auditing:
- A capture turns producer state batching off (`profile_transport`), so it
  under-reports `THEFT4_COMMAND_STREAM`'s producer gain. Use `thread_cpu.py`
  and `profile_frames.py` back-pressure for that.
- Frame clearing and record cleanup run outside the CPU recorder, so CPU
  cleanup doesn't show up in `profile_cpu_ops.py`. Compare the worker % in
  `thread_cpu.py` with the "Theft4 cleanup" thread %.
- Helper threads have no recorder. Work moved to a helper simply vanishes
  from the per-op table. Check the "refresh-output" self time, which includes
  the wait for the constant helper.
- The per-op numbers in PERFORMANCE.md include profiler overhead: two clock
  reads per scope and 15-30 scopes per draw. Ops made of many tiny calls
  (persistent-buffer-lookup, buffer-upload, vk-bind) are mostly measurement.

## Suggested first runs

1. **Run 1, A:** today's `native.toml` plus `env` with nothing set.
2. **Run 2, B (CPU set, low risk):**
   ```fish
   env THEFT4_COMMAND_STREAM=1 THEFT4_CPU_CLEANUP=1 THEFT4_FRAME_ASSEMBLY=1 \
       THEFT4_RENDERER_EFFICIENCY=1 THEFT4_PREWARM_TARGET_REUSE=1 theft4
   ```
   with these lines in `native.toml`:
   ```toml
   gta4_native_lean_upload_planning = true
   gta4_native_skip_identity_restart_rewrite = true
   ```
3. **Run 3:** Run 2 plus `THEFT4_PARALLEL_PREPARATION=1`, once with and once
   without `THEFT4_HELPER_AFFINITY=performance`.
4. **Then, one at a time:** `THEFT4_RUNTIME_WAIT_FIXES=1`,
   `gta4_native_cached_constant_arena = true`, the memory switches and
   `THEFT4_PARALLEL_TEXTURE_CONVERSION=1`.

If Run 2 misbehaves, bisect by dropping one switch per launch. They are
independent, except that `THEFT4_CPU_CLEANUP` needs `THEFT4_COMMAND_STREAM`
and memory recovery needs a warning source (section B).

---

## A. Render-worker CPU, in order of expected impact

Target: the worker at ~95% and ~22 ms/frame (PERFORMANCE.md).

### A1. Lean upload planning: `gta4_native_lean_upload_planning` (cvar, default false)

- **What it does:**
  - `EnsureFrameUploadCapacity` sizes the staging arena every frame. For
    every vertex stream and index buffer of every draw it built a `std::set`
    node and did an XXH3 persistent-cache lookup.
  - MoltenVK skips this, because persistent blocks there are device-local,
    host-visible and filled by `memcpy`, never through staging. Honeykrisp is
    also unified memory, but the check was keyed on the MoltenVK driver ID.
  - With the cvar on, any driver with a device-local + host-visible memory
    type takes the MoltenVK path. Each distinct bound texture is also checked
    once per frame: about 1K resources versus ~16K bindings, using a per-resource
    frame stamp.
  - Always on, with identical results: the texture walk uses raw pointers
    instead of building a temporary `shared_ptr` (two atomics) per binding.
- **Expected:** `upload-capacity` 2.5 → ~1 ms/frame.
- **Risk:** if a persistent reservation ever fails, its upload falls back to
  staging that wasn't planned. `AllocateUpload` then grows an overflow block
  mid-frame: a hitch, not corruption.
- **Commits:** dd64e14, a2559df.

### A2. Parallel constant preparation: `THEFT4_PARALLEL_PREPARATION=1` (env, default off)

- **What it does:**
  - Ports the iOS helper from GCD to one persistent `std::thread`
    (`native_helper_thread.h`).
  - The vertex/pixel constant preparation (`PrepareGuestDrawConstants` for
    every draw) runs on the helper while the worker does `PrepareFrameTextures`.
  - The worker joins before recording. Shared constants stay on the worker.
- **Busy-frame cap:** the cap was 16384 frame commands; it is now 32768,
  because busy streets reach 12-16K.
- **Skipped when** fewer than 128 draws, or any trace or probe diagnostic is
  active.
- **Expected:** `constant-binding` 2.1 → ~0.3 ms, and less `common-draw-bindings`
  self time. The helper's time is invisible to the profiler.
- **Risk:** EAS may put the helper on an E-core, where it can take longer than
  the work it saves. Compare with `THEFT4_HELPER_AFFINITY=performance` (A9).
  The audit found no shared mutable state between the helper and the worker
  during the overlap.
- **Commits:** 5e5b191, f13974f.

### A3. Command stream: `THEFT4_COMMAND_STREAM=1`, plus `THEFT4_CPU_CLEANUP=1` (env, default off)

- **Command stream:**
  - The producer batches state into 8-command packets without taking
    `render_mutex_`.
  - The frame owns the command records, so the worker keeps pointers instead
    of moving ~3 KB records.
  - Recycled records keep their payload capacity.
- **CPU cleanup:** freeing completed frame records (destructors, refcounts,
  payload frees) runs on a nice +5 "Theft4 cleanup" thread instead of the
  worker. It needs the command stream.
- **Expected:**
  - lower producer back-pressure (10-14 ms/frame is spent waiting);
  - lower worker %;
  - the "Theft4 cleanup" thread shows the moved time.
- **Risk:** cross-thread frees contend on glibc arenas. Only one cleanup batch
  is in flight; frames that can't hand off clean up serially (counters in the
  light capture). With memory recovery active, cleanup is serial by design.
- **Commits:** 5e5b191, f13974f.

### A4. Renderer efficiency: `THEFT4_RENDERER_EFFICIENCY=1` (env, default off)

The iOS bundle, audited as valid on plain Vulkan:
- **Sparse texture-stage walk:** only applies with the indexed working-set
  descriptor backend. Check the log for `backend=indexed-working-set`.
- **Batched attachment barriers.**
- **Dynamic state kept across title pipeline switches:** all title pipelines
  declare the same seven dynamic states.
- **Viewport/scissor/bias derivation:** keyed only on its real inputs.

- **Expected:** fewer `vk-dynamic-state`, `vk-bind` and `vk-barrier` calls;
  lower `prepare-textures` and `common-draw-bindings`. Honeykrisp also emits
  less dirty state per draw.
- **Fix included:** the non-fused SMAA chain bound its own pipelines without
  resetting the draw-state cache. It is now reset explicitly; that path only
  matters with this switch on.
- **Commit:** 7203cde.

### A5. Frame assembly and prewarm target reuse: `THEFT4_FRAME_ASSEMBLY=1`, `THEFT4_PREWARM_TARGET_REUSE=1` (env, default off)

- **Frame assembly:**
  - Reuses the previous pipeline snapshot when the effective state is equal.
  - Adds a set-associative pipeline-request cache.
  - With A2 on, the helper also converts up to 32 index buffers per frame.
- **Prewarm target reuse:** memoizes the render target resolved for
  pipeline prewarm within a run of draws.
- **Expected:** lower `worker_assembly_ms` and `worker_snapshot_sum_ms` in
  `profile_frames.py`, and the `pipeline-request-reuses` counter goes up.
- **Commit:** f13974f.

### A6. Identity restart rewrite: `gta4_native_skip_identity_restart_rewrite` (cvar, default false)

- **What it does:** a 16-bit strip whose guest restart index is already
  0xFFFF is no longer copied index-by-index into staging on every draw; the
  uploaded buffer is bound directly. 32-bit indices are unchanged.
- **Expected:** lower `record-indexed` self time and the `index-upload-bytes`
  counter, if the city uses restart strips.
- **Commit:** 8070e57.

### A7. Cached constant arena: `gta4_native_cached_constant_arena` (cvar, default false; set before launch)

- **What it does:** frame constant arenas prefer HOST_CACHED (+coherent)
  memory. The CPU reads them back: delta parents are copied with memmove, and
  masked constants read their base from the arena.
- **Expected:** lower `constant-binding`. Only takes effect if Honeykrisp
  exposes a separate cached memory type: check with
  `vulkaninfo | grep -A6 memoryTypes`. Otherwise it is a no-op.
- **Commit:** e4e21f4.

### A8. Parallel texture conversion: `THEFT4_PARALLEL_TEXTURE_CONVERSION=1` (env, default off)

- **What it does:**
  - Untiled same-format textures copy whole rows (serial, no helper).
  - CTX1/DXN/DXT5A/DXT3A expansion splits half its rows onto a
    "Theft4 texture" helper. That helper is launched from `StartRenderWorker`,
    not lazily on a guest thread.
- **This is producer-side work**, not render worker: it shortens texture
  streaming spikes.
- **Expected:** lower p90/max `producer_texture_capture_sum_ms`.
- **Commit:** f13974f.

### A9. Helper placement: `THEFT4_HELPER_AFFINITY=performance` (env, default off)

- **What it does:** pins the constant and texture helpers to the
  highest-`cpu_capacity` cores (cpu4-7 on the M2). The cleanup helper is never
  pinned.
- **Use it as** a B/C variant of A2.
- **Commit:** 5e5b191.

## B. Power, polling and memory

### B1. Runtime wait fixes: `THEFT4_RUNTIME_WAIT_FIXES=1` (env, default off)

- **What it does:**
  - Ports the macOS notification-based `WaitMultiple` to
    `threading_posix.cpp`. Waiters subscribe to every object and park on one
    condition variable; every signal (event, semaphore, mutant, timer, thread
    exit) wakes them. Before, every multi-object wait polled every 1 ms.
  - The latch is the iOS bundle, so it also enables microsecond guest delays,
    rounded-up and absolute wait timeouts, and guest-increment
    `QueryPriority`. Alertable waits keep their 1 ms slices.
  - It is latched in `OnPreSetup`, before guest threads exist. The log line
    shows each part.
- **Expected:**
  - lower idle and total CPU of the guest threads in `thread_cpu.py`;
  - lower package power, so `fps_watch.py` should show fewer or later 7 W
    clamp phases;
  - fewer wakeups (`perf stat -e context-switches`).
- **Risk:** guest timing changes (delays now really sleep sub-millisecond).
  Review found and fixed a bug where a zero-timeout poll could report a
  timeout under mutex contention (ccdb7d7).
- **Commits:** e95ac4d, d68e9e0, ccdb7d7, 90f7f13.

### B2. Host memory budget: `gta4_native_host_memory_budget` (cvar, default false)

- **What it does:**
  - Honeykrisp has no `VK_EXT_memory_budget`, so texture budgets were
    "unavailable". In any case, budget pressure only reordered eviction; it
    never retired anything earlier than 600 frames.
  - With the cvar on, each heap gets a budget of renderer texture bytes plus
    `MemAvailable` above `gta4_native_host_memory_reserve_mb` (768), capped at
    75% of the heap.
  - Under that pressure, unreferenced textures may be retired after
    `gta4_native_host_memory_grace_frames` (120) instead of 600.
  - `/proc/meminfo` is read only on the 120-frame budget poll.
- **Expected:** less swap and lower RSS when memory is tight (watch
  `vmstat 1`, `free -m`, and the `texture-eviction`/`texture-retirement`
  ops). Nothing changes while there is headroom.
- **Risk:** textures re-uploaded after 4 s unused (more `buffer-upload`, more
  conversion).

### B3. Host memory warnings: `gta4_native_host_memory_warnings` (cvar, default false) + `THEFT4_MEMORY_RECOVERY=1` (env)

- **What it does:**
  - Every 30 frames, samples `/proc/pressure/memory` and `MemAvailable`. If
    PSI "some avg10" is at least `gta4_native_host_memory_psi_percent` (10),
    or MemAvailable is below the reserve, it raises the same renderer memory
    warning iOS gets from `didReceiveMemoryWarning`.
  - With `THEFT4_MEMORY_RECOVERY=1`, each warning starts 120 frames of
    recovery: trim the image pool, use a 60-frame texture grace (up to 64 MiB
    per request), trim buffer shadows (32 MiB), and do serial CPU cleanup.
  - Warnings start 600 frames apart and back off to 9600 while pressure
    persists.
- **Expected:** fewer swap storms; logs `gta4-native-memory: host pressure`.
- **Risk:** periodic trimming can hitch. Keep it off during CPU A/B runs.
- **Commits:** d3f53f7, 7203cde, 2ac342e.

## C. Always on (identical output, cheaper per draw)

- **Profiler scopes:** skip the thread-local recorder read (a TLS-descriptor
  call in the dlopen'd plugin) unless a capture is bound.
- **Cached startup state:** the GPU flight recorder and diagnostics state
  are latched once, so per-command and per-draw flight records and resource
  staging make no cross-library calls.
- **`PrepareFrameTextures`:** compares the room-probe string cvar once per
  frame, not per draw.
- **`FrameGenerationMap::Insert`:** hashes and probes once instead of two or
  three times. It backs shared constants (872-byte keys), upload maps,
  masked coverage and the arena index. Covered by a randomized differential
  test.
- **`EnsureFrameConstantArenaCapacity`:** counts draws during the masked walk
  instead of a second frame walk.
- **Render worker name:** "Theft4 render" on Linux.

Commits: 12ed684, e798ceb, a9baee9 (worker name in f13974f).

## What was verified (x86_64 container, not the M2)

- **Configure:** the `linux-release` preset configured with system
  curl/OpenSSL, because GitHub archive downloads for vcpkg are blocked here
  (`-DVCPKG_MANIFEST_INSTALL=OFF`).
- **Build:** `rexruntime` and `rexgpu-gta4-native` build and link with clang
  19. DXC 1.9.2609 was built from source, because the bundled DXC 1.8
  segfaults on the Bink shaders, as LINUX_PORT.md notes for arm64.
- **App:** `gta4_app.cpp` compiles with the `LibertyRecomp` flags. The
  executable itself wasn't linked; it needs the generated recompiled sources.
- **Unit tests, all passing:**
  - `wait_multiple_test`: runs against the real `librexruntime.so`, and
    includes the contention regression.
  - `native_helper_thread_test` and `native_host_memory_test`: run under
    ThreadSanitizer.
  - `native_draw_state_cache_test`: includes a new uniform-dynamic-state case.
  - `native_hotpath_cache_test`: includes a new single-hash map differential
    test.
  - Also: `native_hotpath_policy_test`, `dirty_state_delta_test`,
    `native_optimization_lifetime_test` and `native_cpu_profile_test`.
- **Review:** an adversarial review of the commits found the zero-timeout
  wait bug, the profiler heap-column mix-up and some helper hardening. All are
  fixed.
- **Not verified:**
  - arm64 codegen;
  - Honeykrisp behaviour;
  - running the game;
  - any of the expected gains.

## Not done: tile-based GPU passes (priority 2)

Audited and designed, but not implemented. Every item needs care around
surface content tracking, so it should land after the CPU switches have been
measured. Findings:

- **The pass statistics undercount.** `pass_stats::Draw` is only called for
  title draws. Every internal pass (resolve conversion, materialization,
  packed-depth alias, depth handoff, present) issues one full-screen draw but
  counts as a 0-draw pass. SMAA, split post-FX and sun shafts aren't
  instrumented at all. Part of the "~39 draw-less passes" is these internal
  passes. **Fix the instrumentation first** (no behaviour change), then
  re-measure.
- **Resolve clears.** Every 360 resolve-with-clear opens its own
  CLEAR→STORE pass per cleared surface (`RecordResolveClears`, ~10-20 per
  frame). The next title scope then LOADs it back.
- **Guest clears.** A guest `Clear` opens a title scope that the next draw
  can never reuse, because both reuse paths require `previous_draw`.
- **`kReleaseResource` ends the open scope for any released resource**
  (textures, buffers, shaders), although only surface releases need it.
- **Store ops.** Title scopes always STORE every attachment; there is no
  DONT_CARE or STORE_OP_NONE anywhere.

Planned order, each behind a cvar, default false:
1. **Instrumentation fix:** count internal draws and clears; instrument the
   SMAA, split post-FX and sun shafts passes.
2. **`gta4_native_clear_scope_reuse`:** let guest clears join, and be joined
   by, compatible open scopes (low risk).
3. **End scopes only for surface releases** at `kReleaseResource` (low risk;
   check `QueueSurfaceImageRelease`).
4. **`gta4_native_store_op_none`:** `STORE_OP_NONE` for aspects that a
   bounded look-ahead proves the scope never writes.
   - Requires Vulkan 1.3.
   - Needs a runtime guard that breaks the scope if a command would write such
     an aspect.
5. **`gta4_native_deferred_clears`:** keep resolve and guest clears pending
   on the surface and apply them as the next scope's `loadOp=CLEAR`.
   - Flush points: resolve sources, materialization, depth handoff, surface
     release, frame end.
   - `ClaimSurfaceContent` serials must stay exactly as today.
   - Medium risk.
6. **`gta4_native_lazy_scope_begin`:** skip scopes whose commands all failed.

## Other hot-path items not done

- Walk-free upload planning from the previous frame's measured usage.
- Cheaper hashes for shared-constant and persistent-buffer keys.
- Caching the per-draw diagnostics `IsEnabled` calls in `RecordIndexedPrimitive`.
- Moving `vkQueueSubmit` (2.9 ms) to a helper thread: the queue lock is shared
  with the presenter, so it needs its own audit.
