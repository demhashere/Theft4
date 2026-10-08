import csv, sys, collections
rows = csv.DictReader(open(sys.argv[1]))
incl = collections.defaultdict(list); selfv = collections.defaultdict(float); frames = set()
per = collections.defaultdict(lambda: collections.defaultdict(float))
for r in rows:
    k = (r['phase'], r['operation']); frames.add(r['frame'])
    per[k][r['frame']] += float(r['inclusive_ms']); selfv[k] += float(r['self_ms'])
n = len(frames)
print(f'frames {n}')
print(f'{"phase/operation":52s} {"incl/frame":>10s} {"self/frame":>10s} {"p90 incl":>9s}')
items = sorted(per.items(), key=lambda kv: -selfv[kv[0]])
for k, fr in items[:30]:
    v = sorted(fr.values()); p90 = v[int(len(v)*.9)] if v else 0
    print(f'{k[0]+"/"+k[1]:52s} {sum(v)/n:10.3f} {selfv[k]/n:10.3f} {p90:9.3f}')
