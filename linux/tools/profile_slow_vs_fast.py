import csv, sys, statistics as st, collections
d = sys.argv[1]
rows = list(csv.DictReader(open(f'{d}/native-performance-frames.csv')))
gpu = {r['frame']: float(r['duration_ms']) for r in csv.DictReader(open(f'{d}/native-performance-gpu-slices.csv'))}
ops = collections.defaultdict(dict)
for r in csv.DictReader(open(f'{d}/native-performance-cpu.csv')):
    k = r['phase'] + '/' + r['operation']
    ops[r['frame']][k] = ops[r['frame']].get(k, 0) + float(r['self_ms'])
f = lambda r, k: float(r[k]) if r[k] not in ('', None) else 0.0
frames = []
for a, b in zip(rows, rows[1:]):
    ta, tb = f(a, 'publish_end_tick'), f(b, 'publish_end_tick')
    if ta and tb > ta: frames.append(((tb - ta) / 1e6, b))
frames.sort(key=lambda x: x[0])
n = len(frames); fast = frames[: n // 3]; slow = frames[-n // 3:]
def mean(sel, fn): return st.mean(fn(r) for _, r in sel)
cols = ['commands', 'worker_draw_commands', 'producer_capture_sum_ms', 'producer_geometry_capture_sum_ms',
        'producer_texture_capture_sum_ms', 'backpressure_sum_ms', 'worker_assembly_ms', 'worker_idle_ms']
print(f'{"metric":40s} {"fast third":>11s} {"slow third":>11s}')
print(f'{"frame interval ms":40s} {st.mean(x for x,_ in fast):11.2f} {st.mean(x for x,_ in slow):11.2f}')
print(f'{"GPU ms":40s} {mean(fast, lambda r: gpu.get(r["frame"], 0)):11.2f} {mean(slow, lambda r: gpu.get(r["frame"], 0)):11.2f}')
for c in cols:
    print(f'{c:40s} {mean(fast, lambda r: f(r, c)):11.2f} {mean(slow, lambda r: f(r, c)):11.2f}')
allk = set(k for fr in ops.values() for k in fr)
diffs = []
for k in allk:
    fa = st.mean(ops[r['frame']].get(k, 0) for _, r in fast); sl = st.mean(ops[r['frame']].get(k, 0) for _, r in slow)
    diffs.append((sl - fa, k, fa, sl))
diffs.sort(reverse=True)
print('--- worker CPU ops (self ms/frame), biggest increase fast -> slow')
for dlt, k, fa, sl in diffs[:14]:
    print(f'  {k:52s} {fa:7.2f} -> {sl:7.2f}  (+{dlt:.2f})')
