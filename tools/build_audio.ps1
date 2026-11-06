# Builds DSOAL (DirectSound -> OpenAL) and OpenAL Soft as x64 with the static VC runtime.
# Output: build\audio\dsound.dll and build\audio\dsoal-aldrv.dll (copy both next to SbzEngine.exe,
# together with dist\alsoft.ini). Both projects are LGPL-2.1+, sources: github.com/kcat
param([string]$Work = "$PSScriptRoot\..\build\thirdparty")
$ErrorActionPreference = "Stop"
$cmake = (Get-Command cmake -ErrorAction SilentlyContinue).Source
if (-not $cmake) { $cmake = "C:\Program Files\CMake\bin\cmake.exe" }
New-Item -ItemType Directory -Force $Work | Out-Null
foreach ($r in "dsoal", "openal-soft") {
    if (-not (Test-Path "$Work\$r")) { git clone --depth 1 "https://github.com/kcat/$r.git" "$Work\$r" }
}
$static = @("-DCMAKE_POLICY_DEFAULT_CMP0091=NEW", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded")
& $cmake -S "$Work\openal-soft" -B "$Work\build-oal" -A x64 -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF `
    -DALSOFT_TESTS=OFF -DALSOFT_INSTALL=OFF -DALSOFT_EAX=ON -DFORCE_STATIC_VCRT=ON @static
& $cmake --build "$Work\build-oal" --config Release
& $cmake -S "$Work\dsoal" -B "$Work\build-dsoal" -A x64 @static
& $cmake --build "$Work\build-dsoal" --config Release
$out = "$PSScriptRoot\..\build\audio"
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item "$Work\build-dsoal\Release\dsound.dll" "$out\dsound.dll" -Force
Copy-Item "$Work\build-oal\Release\OpenAL32.dll" "$out\dsoal-aldrv.dll" -Force
"dsoal $(git -C "$Work\dsoal" rev-parse --short HEAD), openal-soft $(git -C "$Work\openal-soft" rev-parse --short HEAD) -> $out"
