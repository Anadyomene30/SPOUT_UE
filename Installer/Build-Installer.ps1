# Copyright (c) 2026 SpoutUE contributors. MIT licensed.
#
# Builds SpoutUE-Setup-<version>.exe: packages the plugin for each installed
# engine with RunUAT BuildPlugin, zips each package, and compiles the installer
# around them with the C# compiler that ships with Windows (.NET Framework 4).
#
#   .\Build-Installer.ps1                    package every installed UE 5.x, then build
#   .\Build-Installer.ps1 -Engines 5.8       only these versions
#   .\Build-Installer.ps1 -SkipPackage       reuse packages already in -PackageRoot
#
# An engine whose BuildPlugin fails is left out with a warning rather than failing
# the whole build; the installer then shows that version as having no build.

param(
	[string[]] $Engines,
	[string] $PackageRoot = (Join-Path $env:LOCALAPPDATA "SpoutUEBuild"),
	[string] $OutDir = (Join-Path $PSScriptRoot "out"),
	[switch] $SkipPackage
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path $PSScriptRoot -Parent
$PluginFile = Join-Path $RepoRoot "SpoutUE.uplugin"
$Version = (Get-Content $PluginFile -Raw | ConvertFrom-Json).VersionName

# ------------------------------------------------------------------ engines

function Get-InstalledEngines {
	$manifest = Join-Path $env:ProgramData "Epic\UnrealEngineLauncher\LauncherInstalled.dat"
	$roots = @()
	if (Test-Path $manifest) {
		$roots += (Get-Content $manifest -Raw | ConvertFrom-Json).InstallationList |
			Where-Object { $_.AppName -like "UE_5.*" } | ForEach-Object { $_.InstallLocation }
	}
	$roots += Get-ChildItem (Join-Path $env:ProgramFiles "Epic Games") -Directory -Filter "UE_5.*" -ErrorAction SilentlyContinue |
		ForEach-Object { $_.FullName }

	foreach ($root in ($roots | Sort-Object -Unique)) {
		$buildVersion = Join-Path $root "Engine\Build\Build.version"
		if (Test-Path $buildVersion) {
			$v = Get-Content $buildVersion -Raw | ConvertFrom-Json
			[pscustomobject]@{ Version = "$($v.MajorVersion).$($v.MinorVersion)"; Root = $root }
		}
	}
}

$installed = @(Get-InstalledEngines)
if ($Engines) {
	$installed = @($installed | Where-Object { $Engines -contains $_.Version })
}
if ($installed.Count -eq 0) {
	throw "No Unreal Engine 5 install found to build against."
}

New-Item -ItemType Directory -Force $OutDir, $PackageRoot | Out-Null
$Work = Join-Path $OutDir "obj"
if (Test-Path $Work) { Remove-Item $Work -Recurse -Force }
New-Item -ItemType Directory -Force $Work | Out-Null

# ------------------------------------------------------------------ packages

Add-Type -AssemblyName System.IO.Compression.FileSystem
$payloads = @()

foreach ($engine in $installed) {
	# Kept short on purpose: UBT refuses intermediate paths over 260 characters.
	$package = Join-Path $PackageRoot $engine.Version

	if (-not $SkipPackage -or -not (Test-Path (Join-Path $package "SpoutUE.uplugin"))) {
		Write-Host "== Packaging for UE $($engine.Version) ($($engine.Root))"
		if (Test-Path $package) { Remove-Item $package -Recurse -Force }
		$uat = Join-Path $engine.Root "Engine\Build\BatchFiles\RunUAT.bat"
		& $uat BuildPlugin "-Plugin=$PluginFile" "-Package=$package" -TargetPlatforms=Win64 -Rocket | Out-Host
		if ($LASTEXITCODE -ne 0) {
			Write-Warning "BuildPlugin failed for UE $($engine.Version); that version is left out of the installer."
			continue
		}
	}

	# Debug symbols are a third of the package and of no use to an installed plugin.
	$stage = Join-Path $Work "stage-$($engine.Version)"
	robocopy $package $stage /E /XF *.pdb /NJH /NJS /NP /NFL /NDL | Out-Null
	Copy-Item (Join-Path $RepoRoot "LICENSE"), (Join-Path $RepoRoot "README.md") $stage

	$zip = Join-Path $Work "Payload.$($engine.Version).zip"
	[System.IO.Compression.ZipFile]::CreateFromDirectory($stage, $zip, [System.IO.Compression.CompressionLevel]::Optimal, $false)
	Remove-Item $stage -Recurse -Force
	$payloads += [pscustomobject]@{ Version = $engine.Version; Zip = $zip }
	Write-Host ("   UE {0}: {1:N0} MB" -f $engine.Version, ((Get-Item $zip).Length / 1MB))
}

if ($payloads.Count -eq 0) {
	throw "No engine could be packaged; nothing to put in the installer."
}

# ------------------------------------------------------------------ icon

# Windows has read PNG-compressed icons since Vista: an ICO header and the PNG as is.
$png = [System.IO.File]::ReadAllBytes((Join-Path $RepoRoot "Resources\Icon128.png"))
$ico = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $ico
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]1)                     # ICONDIR
$w.Write([byte]128); $w.Write([byte]128); $w.Write([byte]0); $w.Write([byte]0)     # 128x128
$w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]$png.Length); $w.Write([uint32]22)
$w.Write($png)
$icon = Join-Path $Work "SpoutUE.ico"
[System.IO.File]::WriteAllBytes($icon, $ico.ToArray())

# ------------------------------------------------------------------ compile

$numericVersion = ($Version -replace '[^0-9.]', '')
$assemblyInfo = Join-Path $Work "AssemblyInfo.cs"
@"
using System.Reflection;
[assembly: AssemblyTitle("Spout UE Setup")]
[assembly: AssemblyProduct("Spout UE")]
[assembly: AssemblyDescription("Installs the Spout UE plugin into Unreal Engine.")]
[assembly: AssemblyCopyright("SpoutUE contributors, MIT licensed")]
[assembly: AssemblyVersion("$numericVersion")]
[assembly: AssemblyFileVersion("$numericVersion")]
[assembly: AssemblyInformationalVersion("$Version")]
"@ | Set-Content $assemblyInfo -Encoding UTF8

$csc = Join-Path $env:WINDIR "Microsoft.NET\Framework64\v4.0.30319\csc.exe"
$exe = Join-Path $OutDir "SpoutUE-Setup-$Version.exe"
$cscArgs = @(
	"/nologo", "/target:winexe", "/optimize+", "/codepage:65001",
	"/out:$exe",
	"/win32manifest:$(Join-Path $PSScriptRoot 'app.manifest')",
	"/win32icon:$icon",
	"/r:System.dll", "/r:System.Core.dll", "/r:System.Drawing.dll", "/r:System.Windows.Forms.dll",
	"/r:System.IO.Compression.dll", "/r:System.IO.Compression.FileSystem.dll"
)
foreach ($p in $payloads) { $cscArgs += "/resource:$($p.Zip),Payload.$($p.Version).zip" }
$cscArgs += (Join-Path $PSScriptRoot "SpoutUEInstaller.cs"), $assemblyInfo

& $csc @cscArgs
if ($LASTEXITCODE -ne 0) { throw "csc failed." }

# ------------------------------------------------------------------ self-test

# Install into, and uninstall from, fake engine folders: exercises the payloads
# and the file handling without touching a real engine or asking for elevation.
$fakeRoots = @()
foreach ($p in $payloads) {
	$root = Join-Path $Work "selftest\UE_$($p.Version)"
	New-Item -ItemType Directory -Force (Join-Path $root "Engine\Build") | Out-Null
	$major, $minor = $p.Version.Split(".")
	"{ `"MajorVersion`": $major, `"MinorVersion`": $minor, `"PatchVersion`": 0 }" | Set-Content (Join-Path $root "Engine\Build\Build.version")
	$fakeRoots += $root
}
$rootsArg = "/engineroot=" + ($fakeRoots -join ";")
$log = Join-Path $Work "selftest.log"

$install = Start-Process $exe -ArgumentList "/install", $rootsArg, "/log=$log" -Wait -PassThru
$installedOk = $install.ExitCode -eq 0 -and @($fakeRoots | Where-Object { -not (Test-Path (Join-Path $_ "Engine\Plugins\Marketplace\SpoutUE\Binaries\Win64\UnrealEditor-SpoutUE.dll")) }).Count -eq 0
$uninstall = Start-Process $exe -ArgumentList "/uninstall", $rootsArg, "/log=$log" -Wait -PassThru
$removedOk = $uninstall.ExitCode -eq 0 -and @($fakeRoots | Where-Object { Test-Path (Join-Path $_ "Engine\Plugins\Marketplace\SpoutUE") }).Count -eq 0
Remove-Item (Join-Path $Work "selftest") -Recurse -Force

if (-not ($installedOk -and $removedOk)) {
	throw "Self-test failed (install ok: $installedOk, uninstall ok: $removedOk). See $log."
}

Write-Host ""
Write-Host ("Built {0} ({1:N0} MB) for UE {2}. Self-test passed." -f $exe, ((Get-Item $exe).Length / 1MB), (($payloads | ForEach-Object { $_.Version }) -join ", "))
