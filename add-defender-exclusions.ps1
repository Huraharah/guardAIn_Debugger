# Windows Defender Exclusion Script
# Run this script as Administrator to add exclusions for your development folders

Write-Host "Adding Windows Defender exclusions for guardAInDBG development..." -ForegroundColor Green

# Check if running as administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "ERROR: This script must be run as Administrator!" -ForegroundColor Red
    Write-Host "Right-click PowerShell and select 'Run as Administrator', then run this script again." -ForegroundColor Yellow
    exit 1
}

# Add exclusions
$exclusions = @(
    "A:\guardAInDBG",           # Main project folder
    "A:\guardAInDBG\x64",       # Build output folder
    "A:\artifacts"              # Artifacts folder (if it exists)
)

foreach ($path in $exclusions) {
    if (Test-Path $path) {
        try {
            Add-MpPreference -ExclusionPath $path -ErrorAction Stop
            Write-Host "✓ Added exclusion: $path" -ForegroundColor Green
        }
        catch {
            Write-Host "✗ Failed to add exclusion for $path : $_" -ForegroundColor Red
        }
    }
    else {
        Write-Host "⚠ Path does not exist (skipping): $path" -ForegroundColor Yellow
    }
}

# List current exclusions
Write-Host "`nCurrent Windows Defender exclusions:" -ForegroundColor Cyan
Get-MpPreference | Select-Object -ExpandProperty ExclusionPath | ForEach-Object {
    Write-Host "  - $_" -ForegroundColor Gray
}

Write-Host "`nDone! Your development folders are now excluded from Windows Defender scans." -ForegroundColor Green




