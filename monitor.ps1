$env:IDF_PATH = "C:/Users/joe06/esp/frameworks/esp-idf-v5.5.3"
$env:PATH = "C:/Users/joe06/esp/tools/riscv32-esp-elf/esp-14.2.0_20251107/riscv32-esp-elf/bin;C:/Users/joe06/esp/tools/cmake/3.30.2/bin;C:/Users/joe06/esp/tools/ninja/1.12.1;C:/Users/joe06/esp/tools/ccache/4.12.1/ccache-4.12.1-windows-x86_64;C:/Users/joe06/esp/frameworks/esp-idf-v5.5.3/tools;C:/Users/joe06/esp/python_env/idf5.5_py3.11_env/Scripts;$env:PATH"

$msysVars = @("MSYSTEM","MSYSTEM_PREFIX","MSYSTEM_CARCH","MSYSTEM_CHOST","MSYSCON",
              "MINGW_PREFIX","MINGW_CHOST","MINGW_PACKAGE_PREFIX")
foreach ($v in $msysVars) { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }

Set-Location "$PSScriptRoot/firmware"
python $env:IDF_PATH/tools/idf.py -p COM11 monitor
