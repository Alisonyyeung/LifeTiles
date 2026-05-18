Import("env")
import os
import shutil
import subprocess
import sys

libwebp_root = None

for lb in env.GetLibBuilders():
    if lb.name != "libwebp":
        continue
    root = lb.src_dir
    if os.path.basename(root) == "src":
        root = os.path.dirname(root)
    libwebp_root = root

    lb.env.Replace(
        SRC_FILTER=[
            "-<*>",
            "+<dec>",
            "+<dsp>",
            "+<utils>",
            "+<../sharpyuv>",
        ]
    )
    lb.env.Append(
        CPPDEFINES=["HAVE_CONFIG_H"],
        CPPPATH=[root, os.path.join(root, "src")],
    )

    cfg_src = env.subst("$PROJECT_DIR/lib/webp_embedded/config.h")
    cfg_dst = os.path.join(root, "src", "webp", "config.h")
    os.makedirs(os.path.dirname(cfg_dst), exist_ok=True)
    if os.path.normcase(cfg_src) != os.path.normcase(cfg_dst):
        shutil.copyfile(cfg_src, cfg_dst)

if libwebp_root:
    env.Append(CPPPATH=[os.path.join(libwebp_root, "src")])


def _prepare_fs_data(source, target, env):
    for name in ("prepare_images.py", "prepare_weather_backgrounds.py"):
        script = os.path.join(env["PROJECT_DIR"], "tools", name)
        if not os.path.isfile(script):
            continue
        print(f"Running {name}...")
        subprocess.run(
            [sys.executable, script],
            cwd=env["PROJECT_DIR"],
            check=False,
        )


env.AddPreAction("buildfs", _prepare_fs_data)
env.AddPreAction("uploadfs", _prepare_fs_data)
