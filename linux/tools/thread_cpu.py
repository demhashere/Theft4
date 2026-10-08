# Per-thread CPU% of LibertyRecomp over an interval, read from /proc (no ptrace).
import os, sys, time
pid = sys.argv[1]; secs = float(sys.argv[2]) if len(sys.argv) > 2 else 10
hz = os.sysconf('SC_CLK_TCK')
def snap():
    out = {}
    for tid in os.listdir(f'/proc/{pid}/task'):
        try:
            name = open(f'/proc/{pid}/task/{tid}/comm').read().strip()
            f = open(f'/proc/{pid}/task/{tid}/stat').read().rsplit(')', 1)[1].split()
            out[tid] = (name, int(f[11]) + int(f[12]), int(f[36]))  # utime+stime, last cpu
        except OSError:
            pass
    return out
a = snap(); t0 = time.time(); time.sleep(secs); b = snap(); dt = time.time() - t0
rows = []
for tid, (name, ticks, cpu) in b.items():
    if tid in a:
        rows.append(((ticks - a[tid][1]) / hz / dt * 100, tid, name, cpu))
rows.sort(reverse=True)
total = sum(r[0] for r in rows)
print(f'interval {dt:.1f}s  total {total:.0f}% of one core  threads {len(rows)}')
for pct, tid, name, cpu in rows[:25]:
    core = 'P' if cpu >= 4 else 'E'
    print(f'{pct:6.1f}%  tid {tid:>7}  last-cpu {cpu} ({core})  {name}')
