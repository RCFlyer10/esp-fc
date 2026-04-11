# type: ignore
try:
    Import("env")
except NameError:
    # This allows the script to be "inspected" by IDEs without crashing
    env = None

import os
import subprocess
import json
Import("env")

# Path to your library.json
LIB_JSON = os.path.join(env.subst("$PROJECT_DIR"), "lib", "Espfc", "library.json")
current_env = env.get("PIOENV", "default")
HASH_CACHE = f".last_hash_{current_env}"

def get_git_hash():
    try:
        return subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).strip().decode('utf-8')
    except Exception:
        return "0000000"

def get_version_from_json():
    if not os.path.exists(LIB_JSON):
        return "0.0.1"
    with open(LIB_JSON, "r") as f:
        data = json.load(f)
        return data.get("version", "0.0.1")

def update_version_in_json(new_version):
    if not os.path.exists(LIB_JSON):
        return
    with open(LIB_JSON, "r") as f:
        data = json.load(f)
    
    data["version"] = new_version
    
    with open(LIB_JSON, "w") as f:
        json.dump(data, f, indent=2)

# 1. Gather Data
current_hash = get_git_hash()
last_hash = ""
if os.path.exists(HASH_CACHE):
    with open(HASH_CACHE, "r") as f:
        last_hash = f.read().strip()

version = get_version_from_json()

# 2. Logic: Increment if hash changed, or initialize if fresh
if current_hash != last_hash:
    if last_hash != "":
        try:
            parts = version.split('.')
            # Handle versions like "0.2.0"
            major, minor, patch = map(int, parts)
            patch += 1
            version = f"{major}.{minor}.{patch}"
            update_version_in_json(version)
            print(f"--- [{current_env}] library.json VERSION BUMPED TO {version} ---")
        except Exception as e:
            print(f"--- Error bumping version: {e} ---")
    else:
        print(f"--- [{current_env}] Initializing version cache with {current_hash} ---")

# 3. Update the cache
with open(HASH_CACHE, "w") as f:
    f.write(current_hash)

# 4. Generate Source File
GEN_SRC = os.path.join(env.subst("$PROJECT_DIR"), "src", "version_generated.cpp")

with open(GEN_SRC, "w") as f:
    f.write('#include <Arduino.h>\n')
    # Removed __attribute__((weak)) to force the linker to use this definition
    f.write('extern "C" const char * const targetVersion = "v' + version + '";\n')
    f.write('extern "C" const char * const shortGitRevision = "' + current_hash + '";\n')

# FORCE RECOMPILE
if os.path.exists(GEN_SRC):
    os.utime(GEN_SRC, None)

env.Append(LINKFLAGS=["-Wl,--allow-multiple-definition"])
