import os
import sys
from typing import TYPE_CHECKING

from methods import print_error, print_warning
from platform_methods import validate_arch

if TYPE_CHECKING:
    from SCons.Script.SConscript import SConsEnvironment


def get_name():
    return "Switch"


def _get_devkitpro():
    return os.environ.get("DEVKITPRO", "/opt/devkitpro")


def can_build():
    dkp = _get_devkitpro()

    if not os.path.isdir(dkp):
        print_warning("DEVKITPRO not found (set the DEVKITPRO environment variable), Switch disabled.")
        return False

    if not os.path.exists(os.path.join(dkp, "devkitA64", "bin", "aarch64-none-elf-gcc")):
        print_warning("devkitA64 not found, Switch disabled.")
        return False

    if not os.path.exists(os.path.join(dkp, "libnx", "include", "switch.h")):
        print_warning("libnx not found, Switch disabled.")
        return False

    return True


def get_opts():
    from SCons.Variables import BoolVariable

    return [
        BoolVariable("touch", "Enable touch events", True),
        BoolVariable("nxlink", "Redirect stdout/stderr to nxlink when available", True),
    ]


def get_doc_classes():
    return [
        "EditorExportPlatformSwitch",
    ]


def get_doc_path():
    return "doc_classes"


def get_flags():
    return {
        "arch": "arm64",
        "target": "template_release",
        # Horizon has no dynamic loader and no Vulkan loader usable from homebrew; use the
        # Compatibility (GLES3) renderer on top of devkitPro's Mesa/nouveau port.
        "vulkan": False,
        "opengl3": True,
        "library_type": "executable",
        # No JIT-able memory for homebrew applications.
        "builtin_pcre2_with_jit": False,
        # Not useful (or not buildable) on Horizon.
        "module_camera_enabled": False,
        "module_mono_enabled": False,
        "module_openxr_enabled": False,
        "module_raycast_enabled": False,
        "module_upnp_enabled": False,
        "module_webrtc_enabled": False,
        "module_lightmapper_rd_enabled": False,
        "sdl": False,
        "accesskit": False,
    }


def configure(env: "SConsEnvironment"):
    validate_arch(env["arch"], get_name(), ["arm64"])

    if env.editor_build:
        print_error("The Switch platform only supports export templates (target=template_debug/template_release).")
        sys.exit(255)

    dkp = _get_devkitpro()
    env["ENV"]["DEVKITPRO"] = dkp
    updated_path = os.pathsep.join(
        [
            os.path.join(dkp, "portlibs", "switch", "bin"),
            os.path.join(dkp, "devkitA64", "bin"),
            os.path.join(dkp, "tools", "bin"),
            os.environ["PATH"],
        ]
    )
    env["ENV"]["PATH"] = updated_path
    os.environ["PATH"] = updated_path  # Needed for subprocess calls (e.g. compiler version detection).

    ## Compiler configuration

    env["CC"] = "aarch64-none-elf-gcc"
    env["CXX"] = "aarch64-none-elf-g++"
    env["LINK"] = "aarch64-none-elf-g++"
    env["AS"] = "aarch64-none-elf-as"
    env["PROGSUFFIX"] = ".elf"

    if env["lto"] == "auto":
        env["lto"] = "none"

    if env["lto"] != "none":
        env.Append(CCFLAGS=["-flto"])
        env.Append(LINKFLAGS=["-flto=auto"])
        env["AR"] = "aarch64-none-elf-gcc-ar"
        env["RANLIB"] = "aarch64-none-elf-gcc-ranlib"
    else:
        env["AR"] = "aarch64-none-elf-ar"
        env["RANLIB"] = "aarch64-none-elf-ranlib"

    arch_flags = ["-march=armv8-a+crc+crypto", "-mtune=cortex-a57", "-mtp=soft", "-fPIE"]
    env.Prepend(CCFLAGS=arch_flags + ["-ffunction-sections", "-fdata-sections"])
    env.Prepend(LINKFLAGS=arch_flags + ["-specs={}/libnx/switch.specs".format(dkp)])
    env.Append(LINKFLAGS=["-Wl,--gc-sections"])

    env.Prepend(CPPPATH=["{}/portlibs/switch/include".format(dkp)])
    # libnx headers trigger a lot of warnings with Godot's settings, treat them as system headers.
    env.Prepend(CCFLAGS=["-isystem", "{}/libnx/include".format(dkp)])
    env.Append(LIBPATH=["{}/portlibs/switch/lib".format(dkp), "{}/libnx/lib".format(dkp)])

    env.Append(CCFLAGS=["-pipe"])

    ## Platform defines

    # compat/ provides the few POSIX headers newlib lacks (sys/un.h, ifaddrs.h, syslog.h, dlfcn.h).
    env.Prepend(CPPPATH=["#platform/switch", "#platform/switch/compat"])
    env.Append(
        CPPDEFINES=[
            "__SWITCH__",
            "HORIZON_ENABLED",
            "UNIX_ENABLED",
            # Horizon has no /dev/urandom, entropy comes from the kernel via getentropy() (see os_switch.cpp).
            "NO_URANDOM",
            # libnx's pthread implementation has no pthread_setname_np.
            "PTHREAD_NO_RENAME",
        ]
    )

    if env["touch"]:
        env.Append(CPPDEFINES=["TOUCH_ENABLED"])

    if env["nxlink"]:
        env.Append(CPPDEFINES=["SWITCH_NXLINK_ENABLED"])

    if env["opengl3"]:
        env.Append(CPPDEFINES=["GLES3_ENABLED"])
        # devkitPro's Mesa provides EGL + GLES 3.2 through static libraries.
        env.Append(LIBS=["EGL", "GLESv2", "glapi", "drm_nouveau"])

    env.Append(LIBS=["nx", "m"])
