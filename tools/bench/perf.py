#!/usr/bin/env python3
# The benchmark: the port against the original, in the same container,
# one after the other, with the same scripted input and the same seed.
#
#   tools/bench/perf.py [runs]       (default 3 runs of each)
#
# Both run on a virtual display (Xvfb, 960x540, the scale 2 of both), with
# OpenGL (Mesa), every frame drawn, no cap on the frames, and the sound
# mixed but not played (the port on the "disk" driver of SDL, to
# /dev/null; the original on the "null" driver of OpenAL). Each run is
# measured as a process: the time on the clock, the CPU time (user and
# system, of all of its threads), and the peak of its resident memory.
# The display server is a separate process, not measured.
#
# Needs `tools/setup.sh`, `xvfb`, and `love` 11. Builds the port with -O2
# into build/snx.

import os, resource, subprocess, sys, time, statistics

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
os.chdir(ROOT)

RUNS = int(sys.argv[1]) if len(sys.argv) > 1 else 3

CLICK_ARENA = "60:mv:55:125,62:dn:m1,64:up:m1"
SCENARIOS = [
    ("menu",    600,  ""),
    ("shop",    720,  CLICK_ARENA),
    ("level 1", 1800, CLICK_ARENA + ",450:mv:60:60,452:dn:m1,454:up:m1,520:mv:390:250,522:dn:m1,524:up:m1"),
    ("stress",  1500, CLICK_ARENA + ",450:mv:60:60,452:dn:m1,454:up:m1,520:mv:390:250,522:dn:m1,524:up:m1"),
]
STRESS = 200      # seekers more in the arena of the scenario "stress"

def build ():
    env = dict(os.environ, ATMOS_CFLAGS="-O2")
    subprocess.run(["./run.sh", "-o", "build/snx", "main.atm"], check=True, env=env)
    # the original gets the bench (as tools/bench/run.sh)
    main = open("ext/SNKRX/main.lua").read()
    if "SNKRX_BENCH" not in main:
        subprocess.run(["git", "apply", os.path.join(ROOT, "tools/bench/original.patch")],
                       cwd="ext/SNKRX", check=True)
    for f in ["bench.lua", "dump.lua"]:
        subprocess.run(["cp", "tools/bench/" + f, "ext/SNKRX/"], check=True)

def measure (cmd, env):
    t0 = time.monotonic()
    p = subprocess.Popen(cmd, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    _, status, ru = os.wait4(p.pid, 0)
    wall = time.monotonic() - t0
    assert os.waitstatus_to_exitcode(status) == 0, cmd
    return wall, ru.ru_utime + ru.ru_stime, ru.ru_maxrss / 1024

def port (name, frames, inp, display):
    env = dict(os.environ, DISPLAY=display, SNX_FRAMES=str(frames), SNX_INPUT=inp,
               SNX_STRESS=str(STRESS) if name == "stress" else "", SNX_RENDER="1", SNX_RENDERER="opengl",
               SDL_VIDEODRIVER="x11", SDL_AUDIODRIVER="disk", SDL_DISKAUDIOFILE="/dev/null",
               SDL_DISKAUDIODELAY="0")
    env.pop("SNX_MUTE", None)
    return measure(["build/snx"], env)

def original (name, frames, inp, display):
    env = dict(os.environ, DISPLAY=display, SNKRX_BENCH="1", SNKRX_SEED="1",
               SNKRX_STRESS=str(STRESS) if name == "stress" else "", SNKRX_FRAMES=str(frames), SNKRX_INPUT=inp, SNKRX_TRACE="/dev/null",
               ALSOFT_DRIVERS="null")
    return measure(["love", "ext/SNKRX"], env)

def main ():
    build()
    display = ":97"
    xvfb = subprocess.Popen(["Xvfb", display, "-screen", "0", "960x540x24"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    try:
        print("runs: %d (median)" % RUNS)
        print("%-8s %-9s %7s %9s %9s %9s %8s" %
              ("scenario", "program", "ticks", "clock s", "cpu s", "ticks/s", "peak MB"))
        for name, frames, inp in SCENARIOS:
            for prog, f in [("port", port), ("original", original)]:
                rs = [f(name, frames, inp, display) for _ in range(RUNS)]
                wall = statistics.median(r[0] for r in rs)
                cpu = statistics.median(r[1] for r in rs)
                mem = statistics.median(r[2] for r in rs)
                print("%-8s %-9s %7d %9.2f %9.2f %9.0f %8.1f" %
                      (name, prog, frames, wall, cpu, frames / wall, mem))
                sys.stdout.flush()
    finally:
        xvfb.terminate()

main()
