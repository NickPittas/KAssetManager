Param(
  [string]$Source = "native/qt6/src/widgets/file_manager_widget.cpp",
  [string]$Dest = "native/qt6/src/widgets/file_manager_widget_fixed.cpp"
)

# Try robust binary-level cleanup first: drop all NUL bytes and normalize CR/LF
$bytes = [System.IO.File]::ReadAllBytes($Source)
$list = New-Object System.Collections.Generic.List[byte]
foreach ($b in $bytes) { if ($b -ne 0) { [void]$list.Add($b) } }
$clean = $list.ToArray()
$text = [System.Text.Encoding]::UTF8.GetString($clean)

# Fallback: if still looks weird (contains lots of NUL markers), auto-detect via StreamReader
if ($text -match "\x00") {
  $fs = [System.IO.File]::Open($Source, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
  try {
    $sr = New-Object System.IO.StreamReader($fs, $true)
    $text = $sr.ReadToEnd()
    $sr.Close()
  } finally {
    $fs.Close()
  }
}

# Normalize line endings to CRLF and write as UTF-8 without BOM
$text = $text -replace "\r?\n", "`r`n"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($Dest, $text, $utf8NoBom)

