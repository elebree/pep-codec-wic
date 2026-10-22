param(
    [string]$DllPath = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($DllPath)) {
    $candidate = Join-Path $PSScriptRoot "PepWicCodec.dll"
    if (-not (Test-Path -LiteralPath $candidate)) {
        $candidate = Join-Path $PSScriptRoot "..\wic\build\Release\PepWicCodec.dll"
    }
    if (-not (Test-Path -LiteralPath $candidate)) {
        $candidate = Join-Path $PSScriptRoot "..\wic\out\build\x64-Debug\PepWicCodec.dll"
    }
    $DllPath = $candidate
}

$resolved = Resolve-Path -LiteralPath $DllPath

$userDecoderKey = "Registry::HKEY_CURRENT_USER\Software\Classes\CLSID\{f5c347b6-8502-4fa5-8b35-875bbd966c63}"
$userExtensionKey = "Registry::HKEY_CURRENT_USER\Software\Classes\.pep"
$userProgIdKey = "Registry::HKEY_CURRENT_USER\Software\Classes\pepfile"
Remove-Item -LiteralPath $userDecoderKey -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $userExtensionKey -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $userProgIdKey -Recurse -Force -ErrorAction SilentlyContinue

$principal = [Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $hostPath = (Get-Process -Id $PID).Path
    $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -DllPath `"$($resolved.Path)`""
    $process = Start-Process -FilePath $hostPath -ArgumentList $arguments -Verb RunAs -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "Elevated unregistration failed with exit code $($process.ExitCode)."
    }
}
else {
    $process = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList @("/u", "/s", $resolved.Path) -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "regsvr32 unregister failed with exit code $($process.ExitCode) for $($resolved.Path)"
    }
}

Write-Host "Unregistered PEP WIC codec machine-wide: $($resolved.Path)"
