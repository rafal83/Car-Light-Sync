Import("env")

# Disable the IDF component manager for environments that need additional stability.
# PlatformIO invokes internal ESP-IDF helper scripts that expect this variable.
if env.subst("$PIOENV") == "esp32s3":
    env["ENV"]["IDF_COMPONENT_MANAGER"] = "0"
