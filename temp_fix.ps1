$file = "e:\KAssetManager\native\qt6\src\mainwindow.cpp"
$lines = Get-Content $file
$before = $lines[0..194]
$after = $lines[735..($lines.Count-1)]
$newContent = $before + $after
$newContent | Set-Content $file
Write-Host "Removed lines 196-735"
