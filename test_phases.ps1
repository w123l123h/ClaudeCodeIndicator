$env:IDF_PATH = "C:/Users/joe06/esp/frameworks/esp-idf-v5.5.3"
$python = "C:/Users/joe06/esp/python_env/idf5.5_py3.11_env/Scripts/python.exe"
$env:PATH = "C:/Users/joe06/esp/tools/riscv32-esp-elf/esp-14.2.0_20251107/riscv32-esp-elf/bin;C:/Users/joe06/esp/tools/cmake/3.30.2/bin;C:/Users/joe06/esp/tools/ninja/1.12.1;C:/Users/joe06/esp/tools/ccache/4.12.1/ccache-4.12.1-windows-x86_64;C:/Users/joe06/esp/frameworks/esp-idf-v5.5.3/tools;C:/Users/joe06/esp/python_env/idf5.5_py3.11_env/Scripts;$env:PATH"

$msysVars = @("MSYSTEM","MSYSTEM_PREFIX","MSYSTEM_CARCH","MSYSTEM_CHOST","MSYSCON","MINGW_PREFIX","MINGW_CHOST","MINGW_PACKAGE_PREFIX")
foreach ($v in $msysVars) { Remove-Item "Env:$v" -ErrorAction SilentlyContinue }

$monitorLog = "C:/tmp/esp32_test.log"
$monitorScript = "C:/tmp/run_monitor.ps1"

# Write monitor runner
@"
`$env:IDF_PATH = "$env:IDF_PATH"
`$env:PATH = "$env:PATH"
`$msysVars = @("MSYSTEM","MSYSTEM_PREFIX","MSYSTEM_CARCH","MSYSTEM_CHOST","MSYSCON","MINGW_PREFIX","MINGW_CHOST","MINGW_PACKAGE_PREFIX")
foreach (`$v in `$msysVars) { Remove-Item "Env:`$v" -ErrorAction SilentlyContinue }
& "$python" "$env:IDF_PATH/tools/idf_monitor.py" -p COM11 -b 115200 --toolchain-prefix riscv32-esp-elf- --target esp32c3 "D:/develop/projects/esp32Projects/ClaudeCodeIndicator/firmware/build/claude_code_indicator.elf" 2>&1 | Out-File -FilePath "$monitorLog" -Encoding UTF8
"@ | Out-File -FilePath $monitorScript -Encoding UTF8

Write-Host "Step 1: Starting monitor..."
$monitorJob = Start-Job -FilePath $monitorScript
Start-Sleep -Seconds 5

Write-Host "Step 2: Running main.py to connect..."
Set-Location "D:/develop/projects/esp32Projects/ClaudeCodeIndicator/desktop-relay"
$mainJob = Start-Job -ScriptBlock { & python main.py 2>&1 }
Start-Sleep -Seconds 15
Stop-Job $mainJob -ErrorAction SilentlyContinue
Remove-Job $mainJob -ErrorAction SilentlyContinue
Write-Host "Step 3: main.py killed (simulating disconnect)"
Write-Host "Step 4: Waiting 60s for phase transitions..."
Start-Sleep -Seconds 60

Write-Host "Step 5: Stopping monitor..."
Stop-Job $monitorJob -ErrorAction SilentlyContinue
Remove-Job $monitorJob -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2

Write-Host "Step 6: Results:"
$log = Get-Content $monitorLog -ErrorAction SilentlyContinue
$log | Select-String -Pattern "phase|SLEEP|AWAKE|PM lock|Connected|Disconnected|power|Light sleep|light sleep|Advertising started" | ForEach-Object { $_.Line }
