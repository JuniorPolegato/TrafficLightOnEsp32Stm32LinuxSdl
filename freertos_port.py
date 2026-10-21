Import("env")

FR = "$PROJECT_DIR/lib/FreeRTOS-Kernel"
platform = env.PioPlatform().name

if platform == "native":
    port, heap = FR + "/portable/ThirdParty/GCC/Posix", "heap_3.c"
    port_filter = ["+<*.c>", "+<utils/*.c>"]
    env.Append(LIBS=["pthread"])
elif platform == "ststm32":
    port, heap = FR + "/portable/GCC/ARM_CM7/r0p1", "heap_4.c"
    port_filter = ["+<*.c>"]
    fpu = ["-mfpu=fpv5-d16", "-mfloat-abi=hard"]
    env.Append(CCFLAGS=fpu, ASFLAGS=fpu, LINKFLAGS=fpu)
else:
    Return()                       # ESP-IDF has its own FreeRTOS

env.Append(CPPPATH=[FR + "/include", port, port + "/utils"])
env.BuildSources("$BUILD_DIR/freertos_port", port, src_filter=port_filter)
env.BuildSources("$BUILD_DIR/freertos_heap", FR + "/portable/MemMang",
                 src_filter=["-<*>", f"+<{heap}>"])

# native
# {
#   "name": "FreeRTOS-Kernel",
#   "version": "11.0.0",
#   "build": {
#     "srcDir": ".",
#     "includeDir": "include",
#     "srcFilter": [
#       "-<*>", "+<*.c>",
#       "+<portable/MemMang/heap_3.c>",
#       "+<portable/ThirdParty/GCC/Posix/*.c>",
#       "+<portable/ThirdParty/GCC/Posix/utils/*.c>"
#     ]
#   }
# }

# stm32
# {
#   "name": "FreeRTOS-Kernel",
#   "build": {
#     "srcFilter": [
#       "+<*.c>",
#       "+<portable/GCC/ARM_CM7/r0p1/*.c>",
#       "+<portable/MemMang/heap_4.c>"
#     ],
#     "flags": [
#       "-Iinclude",
#       "-Iportable/GCC/ARM_CM7/r0p1"
#     ]
#   }
# }
