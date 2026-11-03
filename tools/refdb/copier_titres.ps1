# Abrasion Labs // OMNIS - copie, depuis un dossier de sons, les titres d'une liste
# ("Artiste - Titre", un par ligne) vers un dossier de travail court, nommes
# "Artiste - Titre.ext" (orthographe de la liste) pour omnis_refdb scan --attendu.
# Les originaux ne sont ni deplaces ni modifies. Aucun telechargement.
#   powershell -ExecutionPolicy Bypass -File copier_titres.ps1 -Source D:\Claude -Dest C:\OM\r36 -Liste data\remesure_36.txt
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [Parameter(Mandatory = $true)][string]$Dest,
    [Parameter(Mandatory = $true)][string]$Liste
)
function Norm([string]$s) {
    $d = $s.Normalize([Text.NormalizationForm]::FormD)
    $sb = New-Object Text.StringBuilder
    foreach ($c in $d.ToCharArray()) {
        if ([Globalization.CharUnicodeInfo]::GetUnicodeCategory($c) -ne [Globalization.UnicodeCategory]::NonSpacingMark) { [void]$sb.Append($c) }
    }
    return (($sb.ToString().ToLower().Replace('$', 's')) -replace '[^a-z0-9]', '')
}
New-Item -ItemType Directory -Force -Path $Dest | Out-Null
$files = Get-ChildItem -Path $Source -Recurse -File | Where-Object { $_.Extension -match '^\.(mp3|flac|wav|m4a|opus|ogg|aac)$' }
$index = $files | ForEach-Object { [pscustomobject]@{ File = $_; Key = (Norm $_.BaseName) } }
$ok = 0; $missing = @(); $several = @()
foreach ($line in (Get-Content -Path $Liste -Encoding UTF8)) {
    if ($line.Trim() -eq '' -or $line.StartsWith('#')) { continue }
    $parts = $line -split ' - ', 2
    if ($parts.Count -lt 2) { continue }
    $artist = $parts[0]; $title = $parts[1]
    $t = Norm $title
    $a = Norm (($artist -split ',')[0])
    $hits = @($index | Where-Object { $_.Key.Contains($t) -and $_.Key.Contains($a) })
    if ($hits.Count -eq 0) { $missing += $line; continue }
    if ($hits.Count -gt 1) { $several += "$line  ->  $($hits.Count) fichiers, pris : $($hits[0].File.Name)" }
    $safe = ($line -replace '[\\/:*?"<>|]', '_')
    Copy-Item -LiteralPath $hits[0].File.FullName -Destination (Join-Path $Dest ($safe + $hits[0].File.Extension)) -Force
    $ok++
}
Write-Host "$ok titre(s) copie(s) dans $Dest"
if ($several.Count) { Write-Host "Plusieurs fichiers possibles (verifie) :"; $several | ForEach-Object { Write-Host "  $_" } }
if ($missing.Count) { Write-Host "Introuvables dans $Source :"; $missing | ForEach-Object { Write-Host "  $_" } }
