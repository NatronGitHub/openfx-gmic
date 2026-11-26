# Load <dir>\GMIC.ofx (LoadLibraryW) in this PowerShell process, and check that the vcomp140.dll
# that it loads is <dir>\vcomp140.dll, the bundle's (listed in GMIC.ofx's embedded manifest), not
# System32's copy. One process per GMIC.ofx: pwsh -NoProfile -File check_vcomp140_loaded.ps1 <dir>
param([Parameter(Mandatory = $true)][string]$dir)
$ErrorActionPreference = 'Stop'
$dir = (Resolve-Path $dir).Path
(Get-Item "$dir\vcomp140.dll").VersionInfo | Format-List FileDescription,FileVersion,ProductVersion
Add-Type -Namespace W -Name K -MemberDefinition '[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] public static extern System.IntPtr LoadLibraryW(string f);'
if ([W.K]::LoadLibraryW("$dir\GMIC.ofx") -eq [System.IntPtr]::Zero) { throw "LoadLibrary failed: $([Runtime.InteropServices.Marshal]::GetLastWin32Error())" }
$vcomp = (Get-Process -Id $PID).Modules | Where-Object ModuleName -ieq 'vcomp140.dll'
Write-Host "$([System.Runtime.InteropServices.RuntimeInformation]::ProcessArchitecture) process, $dir\GMIC.ofx: vcomp140.dll loaded from $($vcomp.FileName)"
if ($vcomp.FileName -ine "$dir\vcomp140.dll") { throw "vcomp140.dll is not the bundle's" }
