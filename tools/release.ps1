param(
  [switch]$NoPull,
  [switch]$NoPush
)

$ErrorActionPreference = "Stop"

function Get-FirmwareConstant {
  param(
    [string]$Name,
    [string]$Default = ""
  )

  $file = "src/AppConfig.cpp"
  if (-not (Test-Path $file)) {
    throw "No trobo $file. Executa aquest script des de l'arrel del projecte."
  }

  $content = Get-Content $file -Raw
  $pattern = 'const\s+char\*\s+' + [regex]::Escape($Name) + '\s*=\s*"([^"]+)"\s*;'
  $match = [regex]::Match($content, $pattern)
  if ($match.Success) {
    return $match.Groups[1].Value
  }

  return $Default
}

$version = Get-FirmwareConstant -Name "FIRMWARE_VERSION"
$title = Get-FirmwareConstant -Name "FIRMWARE_CHANGE_TITLE" -Default $version
$notes = Get-FirmwareConstant -Name "FIRMWARE_CHANGE_NOTES" -Default ""

if ([string]::IsNullOrWhiteSpace($title)) {
  throw "No puc llegir FIRMWARE_CHANGE_TITLE ni FIRMWARE_VERSION de src/AppConfig.cpp"
}

Write-Host "Firmware version : $version" -ForegroundColor Cyan
Write-Host "Commit message   : $title" -ForegroundColor Cyan
if (-not [string]::IsNullOrWhiteSpace($notes)) {
  Write-Host "Notes            : $notes" -ForegroundColor DarkCyan
}

# Evita que fitxers generats entrin per accident encara que algun dia canviï el .gitignore.
git rm -r --cached build .pio 2>$null | Out-Null

git add -A

$staged = git diff --cached --name-only
if ([string]::IsNullOrWhiteSpace($staged)) {
  Write-Host "No hi ha canvis per commitejar." -ForegroundColor Yellow
  if (-not $NoPull) {
    git pull --rebase origin main
  }
  exit 0
}

Write-Host "Fitxers que entraran al commit:" -ForegroundColor Green
$staged | ForEach-Object { Write-Host " - $_" }

git commit -m $title

if (-not $NoPull) {
  git pull --rebase origin main
}

if (-not $NoPush) {
  git push origin main
}

Write-Host "Release enviada correctament." -ForegroundColor Green
