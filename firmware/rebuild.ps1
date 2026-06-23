$env:IDF_PATH = "C:/Users/joe06/esp/frameworks/esp-idf-v5.5.3"
$env:PATH = "C:/Users/joe06/esp/python_env/idf5.5_py3.11_env/Scripts;C:/Users/joe06/esp/tools/riscv32-esp-elf/esp-14.2.0_20251107/riscv32-esp-elf/bin;C:/Users/joe06/esp/tools/cmake/3.30.2/bin;C:/Users/joe06/esp/tools/ninja/1.12.1;C:/Users/joe06/esp/tools/ccache/4.12.1/ccache-4.12.1-windows-x86_64;C:/Users/joe06/esp/frameworks/esp-idf-v5.5.3/tools;$env:PATH"

$msysVars = @("MSYSTEM","MSYSTEM_PREFIX","MSYSTEM_CARCH","MSYSTEM_CHOST","MSYSCON",
              "MINGW_PREFIX","MINGW_CHOST","MINGW_PACKAGE_PREFIX")
foreach ($v in $msysVars) { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }

$ErrorActionPreference = "Continue"

$cmake     = "C:/Users/joe06/esp/tools/cmake/3.30.2/bin/cmake.exe"
$ninja     = "C:/Users/joe06/esp/tools/ninja/1.12.1/ninja.exe"
$srcDir    = "$PSScriptRoot"
$buildDir  = "$PSScriptRoot/build"
$toolchain = "C:/Users/joe06/esp/frameworks/esp-idf-v5.5.3/tools/cmake/toolchain-esp32c3.cmake"

if (-not (Test-Path $buildDir)) { New-Item -ItemType Directory -Path $buildDir -Force | Out-Null }

Write-Host "Configuring CMake for esp32c3..."
Push-Location $buildDir
& $cmake $srcDir -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$toolchain" "-DIDF_TARGET=esp32c3" "-DCCACHE_ENABLE=ON" "-DCMAKE_BUILD_TYPE=Debug" 2>&1
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Host "CMake configuration failed!" -ForegroundColor Red
    exit 1
}

Write-Host "Building with Ninja..."
& $ninja 2>&1
if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}

Pop-Location
Write-Host "Build succeeded!" -ForegroundColor Green
