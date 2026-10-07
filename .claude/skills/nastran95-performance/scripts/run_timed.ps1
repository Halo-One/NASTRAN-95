# run_timed.ps1 -Exe <nastran95ase.exe> -Deck <deck.dat> -Out <dir> [-Env @{NAME='value'}]
#
# The Windows twin of run_timed.sh: runs one deck through nastran95ase (the
# SOL 145 driver and its per-Mach children, or a single run) and records
# the wall time and the CPU time of the whole process tree - the driver
# and every child, all their threads, user + kernel - in <out>\timing.txt,
# and the console in <out>\console.txt.
#
# The CPU comes from a Windows job object: this PowerShell joins a new job
# before it starts the driver, so every process it starts, and every
# process those start, is counted (Windows 8 and later nest jobs, so the
# driver's own job for its children does not break it); this script's own
# CPU up to the start is subtracted. The NASTRAN logs' clock columns count
# the main thread only on Windows (a 29-minute child of Jon's exe logs
# 57 s), and Get-Process sees a child only while it runs.
#
# Environment: $Env is added to this process's environment, which the
# driver and its children inherit. Jon's exe ignores the N95_* switches it
# does not know.
#
# From cmd, call it through -Command: with -File a hashtable argument
# arrives as a string and is refused.
#   powershell -NoProfile -ExecutionPolicy Bypass -Command "& '<scripts>\run_timed.ps1' -Exe '<exe>' -Deck deck.dat -Out C:\temp\t -Env @{N95_AJJ_SOLVE='builtin'}"
param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Deck,
    [Parameter(Mandatory = $true)][string]$Out,
    [hashtable]$Env = @{}
)
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$deckPath = (Resolve-Path $Deck).Path
$exePath = (Resolve-Path $Exe).Path

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class N95Job {
    [StructLayout(LayoutKind.Sequential)]
    public struct BASIC_ACCOUNTING {
        public long TotalUserTime, TotalKernelTime, ThisPeriodTotalUserTime, ThisPeriodTotalKernelTime;
        public uint TotalPageFaultCount, TotalProcesses, ActiveProcesses, TotalTerminatedProcesses;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct BASIC_LIMIT {
        public long PerProcessUserTimeLimit, PerJobUserTimeLimit;
        public uint LimitFlags; public UIntPtr MinimumWorkingSetSize, MaximumWorkingSetSize;
        public uint ActiveProcessLimit; public UIntPtr Affinity; public uint PriorityClass, SchedulingClass;
    }
    [StructLayout(LayoutKind.Sequential)]
    public struct IO_COUNTERS { public ulong a, b, c, d, e, f; }
    [StructLayout(LayoutKind.Sequential)]
    public struct EXTENDED_LIMIT {
        public BASIC_LIMIT Basic; public IO_COUNTERS Io;
        public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
    }
    [DllImport("kernel32.dll", SetLastError = true)] public static extern IntPtr CreateJobObject(IntPtr a, string name);
    [DllImport("kernel32.dll", SetLastError = true)] public static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll")] public static extern IntPtr GetCurrentProcess();
    [DllImport("kernel32.dll", SetLastError = true)] public static extern bool QueryInformationJobObject(IntPtr job, int cls, out BASIC_ACCOUNTING info, int len, IntPtr ret);
    [DllImport("kernel32.dll", SetLastError = true)] public static extern bool QueryInformationJobObject(IntPtr job, int cls, out EXTENDED_LIMIT info, int len, IntPtr ret);
    public static IntPtr Job;
    public static void Join() {
        Job = CreateJobObject(IntPtr.Zero, null);
        if (Job == IntPtr.Zero || !AssignProcessToJobObject(Job, GetCurrentProcess()))
            throw new Exception("job object: error " + Marshal.GetLastWin32Error());
    }
    public static double[] Totals() {
        BASIC_ACCOUNTING a; EXTENDED_LIMIT e;
        QueryInformationJobObject(Job, 1, out a, Marshal.SizeOf(typeof(BASIC_ACCOUNTING)), IntPtr.Zero);
        QueryInformationJobObject(Job, 9, out e, Marshal.SizeOf(typeof(EXTENDED_LIMIT)), IntPtr.Zero);
        return new double[] { a.TotalUserTime / 1e7, a.TotalKernelTime / 1e7, a.TotalProcesses,
                              (double) e.PeakProcessMemoryUsed.ToUInt64() / 1048576.0 };
    }
}
'@

[N95Job]::Join()
foreach ($k in $Env.Keys) { [Environment]::SetEnvironmentVariable($k, [string]$Env[$k], 'Process') }
$before = [N95Job]::Totals()
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $exePath -ArgumentList "`"$(Split-Path -Leaf $deckPath)`" `"$Out`"" `
    -WorkingDirectory (Split-Path -Parent $deckPath) -NoNewWindow -PassThru `
    -RedirectStandardOutput (Join-Path $Out 'console.txt') -RedirectStandardError (Join-Path $Out 'stderr.txt')
$null = $p.Handle    # without it Windows PowerShell 5.1 loses the exit code
$p.WaitForExit()
$rc = $p.ExitCode
$wall = $sw.Elapsed.TotalSeconds
$after = [N95Job]::Totals()
$user = $after[0] - $before[0]; $kernel = $after[1] - $before[1]
$envText = ($Env.Keys | Sort-Object | ForEach-Object { "$_=$($Env[$_])" }) -join ' '
$line = "exit {0} wall {1:F1} s cpu {2:F1} s (user {3:F1}, kernel {4:F1}) processes {5} peak process memory {6:F0} MB env {7}" -f `
    $rc, $wall, ($user + $kernel), $user, $kernel, ($after[2] - $before[2]), $after[3], $envText
$line | Out-File -FilePath (Join-Path $Out 'timing.txt') -Encoding ascii   # Tee-Object writes UTF-16 in Windows PowerShell 5.1
$line
