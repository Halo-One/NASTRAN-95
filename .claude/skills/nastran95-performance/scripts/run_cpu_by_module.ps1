# run_cpu_by_module.ps1 -Exe <nastran95ase.exe> -Deck <deck.dat> -Out <dir> [-Env @{NAME='value'}] [-Period 0.5]
#
# The Windows twin of run_cpu_by_module.sh: runs a SOL 145 deck through the
# driver and, every Period seconds, reads each child's whole-process CPU
# (all its threads: Get-Process TotalProcessorTime) and the DMAP module its
# log is in (the last BEGN line of <out>\s<subcase>\<stem>_s<subcase>.log),
# and attributes the CPU and the wall time since the last sample to that
# module. Prints wall and CPU per child per module and the CPU per module
# over all children. The NASTRAN logs' own clock columns cannot be used for
# this on Windows: they count the main thread's CPU only (a 29-minute child
# of Jon's exe logs 57 s), so threaded modules and waits vanish from them.
#
# From cmd, call it through -Command: with -File a hashtable argument
# arrives as a string and is refused.
#   powershell -NoProfile -ExecutionPolicy Bypass -Command "& '<scripts>\run_cpu_by_module.ps1' -Exe '<exe>' -Deck deck.dat -Out C:\temp\c"
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Deck,
    [Parameter(Mandatory = $true)][string]$Out,
    [hashtable]$Env = @{},
    [double]$Period = 0.5
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$deckDir = Split-Path -Parent (Resolve-Path $Deck).Path
$deckName = Split-Path -Leaf $Deck

# the driver, with the environment asked for
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = (Resolve-Path $Exe).Path
$psi.Arguments = "`"$deckName`" `"$Out`""
$psi.WorkingDirectory = $deckDir
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
foreach ($k in $Env.Keys) { $psi.EnvironmentVariables[$k] = [string]$Env[$k] }
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$driver = [System.Diagnostics.Process]::Start($psi)
$stdout = $driver.StandardOutput.ReadToEndAsync()
$stderr = $driver.StandardError.ReadToEndAsync()

$cpuPrev = @{}      # child pid -> seconds
$wall = @{}         # "subcase|module" -> seconds
$cpu = @{}
$lastT = 0.0
while (-not $driver.HasExited) {
    Start-Sleep -Milliseconds ([int]($Period * 1000))
    $t = $sw.Elapsed.TotalSeconds
    $dt = $t - $lastT
    $lastT = $t
    $kids = @(Get-CimInstance Win32_Process -Filter "ParentProcessId = $($driver.Id)" -ErrorAction SilentlyContinue)
    foreach ($k in $kids) {
        if ($k.CommandLine -notmatch '(s\d+)[\\/][^\\/"]+\.dat') { continue }
        $sub = $Matches[1]
        $p = Get-Process -Id $k.ProcessId -ErrorAction SilentlyContinue
        if (-not $p) { continue }
        $c = $p.TotalProcessorTime.TotalSeconds
        $dc = $c - $(if ($cpuPrev.ContainsKey($k.ProcessId)) { $cpuPrev[$k.ProcessId] } else { 0 })
        $cpuPrev[$k.ProcessId] = $c
        $log = Get-ChildItem (Join-Path $Out $sub) -Filter '*.log' -ErrorAction SilentlyContinue | Select-Object -First 1
        $mod = 'start'
        if ($log) {
            # the DMAP module: its statement number, then its name, then BEGN
            # (a subroutine's own lines inside a module put the name first:
            # "MMA1  0  BEGN", and must not be taken for the module)
            $line = Get-Content $log.FullName -Tail 300 -ErrorAction SilentlyContinue |
                Where-Object { $_ -match '\s\d+\s+([A-Z][A-Z0-9]*)\s+BEGN\s*$' } | Select-Object -Last 1
            if ($line -match '\s\d+\s+([A-Z][A-Z0-9]*)\s+BEGN\s*$') { $mod = $Matches[1] }
        }
        $key = "$sub|$mod"
        $wall[$key] = $(if ($wall.ContainsKey($key)) { $wall[$key] } else { 0 }) + $dt
        $cpu[$key] = $(if ($cpu.ContainsKey($key)) { $cpu[$key] } else { 0 }) + $dc
    }
}
$total = $sw.Elapsed.TotalSeconds
"exit $($driver.ExitCode) wall $([math]::Round($total, 1)) s" | Tee-Object -FilePath (Join-Path $Out 'timing.txt')
$rows = foreach ($key in $wall.Keys) {
    $sub, $mod = $key -split '\|', 2
    [pscustomobject]@{ child = $sub; module = $mod; wall_s = [math]::Round($wall[$key], 1); cpu_s = [math]::Round($cpu[$key], 1) }
}
$rows | Sort-Object child, @{ Expression = 'wall_s'; Descending = $true } | Format-Table -AutoSize | Out-String | Tee-Object -FilePath (Join-Path $Out 'timing.txt') -Append
'CPU per module over all children:' | Tee-Object -FilePath (Join-Path $Out 'timing.txt') -Append
$rows | Group-Object module | ForEach-Object {
    [pscustomobject]@{ module = $_.Name; cpu_s = [math]::Round(($_.Group | Measure-Object cpu_s -Sum).Sum, 1); wall_s_sum = [math]::Round(($_.Group | Measure-Object wall_s -Sum).Sum, 1) }
} | Sort-Object cpu_s -Descending | Format-Table -AutoSize | Out-String | Tee-Object -FilePath (Join-Path $Out 'timing.txt') -Append
