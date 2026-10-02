#!/usr/bin/env python3
"""Stock check: compile a map, pack its pk3 and run it on stock ioquake3 (opengl1 and opengl2).

    tools/stockcheck/stockcheck.py content/benchmark bench

Exits 0 only if the compile and both engine runs pass. See README.md in this directory.
"""
import argparse
import datetime
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
RENDERERS = ["opengl1", "opengl2"]
PAKS = ["pak%d.pk3" % i for i in range(9)]
IMAGE_EXTS = (".tga", ".jpg", ".png")
MAX_DRIFT = 256  # a recorded eye this far from its request means the teleport was ignored
RUN_TIMEOUT = 120

COMPILE_STAGES = [
    ("bsp", ["-meta", "-keeplights"]),
    ("vis", ["-vis", "-saveprt"]),
    ("light", ["-light", "-patchshadows"]),
]
COMPILE_FAILURES = [re.compile(r"leaked", re.I), re.compile(r"Unknown q3map_\* directive")]
# missing images are failures that no allowlist entry can excuse
IMAGE_FAILURES = [re.compile(r"R_FindImageFile could not find"), re.compile(r"Couldn't find image file for shader")]
CHECKED_LINES = re.compile(r"WARNING|doesn't have a spawn function")
VIEWPOS = re.compile(r"^\((-?\d+) (-?\d+) (-?\d+)\) : (-?\d+)$")
COLOUR = re.compile(r"\^[0-9]")
LITERAL = re.compile(r"(?:[^\\.^$*+?{}\[\]|()]|\\[^A-Za-z0-9])*")  # plain characters and escaped punctuation only


def fail(message):
    print("stockcheck: " + message, file=sys.stderr)
    sys.exit(2)


def parse_args():
    p = argparse.ArgumentParser(description="Compile a map and run its pk3 on stock ioquake3.")
    p.add_argument("mod", help="mod directory (maps/, scripts/, textures/); its name is the fs_game")
    p.add_argument("map", help="map name, without .map")
    p.add_argument("--engine", default="/home/davis/projects/ioq3-custom/ioq3/build/Release/ioquake3",
                   help="ioquake3 binary; its renderer libraries sit beside it")
    p.add_argument("--q3map2", default=os.path.join(REPO, "install", "q3map2"), help="q3map2 binary")
    p.add_argument("--q3base", default="/home/davis/q3game/q3data", help="Quake 3 install holding baseq3/pak0-8.pk3")
    p.add_argument("--out", default=os.path.join(REPO, "stockcheck-out"), help="output root; each run gets a timestamped directory")
    return p.parse_args()


def check_paths(args):
    """Every input must exist before anything is compiled."""
    missing = []
    def need(path, executable=False):
        if not os.path.exists(path) or (executable and not os.access(path, os.X_OK)):
            missing.append(path)
    need(args.engine, True)
    need(args.q3map2, True)
    for pak in PAKS:
        need(os.path.join(args.q3base, "baseq3", pak))
    need(os.path.join(args.mod, "maps", args.map + ".map"))
    need(os.path.join(args.mod, "maps", args.map + ".cameras"))
    need(os.path.join(HERE, "allowlist.txt"))
    if missing:
        fail("missing: " + ", ".join(missing))


def read_cameras(path):
    views = []
    for n, line in enumerate(open(path), 1):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 5 or not re.fullmatch(r"[A-Za-z0-9_-]+", parts[0]):
            fail("%s:%d: expected 'name x y z yaw'" % (path, n))
        try:
            x, y, z, yaw = (float(v) for v in parts[1:])
        except ValueError:
            fail("%s:%d: expected 'name x y z yaw'" % (path, n))
        if any(v[0] == parts[0] for v in views):
            fail("%s:%d: duplicate view name %s" % (path, n, parts[0]))
        views.append((parts[0], x, y, z, yaw))
    if not views:
        fail(path + ": no views")
    return views


def read_allowlist(path):
    """One regular expression per line; '#' lines are the comments saying why each entry is benign."""
    entries = []
    for line in open(path):
        line = line.rstrip("\n")
        if line.strip() and not line.lstrip().startswith("#"):
            entries.append(re.compile(line))
    return entries


def image_entry(entry):
    """Only an entry anchored at both ends that spells out a missing-image message literally can excuse one: an image
    the engine looks up for itself and replaces with a built-in fallback. No wildcard and no general WARNING entry
    hides a missing image."""
    p = entry.pattern
    return (p.startswith("^") and p.endswith("$") and LITERAL.fullmatch(p[1:-1]) is not None
            and any(r.pattern in p for r in IMAGE_FAILURES))


def engine_commit(engine):
    src = os.path.dirname(os.path.abspath(engine))
    try:
        commit = subprocess.run(["git", "-C", src, "rev-parse", "--short", "HEAD"], capture_output=True, text=True,
                                check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown (not a git tree)"
    dirty = subprocess.run(["git", "-C", src, "diff", "--quiet", "HEAD"]).returncode != 0
    return commit + (" (tracked changes)" if dirty else "")


def make_root(root, q3base):
    os.makedirs(os.path.join(root, "baseq3"))
    os.makedirs(os.path.join(root, "home"))
    for pak in PAKS:
        os.symlink(os.path.join(os.path.abspath(q3base), "baseq3", pak), os.path.join(root, "baseq3", pak))


def compile_map(args, mod_name, tmp, out):
    """Compiles a copy of the mod in an isolated basepath. Returns (ok, reasons, compiled mod dir)."""
    root = os.path.join(tmp, "compile")
    make_root(root, args.q3base)
    mod = os.path.join(root, mod_name)
    shutil.copytree(args.mod, mod, symlinks=False)
    # stale outputs of an editor compile would otherwise be packed
    maps = os.path.join(mod, "maps")
    for ext in (".bsp", ".prt", ".srf", ".lin"):
        if os.path.isfile(os.path.join(maps, args.map + ext)):
            os.remove(os.path.join(maps, args.map + ext))
    lm_dir = os.path.join(maps, args.map)
    if os.path.isdir(lm_dir):
        for name in os.listdir(lm_dir):
            if name.startswith("lm_"):
                os.remove(os.path.join(lm_dir, name))
    map_path = os.path.join(mod, "maps", args.map + ".map")
    common = ["-game", "quake3", "-fs_basepath", root, "-fs_homepath", os.path.join(root, "home"), "-fs_game", mod_name]
    for stage, flags in COMPILE_STAGES:
        result = subprocess.run([args.q3map2] + common + flags + [map_path], capture_output=True, text=True,
                                errors="replace")
        log = result.stdout + result.stderr
        with open(os.path.join(out, "compile-%s.log" % stage), "w") as f:
            f.write(log)
        bad = [line for line in log.splitlines() if any(r.search(line) for r in COMPILE_FAILURES)]
        reasons = []
        if result.returncode != 0:
            reasons.append("q3map2 %s exited with %d" % (stage, result.returncode))
        reasons += ["%s: %s" % (stage, line.strip()) for line in bad]
        if reasons:
            return False, reasons, mod
    return True, [], mod


def pack(mod, mod_name, map_name, pk3):
    """Packs only what the engine needs; everything else (.map, .prt, .srf, *.import, READMEs) stays out."""
    entries = []
    def add(rel):
        if os.path.isfile(os.path.join(mod, rel)):
            entries.append(rel)
    add("maps/%s.bsp" % map_name)
    lm_dir = os.path.join(mod, "maps", map_name)
    if os.path.isdir(lm_dir):
        for name in sorted(os.listdir(lm_dir)):
            if name.startswith("lm_") and name.lower().endswith(IMAGE_EXTS):
                add("maps/%s/%s" % (map_name, name))
    for ext in (".jpg", ".tga"):
        add("levelshots/%s%s" % (map_name, ext))
    scripts = os.path.join(mod, "scripts")
    if os.path.isdir(scripts):
        for name in sorted(os.listdir(scripts)):
            if name.endswith(".shader") or name == "shaderlist.txt":
                add("scripts/" + name)
    for dirpath, dirnames, filenames in os.walk(os.path.join(mod, "textures")):
        dirnames.sort()
        for name in sorted(filenames):
            if name.lower().endswith(IMAGE_EXTS):
                add(os.path.relpath(os.path.join(dirpath, name), mod).replace(os.sep, "/"))
    with zipfile.ZipFile(pk3, "w", zipfile.ZIP_DEFLATED) as z:
        for rel in entries:
            z.write(os.path.join(mod, rel), rel)
    return entries


def write_cfg(path, map_name, views):
    lines = ["devmap " + map_name, "wait 300"]
    for name, x, y, z, yaw in views:
        lines += [
            "setviewpos %g %g %g %g" % (x, y, z, yaw),
            "wait 250",  # TeleportPlayer pushes the player forward for 160 ms; let friction stop them
            "echo stockcheck view " + name,
            "viewpos",
            "wait 20",
            "screenshot sc_" + name,
            "wait 20",
        ]
    lines += ["wait 100", "quit"]  # the last screenshot is written before quitting
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def check_log(log, views, allowlist):
    """Checks a console log (colour codes removed) for unallowlisted lines, missing images and view readbacks.
    Returns (reasons, {view name: (eye x, y, z, yaw)})."""
    reasons = []
    for line in log:
        if any(r.search(line) for r in IMAGE_FAILURES):
            if not any(image_entry(r) and r.fullmatch(line.strip()) for r in allowlist):
                reasons.append("missing image: " + line.strip())
        elif CHECKED_LINES.search(line) and not any(r.search(line) for r in allowlist):
            reasons.append("not allowlisted: " + line.strip())

    # each 'echo stockcheck view <name>' is followed by that view's viewpos line
    recorded, current = {}, None
    for line in log:
        if line.startswith("stockcheck view "):
            current = line[len("stockcheck view "):].strip()
        elif current:
            m = VIEWPOS.match(line.strip())
            if m:
                recorded[current] = tuple(int(v) for v in m.groups())
                current = None
    for name, x, y, z, yaw in views:
        if name not in recorded:
            reasons.append("view %s: no viewpos readback" % name)
        else:
            ex, ey, ez, _ = recorded[name]
            drift = math.dist((ex, ey, ez), (x, y, z))
            if drift > MAX_DRIFT:
                reasons.append("view %s: eye (%d %d %d) is %.0f units from the request, teleport ignored" %
                               (name, ex, ey, ez, drift))
    return reasons, recorded


def run_engine(args, renderer, mod_name, pk3, views, allowlist, tmp, out):
    """Runs one renderer in a fresh basepath holding only the stock paks and the pk3. Returns (ok, reasons, recorded)."""
    root = os.path.join(tmp, "run-" + renderer)
    make_root(root, args.q3base)
    os.makedirs(os.path.join(root, mod_name))
    shutil.copy(pk3, os.path.join(root, mod_name, mod_name + ".pk3"))
    home_mod = os.path.join(root, "home", mod_name)
    os.makedirs(home_mod)
    write_cfg(os.path.join(home_mod, "stockcheck.cfg"), args.map, views)

    engine = os.path.abspath(args.engine)
    cmd = [engine,
           "+set", "fs_basepath", root, "+set", "fs_homepath", os.path.join(root, "home"), "+set", "fs_game", mod_name,
           "+set", "cl_renderer", renderer, "+set", "developer", "1", "+set", "logfile", "2",
           "+set", "r_mode", "-1", "+set", "r_customwidth", "1280", "+set", "r_customheight", "720",
           "+set", "r_fullscreen", "0", "+set", "cg_draw2D", "0", "+set", "cg_drawGun", "0",
           "+set", "con_notifytime", "0", "+set", "com_introPlayed", "1", "+exec", "stockcheck.cfg"]
    reasons = []
    with open(os.path.join(out, renderer + "-stdout.log"), "w") as stdout:
        try:
            code = subprocess.run(cmd, cwd=os.path.dirname(engine), stdout=stdout, stderr=subprocess.STDOUT,
                                  timeout=RUN_TIMEOUT).returncode
            if code != 0:
                reasons.append("engine exited with %d" % code)
        except subprocess.TimeoutExpired:
            reasons.append("engine did not quit within %d s" % RUN_TIMEOUT)

    log_path = os.path.join(home_mod, "qconsole.log")
    log = []
    if os.path.isfile(log_path):
        shutil.copy(log_path, os.path.join(out, renderer + "-qconsole.log"))
        log = [COLOUR.sub("", line.rstrip("\r\n")) for line in open(log_path, errors="replace")]
    else:
        reasons.append("no qconsole.log")

    log_reasons, recorded = check_log(log, views, allowlist)
    reasons += log_reasons
    for name, *_ in views:
        shot = os.path.join(home_mod, "screenshots", "sc_%s.tga" % name)
        if os.path.isfile(shot):
            Image.open(shot).convert("RGB").save(os.path.join(out, "%s_%s.png" % (renderer, name)))
        else:
            reasons.append("view %s: no screenshot" % name)
    return not reasons, reasons, recorded


def main():
    args = parse_args()
    check_paths(args)
    args.mod = os.path.abspath(args.mod)
    mod_name = os.path.basename(args.mod.rstrip("/"))
    views = read_cameras(os.path.join(args.mod, "maps", args.map + ".cameras"))
    allowlist = read_allowlist(os.path.join(HERE, "allowlist.txt"))
    out = os.path.join(os.path.abspath(args.out), datetime.datetime.now().strftime("%Y%m%d-%H%M%S"))
    os.makedirs(out)

    header = ["engine: %s" % os.path.abspath(args.engine), "engine commit: %s" % engine_commit(args.engine),
              "map: %s/maps/%s.map" % (args.mod, args.map), "output: %s" % out]
    rows = []
    with tempfile.TemporaryDirectory(prefix="stockcheck-") as tmp:
        ok, reasons, compiled = compile_map(args, mod_name, tmp, out)
        rows.append(("compile", ok, reasons))
        if ok:
            pk3 = os.path.join(out, mod_name + ".pk3")
            pack(compiled, mod_name, args.map, pk3)
            views_txt = []
            for renderer in RENDERERS:
                ok, reasons, recorded = run_engine(args, renderer, mod_name, pk3, views, allowlist, tmp, out)
                rows.append((renderer, ok, reasons))
                for name, *_ in views:
                    if name in recorded:
                        views_txt.append("%s %s %d %d %d %d" % ((name, renderer) + recorded[name]))
            with open(os.path.join(out, "views.txt"), "w") as f:
                f.write("# view renderer eye_x eye_y eye_z yaw (the opengl1 lines are authoritative)\n")
                f.write("".join(line + "\n" for line in views_txt))
        else:
            rows += [(r, False, ["not run: compile failed"]) for r in RENDERERS]

    table = header + [""] + ["%-8s %s" % (name, "PASS" if ok else "FAIL") for name, ok, _ in rows]
    for name, ok, reasons in rows:
        table += ["  %s: %s" % (name, r) for r in reasons]
    text = "\n".join(table) + "\n"
    with open(os.path.join(out, "result.txt"), "w") as f:
        f.write(text)
    print(text, end="")
    return 0 if all(ok for _, ok, _ in rows) else 1


if __name__ == "__main__":
    sys.exit(main())
