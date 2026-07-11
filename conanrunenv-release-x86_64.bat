@echo off
chcp 65001 > nul
@echo off


setlocal
echo @echo off > "%~dp0/deactivate_conanrunenv-release-x86_64.bat"
echo echo Restoring environment for conanrunenv-release-x86_64.bat >> "%~dp0/deactivate_conanrunenv-release-x86_64.bat"
for %%v in (PATH) do (
    set foundenvvar=
    for /f "delims== tokens=1,2" %%a in ('set') do (
        if /I "%%a" == "%%v" (
            echo set "%%a=%%b">> "%~dp0/deactivate_conanrunenv-release-x86_64.bat"
            set foundenvvar=1
        )
    )
    if not defined foundenvvar (
        echo set %%v=>> "%~dp0/deactivate_conanrunenv-release-x86_64.bat"
    )
)
endlocal




if defined PATH (
    set "PATH=C:\Users\Administrator\.conan2\p\boost3470b08540450\p\bin;C:\Users\Administrator\.conan2\p\b\miniz54a704a762440\p\bin;C:\Users\Administrator\.conan2\p\libicfd7bc243aebf9\p\bin;C:\Users\Administrator\.conan2\p\zstdf533cf5aa3eb0\p\bin;C:\Users\Administrator\.conan2\p\b\sdl_i625c31506b1fb\p\bin;C:\Users\Administrator\.conan2\p\xz_utc330954994000\p\bin;C:\Users\Administrator\.conan2\p\libjp9100cdb58e0c1\p\bin;C:\Users\Administrator\.conan2\p\sdl_t99fc4f4a72895\p\bin;C:\Users\Administrator\.conan2\p\sdl741b3a9c9bbd2\p\bin;C:\Users\Administrator\.conan2\p\onetb500b8f27a4304\p\bin;C:\Users\Administrator\.conan2\p\fmtb023e645abce4\p\bin;C:\Users\Administrator\.conan2\p\b\freet065238355bf7b\p\bin;C:\Users\Administrator\.conan2\p\libpn53f6a88ebd035\p\bin;C:\Users\Administrator\.conan2\p\zlibe7061fb113fae\p\bin;C:\Users\Administrator\.conan2\p\bzip2217ece51953bc\p\bin;%PATH%"
) else (
    set "PATH=C:\Users\Administrator\.conan2\p\boost3470b08540450\p\bin;C:\Users\Administrator\.conan2\p\b\miniz54a704a762440\p\bin;C:\Users\Administrator\.conan2\p\libicfd7bc243aebf9\p\bin;C:\Users\Administrator\.conan2\p\zstdf533cf5aa3eb0\p\bin;C:\Users\Administrator\.conan2\p\b\sdl_i625c31506b1fb\p\bin;C:\Users\Administrator\.conan2\p\xz_utc330954994000\p\bin;C:\Users\Administrator\.conan2\p\libjp9100cdb58e0c1\p\bin;C:\Users\Administrator\.conan2\p\sdl_t99fc4f4a72895\p\bin;C:\Users\Administrator\.conan2\p\sdl741b3a9c9bbd2\p\bin;C:\Users\Administrator\.conan2\p\onetb500b8f27a4304\p\bin;C:\Users\Administrator\.conan2\p\fmtb023e645abce4\p\bin;C:\Users\Administrator\.conan2\p\b\freet065238355bf7b\p\bin;C:\Users\Administrator\.conan2\p\libpn53f6a88ebd035\p\bin;C:\Users\Administrator\.conan2\p\zlibe7061fb113fae\p\bin;C:\Users\Administrator\.conan2\p\bzip2217ece51953bc\p\bin"
)