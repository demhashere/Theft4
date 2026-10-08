import re,sys,collections
rows=[]
for line in open(sys.argv[1]):
    m=re.match(r'(\S+) fps=\s*([\d.]+) E=(\d+) P=(\d+) \| (.*)',line)
    if not m: continue
    t,fps,e,p,rest=m.groups()
    th={tid:(name.strip(),float(pct),k,int(c)) for name,tid,pct,k,c in re.findall(r'(.{1,10}?):(\d+):(\d+)%@([PE])(\d)',rest)}
    rows.append((t,float(fps),int(e),int(p),th))
fps=[r[1] for r in rows]
print('samples',len(rows),'fps min %.1f max %.1f mean %.1f'%(min(fps),max(fps),sum(fps)/len(fps)))
print('timeline:',' '.join(str(int(f)) for f in fps))
# per-thread placement vs fps buckets
tids=collections.Counter()
for r in rows:
    for tid,(n,pct,k,c) in r[4].items(): tids[tid]+=pct
top=[t for t,_ in tids.most_common(6)]
names={}
for r in rows:
    for tid,(n,pct,k,c) in r[4].items(): names[tid]=n
def bucket(f): return 'slow(<25)' if f<25 else ('mid' if f<45 else 'fast(>=45)')
stats=collections.defaultdict(lambda: collections.Counter())
cnt=collections.Counter()
for r in rows:
    b=bucket(r[1]); cnt[b]+=1
    for tid in top:
        if tid in r[4]:
            n,pct,k,c=r[4][tid]; stats[(b,tid)][k]+=1
            stats[(b,tid)]['pct']+=pct
for b in ['slow(<25)','mid','fast(>=45)']:
    if not cnt[b]: continue
    print(f'--- {b}: {cnt[b]} samples, mean E-freq {sum(r[2] for r in rows if bucket(r[1])==b)/cnt[b]:.0f} P-freq {sum(r[3] for r in rows if bucket(r[1])==b)/cnt[b]:.0f}')
    for tid in top:
        s=stats[(b,tid)]; tot=s['P']+s['E']
        if tot: print(f'   {names[tid]:12s} {tid}: on P {100*s["P"]/tot:3.0f}%  avg cpu {s["pct"]/tot:3.0f}%')
