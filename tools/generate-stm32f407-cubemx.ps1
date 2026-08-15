[CmdletBinding()]
param(
    [string]$CubeMx = $env:STM32CUBEMX_EXE,
    [string]$FirmwarePackage = $env:STM32CUBE_F4_ROOT
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($CubeMx)) {
    $cubeCommand = Get-Command STM32CubeMX -ErrorAction SilentlyContinue
    if ($null -ne $cubeCommand) {
        $CubeMx = $cubeCommand.Source
    }
}
if ([string]::IsNullOrWhiteSpace($FirmwarePackage)) {
    $FirmwarePackage = Join-Path $HOME `
        'STM32Cube\Repository\STM32Cube_FW_F4_V1.28.3'
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$iocPath = Join-Path $projectRoot 'firmware\platform\stm32f407\cubemx\stm32f407_gateway.ioc'
$outputPath = Join-Path $projectRoot 'firmware\platform\stm32f407\cubemx\generated'
$commandFile = Join-Path ([System.IO.Path]::GetTempPath()) 'stm32f407_gateway_cubemx.txt'
$java = Join-Path (Split-Path -Parent $CubeMx) 'jre\bin\java.exe'

if (-not (Test-Path -LiteralPath $CubeMx)) {
    throw "STM32CubeMX not found: $CubeMx"
}
if (-not (Test-Path -LiteralPath $java)) {
    throw "STM32CubeMX Java runtime not found: $java"
}
if (-not (Test-Path -LiteralPath $FirmwarePackage)) {
    throw "STM32CubeF4 package not found: $FirmwarePackage"
}
if (-not (Test-Path -LiteralPath $iocPath)) {
    throw "CubeMX project not found: $iocPath"
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

$commands = @(
    "config load `"$iocPath`""
    "project path `"$outputPath`""
    'project name stm32f407_gateway'
    'project toolchain CMake'
    "project setCustomFWPath `"$FirmwarePackage`""
    'project couplefilesbyip 1'
    'project generate'
    'exit'
)

[System.IO.File]::WriteAllLines(
    $commandFile,
    $commands,
    [System.Text.UTF8Encoding]::new($false))

try {
    & $java -jar $CubeMx -q $commandFile
    if ($LASTEXITCODE -ne 0) {
        throw "STM32CubeMX exited with code $LASTEXITCODE"
    }
}
finally {
    Remove-Item -LiteralPath $commandFile -Force -ErrorAction SilentlyContinue
}

Write-Host "CubeMX sources generated at: $outputPath"
