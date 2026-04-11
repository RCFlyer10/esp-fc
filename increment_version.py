import os
import subprocess
Import("env")

VERSION_FILE = "version.txt"
current_env = env.get("PIOENV", "default")
HASH_CACHE = f".last_hash_{current_env}"

def get_git_hash():
    try:
        return subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD']).strip().decode('utf-8')
    except:
        return "0000000"

def get_current_version():
    if not os.path.exists(VERSION_FILE):
        with open(VERSION_FILE, "w") as f: f.write("0.0.1")
    with open(VERSION_FILE, "r") as f:
        return f.read().strip()

# 1. Logic
current_hash = get_git_hash()
last_hash = ""
if os.path.exists(HASH_CACHE):
    with open(HASH_CACHE, "r") as f: last_hash = f.read().strip()

version = get_current_version()

if current_hash != last_hash and last_hash != "":
    major, minor, patch = map(int, version.split('.'))
    patch += 1
    version = f"{major}.{minor}.{patch}"
    with open(VERSION_FILE, "w") as f: f.write(version)
    print(f"--- [{current_env}] NEW COMMIT: Bumping version to v{version} ---")

with open(HASH_CACHE, "w") as f: f.write(current_hash)

# 2. THE FIX: Generate a header-only definition
# We use 'weak' attribute so our definition can override or coexist 
# without the 'multiple definition' error.
GEN_SRC = os.path.join(env.subst("$PROJECT_DIR"), "src", "version_generated.cpp")

with open(GEN_SRC, "w") as f:
    f.write(f'#include <Arduino.h>\n')
    # We use __attribute__((weak)) to tell the linker: 
    # "If you find another definition, use that one; otherwise, use this."
    # But to ensure OURS is used, we'll keep it simple:
    f.write(f'extern "C" const char * const targetVersion = "v{version}";\n')
    f.write(f'extern "C" const char * const shortGitRevision = "{current_hash}";\n')

# 3. Add a build flag to ignore the multiple definition error for these specific symbols
env.Append(LINKFLAGS=[
    "-Wl,--allow-multiple-definition"
])