# Platform support

Linux, macOS and Windows are required product targets. The shared kernel and
Python clients must use the same project format and simulation semantics.
Platform packaging and native file dialogs must not change simulation behavior.

| Target | Native compiler path | Status |
|---|---|---|
| Linux | GCC or Clang, C99 shared library | Built and tested in the development environment |
| macOS | Apple Clang, C99 dynamic library | Build path and CI configured; execution pending CI |
| Windows | MSVC C11 mode for the C99 subset, or MinGW GCC | DLL exports, build path and CI configured; execution pending CI |

No platform is called verified until its tests run. ARM64/x86-64 packaging,
installers, code signing and release artifacts require later release CI.

## Build on any target

Install Python 3.10+ and a supported C compiler. On Windows, use the Visual Studio
Developer Command Prompt with the C/C++ build tools installed. From the repo:

```sh
python scripts/build.py --test
```

Use `python3` where the Python executable has that name. `--cc` and `--cflags`
override the compiler and flags; `CC` and `CFLAGS` are also honored. GCC/Clang
build in C99 mode; MSVC uses its supported C11 mode. Warnings are errors.

Run clients without platform-specific shell environment syntax:

```sh
python scripts/embsim.py run examples/button-led.json --until-ns 20000000
python scripts/embsim.py serve examples/button-led.json
```

The GUI currently uses a local browser and a loopback server, giving all three
platforms the same interface without requiring an early desktop wrapper choice.
The development server is not a production distribution mechanism. Desktop
integration, offline packaging, high-DPI and platform assistive technology checks
are required before a desktop release.
