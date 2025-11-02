param(
    [string]$FilePath = "native/qt6/src/mainwindow.cpp"
)

$content = Get-Content $FilePath -Raw
$lines = $content -split "`n"
$newLines = @()
$i = 0

while ($i -lt $lines.Count) {
    $line = $lines[$i]
    
    # Check if this line has qDebug() and is not already wrapped
    if ($line -match 'qDebug\(\)' -and $line -notmatch '#ifdef' -and $line -notmatch '#endif') {
        # Check if previous lines have #ifdef QT_DEBUG
        $hasDebugGuard = $false
        for ($j = [Math]::Max(0, $i-5); $j -lt $i; $j++) {
            if ($lines[$j] -match '#ifdef QT_DEBUG') {
                $hasDebugGuard = $true
                break
            }
        }
        
        if (-not $hasDebugGuard) {
            # Add #ifdef QT_DEBUG before this line
            $indent = $line -replace '^(\s*).*', '$1'
            $newLines += "#ifdef QT_DEBUG"
            $newLines += $line
            
            # Check if next line is also qDebug() or continuation
            $j = $i + 1
            while ($j -lt $lines.Count -and ($lines[$j] -match 'qDebug\(\)' -or $lines[$j] -match '<<' -or $lines[$j].Trim() -eq '')) {
                if ($lines[$j] -match 'qDebug\(\)' -or $lines[$j] -match '<<') {
                    $newLines += $lines[$j]
                    $j++
                } else {
                    break
                }
            }
            
            # Add #endif after the qDebug statement
            $newLines += "#endif"
            $i = $j
            continue
        }
    }
    
    $newLines += $line
    $i++
}

$newContent = $newLines -join "`n"
$newContent | Set-Content $FilePath -Encoding UTF8
Write-Host "Wrapped qDebug() calls in $FilePath"

