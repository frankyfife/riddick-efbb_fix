param([string]$times = "8,16,24,32", [string]$prefix = "s", [string]$pad = "")
if ($pad) { $env:RIDDICKFIX_PADSCRIPT = $pad } else { Remove-Item Env:RIDDICKFIX_PADSCRIPT -ErrorAction SilentlyContinue }
$at = $times.Split(",") | ForEach-Object { [int]$_ }
$ShotDir = $(if ($env:RIDDICKFIX_TESTDIR) { $env:RIDDICKFIX_TESTDIR } else { "$env:TEMP\RiddickFixTest" })
Add-Type @"
using System; using System.Runtime.InteropServices;
public class FG { [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h); }
"@
$p = Start-Process -FilePath "D:\Starbreeze Studios\Riddick EFBB\Riddick.exe" -WorkingDirectory "D:\Starbreeze Studios\Riddick EFBB" -PassThru
$elapsed = 0
foreach ($a in $at) {
  Start-Sleep -Seconds ($a - $elapsed); $elapsed = $a
  $s = Get-Process SbzEngine -ErrorAction SilentlyContinue
  if (!$s) { "t=$a EXITED"; break }
  [FG]::SetForegroundWindow($s.MainWindowHandle) | Out-Null
  & "C:\Tools\bin\ffmpeg.exe" -loglevel error -filter_complex "ddagrab=output_idx=0,hwdownload,format=bgra,scale=640:-1" -frames:v 1 -update 1 -y "$ShotDir\$prefix$a.png"
  "t=$a cpu=$([int]$s.TotalProcessorTime.TotalSeconds)"
}
taskkill /F /IM SbzEngine.exe 2>$null | Out-Null; taskkill /F /IM Riddick.exe 2>$null | Out-Null
