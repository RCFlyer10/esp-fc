import os
import subprocess
Import("env")

VERSION_FILE = "version.txt"
current_env = env.get("PIOENV", "default")
HASH_CACHE = f".last_hash_{current_env}"

def get_git_hash():
    try:
        return subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).strip().decode('utf-8')
    except Exception:
        return "0000000"

def get_current_version():
    if not os.path.exists(VERSION_FILE):
        with open(VERSION_FILE, "w") as f: f.write("0.0.1")
        return "0.0.1"
    with open(VERSION_FILE, "r") as f:
        return f.read().strip()

# 1. Gather Data
current_hash = get_git_hash()
last_hash = ""
if os.path.exists(HASH_CACHE):
    with open(HASH_CACHE, "r") as f:
        last_hash = f.read().strip()

version = get_current_version()

# 2. Logic: Increment only if hash changed
if current_hash != last_hash and last_hash != "":
    try:
        major, minor, patch = map(int, version.split('.'))
        patch += 1
        version = f"{major}.{minor}.{patch}"
        with open(VERSION_FILE, "w") as f:
            f.write(version)
        print(f"--- [{current_env}] VERSION BUMPED TO {version} ---")
    except:
        pass

# 3. Update the cache
with open(HASH_CACHE, "w") as f:
    f.write(current_hash)

# 4. Generate Source File
GEN_SRC = os.path.join(env.subst("$PROJECT_DIR"), "src", "version_generated.cpp")

# Create/Update the file
with open(GEN_SRC, "w") as f:
    f.write('#include <Arduino.h>\n')
    f.write(f'extern "C" const char * const targetVersion = "v{version}";\n')
    f.write(f'extern "C" const char * const shortGitRevision = "{current_hash}";\n')

# FORCE RECOMPILE: Tell PlatformIO that this specific file is always out of date
# This ensures the compiler actually looks at the new strings we just wrote
env.Execute(f'touch "{GEN_SRC}"') 

env.Append(LINKFLAGS=["-Wl,--allow-multiple-definition"])