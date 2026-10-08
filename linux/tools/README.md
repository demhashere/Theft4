# Linux performance tools

Read-only helpers used while porting; none of them modify the game.

| Script | Use |
|---|---|
| `thread_cpu.py PID SECONDS` | Per-thread CPU % and last core (P/E on Apple Silicon) from `/proc`. |
| `fps_watch.py PID SECONDS OUT` | Twice a second: frame rate (from the guest frame counter), busy threads and their cores, CPU cluster clocks. |
| `fps_analyze.py OUT` | Splits an `fps_watch.py` log into slow/normal/fast phases and compares thread placement. |
| `profile_frames.py DIR` | Summary of a native-profiler export (`~/.local/share/LibertyRecomp/Diagnostics`). |
| `profile_cpu_ops.py DIR/native-performance-cpu.csv` | Render-worker CPU operations ranked by self time. |
| `profile_gpu_passes.py DIR/native-performance-gpu-passes.csv` | GPU time per render phase (needs `gta4_profile_native_detailed_gpu = true`). |
| `profile_slow_vs_fast.py DIR` | Compares the slowest and fastest third of a capture: draw counts, GPU time, per-operation cost. |

Native profiler capture: launch with
`--diagnostics --diagnostics_categories=native-profiler`, then (one capture per
process, up to 600 frames):

    gdb -q -batch -p $(pgrep -x LibertyRecomp) -ex 'call (int)rex_gta4_native_profile_start()' -ex detach

Render-pass statistics: launch with `THEFT4_PASS_STATS=1`; every 600 frames a
summary (passes per frame, draws per pass, scope-break reasons) is appended to
`~/.local/share/LibertyRecomp/Diagnostics/pass-stats.txt`.
