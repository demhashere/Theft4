# Correlate GTA IV frame rate with thread placement and CPU clocks, from outside
# the process (/proc only). Usage: fps_watch.py PID SECONDS OUT
import os, sys, time, struct
pid, secs, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
GUEST = 0x100000000          # guest memory base (first mapping slot)
DEVICE_PTR = 0x831C22A4      # D3D device pointer (guest)
COMPLETED = 16552            # device + offset: completed frame counter
hz = os.sysconf('SC_CLK_TCK')
mem = open(f'/proc/{pid}/mem', 'rb', buffering=0)
def u32(addr):
    mem.seek(GUEST + addr); return struct.unpack('>I', mem.read(4))[0]
def counter(addr):
    # 64-bit big-endian counter if plausible, else a 32-bit one.
    mem.seek(GUEST + addr); value = struct.unpack('>Q', mem.read(8))[0]
    return value if value < (1 << 40) else u32(addr)
def freq(policy):
    try: return int(open(f'/sys/devices/system/cpu/cpufreq/{policy}/scaling_cur_freq').read()) // 1000
    except OSError: return 0
def threads():
    r = {}
    for tid in os.listdir(f'/proc/{pid}/task'):
        try:
            name = open(f'/proc/{pid}/task/{tid}/comm').read().strip()
            f = open(f'/proc/{pid}/task/{tid}/stat').read().rsplit(')', 1)[1].split()
            r[tid] = (name, int(f[11]) + int(f[12]), int(f[36]))
        except OSError: pass
    return r
device = u32(DEVICE_PTR)
log = open(out, 'w')
log.write(f'# device={device:08X}\n')
prev_t, prev_frames, prev_threads = time.time(), counter(device + COMPLETED), threads()
end = prev_t + secs
while time.time() < end:
    time.sleep(0.5)
    t, frames, th = time.time(), counter(device + COMPLETED), threads()
    dt = t - prev_t
    fps = (frames - prev_frames) / dt
    busy = []
    for tid, (name, ticks, cpu) in th.items():
        if tid in prev_threads:
            pct = (ticks - prev_threads[tid][1]) / hz / dt * 100
            if pct >= 15: busy.append((pct, tid, name, cpu))
    busy.sort(reverse=True)
    parts = ' '.join(f'{name[:10]}:{tid}:{pct:.0f}%@{"P" if cpu >= 4 else "E"}{cpu}' for pct, tid, name, cpu in busy[:7])
    log.write(f'{time.strftime("%H:%M:%S")} fps={fps:5.1f} E={freq("policy0")} P={freq("policy4")} | {parts}\n')
    log.flush()
    prev_t, prev_frames, prev_threads = t, frames, th
