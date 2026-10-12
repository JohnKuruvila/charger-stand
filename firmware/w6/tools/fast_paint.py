# PlatformIO build step: compiles the per-pixel painters, the living faces (src/living_fx.cpp) and the Warp and Swarm
# charging screens (src/charging_fx.cpp), with -O2 instead of the framework's -Os. They paint every pixel of every
# frame, and -O2 makes them 20-35 % faster for a few KB more flash; the rest of the firmware stays small.
Import("env")


def o2(env, node):
    return env.Object(node, CCFLAGS=[f for f in env["CCFLAGS"] if f != "-Os"] + ["-O2"])


env.AddBuildMiddleware(o2, "*/living_fx.cpp")
env.AddBuildMiddleware(o2, "*/charging_fx.cpp")
