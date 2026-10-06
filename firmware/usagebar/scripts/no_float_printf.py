# PlatformIO post-script: drop the "-u _printf_float -u _scanf_float" link flags the
# ESP8266 Arduino builder adds by default. Nothing here formats floats with printf/scanf
# (ArduinoJson has its own float writer), and the float printf code costs ~16 KB of flash.
Import("env")

flags, out, skip = env["LINKFLAGS"], [], False
for i, f in enumerate(flags):
    if skip:
        skip = False
        continue
    if f == "-u" and i + 1 < len(flags) and flags[i + 1] in ("_printf_float", "_scanf_float"):
        skip = True
        continue
    out.append(f)
env.Replace(LINKFLAGS=out)
