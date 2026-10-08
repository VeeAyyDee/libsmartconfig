# SPDX-License-Identifier: 0BSD
param(
    [Parameter(Mandatory=$true)][ValidateSet('v1','v2','v2-aes','airkiss','airkiss-aes')][string]$Mode,
    [Parameter(Mandatory=$true)][string]$IdfPath,
    [Parameter(Mandatory=$true)][string]$PythonEnvironment
)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$env:IDF_PYTHON_ENV_PATH=$PythonEnvironment
$env:Path="$PythonEnvironment\Scripts;$env:Path"
. (Join-Path $IdfPath 'export.ps1')
$env:IDF_COMPONENT_MANAGER='0'
$config=Join-Path $repo 'examples/provision/sdkconfig'
if (!(Test-Path -LiteralPath $config)) { throw 'First build examples/provision for esp32s3 to create sdkconfig.' }
$lines=Get-Content -LiteralPath $config | Where-Object { $_ -notmatch '^(# )?CONFIG_SC_EXAMPLE_(PROTOCOL_|V2_KEY|AIRKISS_KEY)' }
$protocol=if($Mode -eq 'v1'){'V1'}elseif($Mode.StartsWith('airkiss')){'AIRKISS'}else{'V2'}
$lines += "CONFIG_SC_EXAMPLE_PROTOCOL_$protocol=y"
$labKey=if($Mode.EndsWith('-aes')){'000102030405060708090a0b0c0d0e0f'}else{''}
if($protocol -eq 'V2') { $lines += 'CONFIG_SC_EXAMPLE_V2_KEY="'+$labKey+'"' }
if($protocol -eq 'AIRKISS') { $lines += 'CONFIG_SC_EXAMPLE_AIRKISS_KEY="'+$labKey+'"' }
Set-Content -LiteralPath $config -Value $lines
$out=Join-Path $repo 'android/validation/firmware'
New-Item -ItemType Directory -Force $out | Out-Null
idf.py -C (Join-Path $repo 'examples/provision') build *> (Join-Path $out "$Mode-build.txt")
if($LASTEXITCODE -ne 0) { Get-Content (Join-Path $out "$Mode-build.txt") -Tail 35; throw 'Receiver build failed' }
Copy-Item -LiteralPath (Join-Path $repo 'examples/provision/build/cleanroom_provision_example.bin') -Destination (Join-Path $out "$Mode.bin")
Write-Output "Built $Mode"
