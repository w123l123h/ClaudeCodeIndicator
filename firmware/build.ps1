$env:IDF_PATH = "C:/Users/joe06/esp/frameworks/esp-idf-v5.5.3"
$env:PATH = "C:/Users/joe06/esp/python_env/idf5.5_py3.11_env/Scripts;C:/Users/joe06/esp/tools/riscv32-esp-elf/esp-14.2.0_20251107/riscv32-esp-elf/bin;C:/Users/joe06/esp/tools/cmake/3.30.2/bin;C:/Users/joe06/esp/tools/ninja/1.12.1;C:/Users/joe06/esp/tools/ccache/4.12.1/ccache-4.12.1-windows-x86_64;C:/Users/joe06/esp/frameworks/esp-idf-v5.5.3/tools;$env:PATH"

Set-Location "$PSScriptRoot/build"
$ninja = "C:/Users/joe06/esp/tools/ninja/1.12.1/ninja.exe"

Write-Host "Starting build..."
& $ninja all
