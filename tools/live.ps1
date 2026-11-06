# start game with live pad channel; stays running
$env:RIDDICKFIX_PADLIVE = ($(if ($env:RIDDICKFIX_TESTDIR) { $env:RIDDICKFIX_TESTDIR } else { "$env:TEMP\RiddickFixTest" }) + "\pad_live.txt")
New-Item -ItemType Directory -Force (Split-Path $env:RIDDICKFIX_PADLIVE) | Out-Null
Remove-Item Env:RIDDICKFIX_PADSCRIPT -ErrorAction SilentlyContinue
Set-Content -Path $env:RIDDICKFIX_PADLIVE -Value "0 0 0"
Start-Process -FilePath "D:\Starbreeze Studios\Riddick EFBB\Riddick.exe" -WorkingDirectory "D:\Starbreeze Studios\Riddick EFBB" | Out-Null
"started"
