param(
    [string]$DllPath = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($DllPath)) {
    $candidate = Join-Path $PSScriptRoot "PepWicCodec.dll"
    if (-not (Test-Path -LiteralPath $candidate)) {
        $candidate = Join-Path $PSScriptRoot "..\build\Release\PepWicCodec.dll"
    }
    if (-not (Test-Path -LiteralPath $candidate)) {
        $candidate = Join-Path $PSScriptRoot "..\out\build\x64-Debug\PepWicCodec.dll"
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
        throw "Elevated registration failed with exit code $($process.ExitCode)."
    }
}
else {
    $process = Start-Process -FilePath "$env:SystemRoot\System32\regsvr32.exe" -ArgumentList @("/s", $resolved.Path) -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "regsvr32 failed with exit code $($process.ExitCode) for $($resolved.Path)"
    }
}

$decoderKey = "Registry::HKEY_LOCAL_MACHINE\Software\Classes\CLSID\{f5c347b6-8502-4fa5-8b35-875bbd966c63}"
$mergedDecoderKey = "Registry::HKEY_CLASSES_ROOT\CLSID\{f5c347b6-8502-4fa5-8b35-875bbd966c63}"
$decoderCategoryInstanceKey = "Registry::HKEY_LOCAL_MACHINE\Software\Classes\CLSID\{7ed96837-96f0-4812-b211-f13c24117ed3}\Instance\{f5c347b6-8502-4fa5-8b35-875bbd966c63}"
$extensionKey = "Registry::HKEY_LOCAL_MACHINE\Software\Classes\.pep"
$inprocKey = Join-Path $decoderKey "InprocServer32"

if (-not (Test-Path -LiteralPath $decoderKey)) {
    throw "Decoder CLSID was not written to HKLM. Check that the DLL exports DllRegisterServer and PowerShell is elevated."
}

if (-not (Test-Path -LiteralPath $decoderCategoryInstanceKey)) {
    throw "Decoder was registered for COM, but not added to the WIC decoder category table."
}

$merged = Get-ItemProperty -LiteralPath $mergedDecoderKey
if ($merged.Flags -ne 6) {
    throw "HKCR still resolves the decoder to stale metadata (Flags=$($merged.Flags)); remove the old HKCU registration and run this script again."
}

if (-not (Test-Path -LiteralPath $extensionKey)) {
    throw ".pep image file association was not written to HKLM."
}

$inproc = Get-ItemProperty -LiteralPath $inprocKey
Write-Host "Registered PEP WIC codec machine-wide: $($resolved.Path)"
Write-Host "COM server path: $($inproc.'(default)')"
Write-Host "If Explorer was already open, restart Explorer or sign out/in before checking thumbnails."
