param([int]$secs = 20, [string]$shot = "shot.png", [switch]$keep)
$ShotDir = $(if ($env:RIDDICKFIX_TESTDIR) { $env:RIDDICKFIX_TESTDIR } else { "$env:TEMP\RiddickFixTest" })
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices; using System.Collections.Generic;
public class TW {
 public delegate bool P(IntPtr h, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumWindows(P p, IntPtr l);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr w, P p, IntPtr l);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 public struct RECT{public int L,T,R,B;}
 public static List<string> Dump(uint pid){ var res=new List<string>();
  EnumWindows((h,l)=>{ uint p; GetWindowThreadProcessId(h,out p); if(p==pid){ var sb=new StringBuilder(4096); var c=new StringBuilder(256); GetWindowText(h,sb,4096); GetClassName(h,c,256); RECT r; GetWindowRect(h,out r);
     if(c.ToString().StartsWith("NVOpenGL")||c.ToString().StartsWith("MSCTF")||c.ToString()=="IME") return true;
     res.Add("WIN ["+c+"] '"+sb+"' "+r.L+","+r.T+"-"+r.R+","+r.B);
     EnumChildWindows(h,(h2,l2)=>{ var s2=new StringBuilder(4096); GetWindowText(h2,s2,4096); if(s2.Length>0) res.Add("   : "+s2); return true;},IntPtr.Zero);} return true;},IntPtr.Zero); return res; } }
"@
$p = Start-Process -FilePath "D:\Starbreeze Studios\Riddick EFBB\Riddick.exe" -WorkingDirectory "D:\Starbreeze Studios\Riddick EFBB" -PassThru
Start-Sleep -Seconds 4
$s = Get-Process SbzEngine -ErrorAction SilentlyContinue
if ($s) { [TW]::SetForegroundWindow($s.MainWindowHandle) | Out-Null }
Start-Sleep -Seconds ($secs - 4)
$s = Get-Process SbzEngine -ErrorAction SilentlyContinue
if ($s) {
  "alive pid=$($s.Id) cpu=$([int]$s.TotalProcessorTime.TotalSeconds)s mem=$([int]($s.WorkingSet64/1MB))MB"
  [TW]::Dump([uint32]$s.Id)
} else { "PROCESS EXITED" }
& "C:\Tools\bin\ffmpeg.exe" -loglevel error -filter_complex "ddagrab=output_idx=0,hwdownload,format=bgra,scale=1280:-1" -frames:v 1 -update 1 -y "$ShotDir$shot"
if (-not $keep) { taskkill /F /IM SbzEngine.exe 2>$null | Out-Null; taskkill /F /IM Riddick.exe 2>$null | Out-Null }
