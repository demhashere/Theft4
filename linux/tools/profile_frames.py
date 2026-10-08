import csv, sys, statistics as st
d = sys.argv[1]
rows = list(csv.DictReader(open(f'{d}/native-performance-frames.csv')))
gpu = {r['frame']: float(r['duration_ms']) for r in csv.DictReader(open(f'{d}/native-performance-gpu-slices.csv'))}
def f(r, k):
    try: return float(r[k])
    except (ValueError, KeyError): return None
def series(fn):
    v = [x for x in (fn(r) for r in rows[1:]) if x is not None]
    return v
def show(name, v):
    if not v: print(f'{name:34s} n/a'); return
    v = sorted(v); n = len(v)
    print(f'{name:34s} med {v[n//2]:7.2f}  p90 {v[int(n*.9)]:7.2f}  p99 {v[int(n*.99)]:7.2f}  max {v[-1]:7.2f}  mean {st.mean(v):7.2f}')
ticks = [f(r, 'publish_end_tick') for r in rows]
intervals = [(b - a) / 1e6 for a, b in zip(ticks, ticks[1:]) if a and b and b > a]
paint = [f(r, 'paint_end_tick') for r in rows]
paint_iv = [(b - a) / 1e6 for a, b in zip(paint, paint[1:]) if a and b and b > a]
print(f'frames {len(rows)}  render {rows[1]["render_width"]}x{rows[1]["render_height"]} display {rows[1]["display_width"]}x{rows[1]["display_height"]}')
show('frame interval (publish) ms', intervals)
show('present interval (paint) ms', paint_iv)
show('GPU frame ms', [gpu[r['frame']] for r in rows[1:] if r['frame'] in gpu])
show('producer CPU span ms', series(lambda r: (f(r,'cpu_end_tick') - f(r,'cpu_begin_tick')) / 1e6 if f(r,'cpu_end_tick') else None))
for k in ['producer_capture_sum_ms','producer_state_capture_sum_ms','producer_geometry_capture_sum_ms','producer_texture_capture_sum_ms','backpressure_sum_ms','queue_dwell_max_ms','worker_assembly_ms','worker_idle_ms','worker_dispatch_ms','worker_condition_wait_ms','worker_constant_sum_ms','worker_snapshot_sum_ms','internal_flush_ms','paint_total_ms','paint_acquire_ms','paint_present_ms','commands','worker_draw_commands']:
    show(k, series(lambda r: f(r, k)))
long = sorted(intervals)[int(len(intervals)*.95):]
print('slowest intervals:', ' '.join(f'{x:.1f}' for x in long[-12:]))
