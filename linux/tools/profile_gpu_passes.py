import csv, sys, collections
rows = csv.DictReader(open(sys.argv[1]))
by = collections.defaultdict(float); draws = collections.defaultdict(int); frames = set()
byphase = collections.defaultdict(float)
for r in rows:
    frames.add(r['frame'])
    try: ms = float(r['gpu_ms'])
    except ValueError: continue
    key = r['retail_phase_name'] or r['render_phase'] or r['classification']
    by[(key, r['classification'])] += ms
    byphase[key] += ms
    try: draws[(key, r['classification'])] += int(r['draws'] or 0)
    except ValueError: pass
n = len(frames)
total = sum(byphase.values()) / n
print(f'frames {n}  attributed GPU ms/frame {total:.2f}')
for k, v in sorted(byphase.items(), key=lambda kv: -kv[1])[:18]:
    print(f'  {v/n:6.2f} ms  {100*v/n/total:5.1f}%  {k}')
print('--- by phase/classification')
for k, v in sorted(by.items(), key=lambda kv: -kv[1])[:22]:
    print(f'  {v/n:6.2f} ms  draws/frame {draws[k]/n:7.1f}  {k[0]} / {k[1]}')
