param([int]$dx = 100, [int]$steps = 10)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class MM { [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, UIntPtr e); }
"@
for ($i = 0; $i -lt $steps; $i++) { [MM]::mouse_event(1, $dx, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 20 }
