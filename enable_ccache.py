from SCons.Script import DefaultEnvironment
import os

env = DefaultEnvironment()

# Inject the proxy directory to the VERY FRONT of PlatformIO's execution path
proxy_path = os.getenv("HOME") + "/.pio_ccache_links"

if os.path.exists(proxy_path):
    # Prepend to the internal OS environment dictionary used by SCons shells
    env.PrependENVPath("PATH", proxy_path)
    print(f"\n[CCACHE SUCCESS] Prioritizing proxy path pipeline: {proxy_path}\n")
