# build_nastran95.ps1 - build nastran95.exe and nastran95ase.exe, the two
# standalone Windows executables, from this checkout, reproducibly.
#
#   powershell -ExecutionPolicy Bypass -File standalone\build\build_nastran95.ps1
#   ... -Source <dir>       another local source tree instead of this checkout
#   ... -Commit <sha>       that commit of Halo-One/NASTRAN-95, downloaded from
#                           GitHub, instead of any local tree: a release build,
#                           whose record names a commit anyone can fetch
#   ... -Branch <name>      with -Commit: the branch it is on, for the record and
#                           the --version label (a local tree's is read from git)
#   ... -InstallDir <dir>   where the executables and BUILD_INFO.txt go
#                           (default: standalone\, beside this build folder)
#   ... -WorkDir <dir>      where the toolchain, OpenBLAS and the build trees
#                           live (default: standalone\build\work, gitignored)
#   ... -NoInstall          build and check, but leave the executables in the
#                           build tree (a candidate under test)
#   ... -Clean              throw the work directory away first
#
# Needs a 64-bit Windows 10/11 box, PowerShell 5.1 or later, about 1.5 GB of
# disk and an internet connection for two downloads. Nothing is installed:
# the compiler is unpacked under the work directory, which can be deleted
# afterwards. A first build is about 40 minutes (the toolchain download, and
# OpenBLAS built once from source); afterwards a few minutes, and an
# incremental rebuild of a changed C file seconds.
#
# The skill .claude\skills\build-nastran95 walks through this script, what
# each choice below is for, and what a Mac build would still change. The
# Linux counterpart is build_nastran95.sh beside it.
#
# What comes out, and why it is built the way it is:
#
#   * The source is this checkout of Halo One's fork of NASA's NASTRAN-95
#     (github.com/Halo-One/NASTRAN-95). HALO.md lists every change against
#     NASA's tree, as the NASA Open Source Agreement asks: a CMake build,
#     the gfortran compatibility fixes, the bug fixes, the standalone
#     start-up (the deck on the command line, the rigid format library
#     compiled in, a scratch directory under TEMP, defaults for every
#     environment variable, the exit code taken from the print file), the
#     msc\ directory (the MSC dialect front end, the print-file rewrite, the
#     OUTPUT4 writer, the SOL 144 trim module and the SOL 145 and SOL 200
#     drivers, the fatal-message explainer), the FEER guards and the
#     wall-clock watchdog so that no run can hang, the link-time wrapper
#     around libgfortran's format-copy routine, and the flutter speed-ups.
#     One CMake build makes both executables from one library; they differ
#     only in a .TRUE./.FALSE. in the generated main program.
#   * The compiler is gfortran (and gcc, for msc\) from a portable winlibs GCC
#     release, pinned by URL and SHA-256. The MSVCRT flavour is chosen over
#     UCRT on purpose: msvcrt.dll is present on every Windows since 95, so the
#     result runs on any 64-bit Windows machine without a redistributable.
#   * The link is static (-static): libgfortran, libquadmath, libgcc and
#     libwinpthread are folded into the executable. Its only imports are
#     KERNEL32.dll and msvcrt.dll, which the script checks at the end.
#   * -O0, except for the fenced optimised files CMakeLists.txt lists, each
#     with its reason. The CMakeLists says why: 1970s FORTRAN with a
#     hand-rolled memory manager that aliases INTEGER, REAL and DOUBLE
#     PRECISION through one COMMON block gives an optimiser plenty of rope.
#     Measured: the -O2 build fails NASA demo d01013a in the case control
#     parser (UFM 615) and segfaults on ten-element cantilevers; the -O0
#     build reproduces NASA's 1995 print files to the printed digit.
#   * Debug symbols are stripped afterwards; they change nothing at run time.
#   * Open core is 256,000,000 words (1 GB), up from the 14,000,000 NASA
#     shipped: a 120-mode flutter model needs the room, and it is a static
#     array in the executable's data segment, so it costs nothing until used.
#     It is a link-time constant; -DNASTRAN_OPEN_CORE_WORDS below sets it.
#     nastran95 splits it 2,000,000 for the modules and the rest for the
#     in-memory database, as NASA did; nastran95ase gives the modules three
#     quarters. OCMEM and DBMEM in the environment override either.
#   * The rigid format library (rf\) is compiled into the exe by the CMake
#     build, so nothing travels with the executable. What the exe writes
#     into its scratch directory at start-up is exactly those files;
#     `nastran95 --version` says which commit they came from.
#   * OpenBLAS 0.3.34 for the in-core doublet lattice solve and SOL 144's
#     dense algebra, built here once from its pinned release (static,
#     DYNAMIC_ARCH over the SSE3 / AVX / AVX2 / AVX-512 kernel sets, OpenMP
#     from the same libgomp as the solver, no thread pinning), with
#     OpenBLAS's own CMake and the toolchain's Ninja (its Makefiles need a
#     POSIX shell, and MSYS forks make them hopelessly slow here). Kept in
#     the work directory like the toolchain.
#   * The link also pins the executable at a fixed, low image base with
#     ASLR off; the comment above $linkFlags says why that is load-bearing.

param(
    [switch]$Clean,
    [string]$Source = '',
    [string]$Commit = '',
    [string]$Branch = '',
    [string]$InstallDir = '',
    [string]$WorkDir = '',
    [switch]$NoInstall
)

$ErrorActionPreference = 'Stop'
# Invoke-WebRequest is an order of magnitude slower with its progress bar on
$ProgressPreference = 'SilentlyContinue'

# --- what is pinned ----------------------------------------------------------
$ToolchainUrl    = 'https://github.com/brechtsanders/winlibs_mingw/releases/download/16.2.0posix-14.0.0-msvcrt-r1/winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64msvcrt-14.0.0-r1.zip'
$ToolchainSha256 = 'A6CB646ED5FC1C2326D29FAD90BDAE5B1D6251463DD81B6E37E499FE8EF80CA8'
$SourceRepo      = 'Halo-One/NASTRAN-95'   # where -Commit downloads from
$OpenCoreWords   = '256000000'   # 1 GB: a static array, costs nothing until used; below 2 GB with the fixed image base
$OpenBlasVersion = '0.3.34'
$OpenBlasUrl     = "https://github.com/OpenMathLib/OpenBLAS/releases/download/v$OpenBlasVersion/OpenBLAS-$OpenBlasVersion.tar.gz"
$OpenBlasSha256  = 'CD7E129868320CC2D033AFA920E31202DFE0B8066A5B66661900CCC0F197DFED'
$OpenBlasCores   = 'PRESCOTT SANDYBRIDGE HASWELL SKYLAKEX'   # SSE3, AVX, AVX2+FMA, AVX-512: as Linux

# --- layout ------------------------------------------------------------------
$buildDir   = $PSScriptRoot                                   # standalone\build
$repoRoot   = Split-Path -Parent (Split-Path -Parent $buildDir)
if (-not $InstallDir) { $InstallDir = Split-Path -Parent $buildDir }   # standalone\
if (-not $WorkDir)    { $WorkDir = Join-Path $buildDir 'work' }
$work = $WorkDir
if ($Clean -and (Test-Path $work)) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Force -Path $work | Out-Null
$work = (Resolve-Path $work).Path

$toolchainZip = Join-Path $work 'winlibs.zip'
$toolchainDir = Join-Path $work 'winlibs'
$openblasTgz  = Join-Path $work "OpenBLAS-$OpenBlasVersion.tar.gz"
$openblasDir  = Join-Path $work ("OpenBLAS-$OpenBlasVersion-static-omp-" + ($OpenBlasCores.ToLower() -replace ' ', '-'))
$openblasLib  = Join-Path $openblasDir 'libopenblas.a'

function Expand-Zip($zip, $dest) {
    # Windows 10 1803+ ships bsdtar as tar.exe and it unpacks a 260 MB zip in
    # a fraction of the time Expand-Archive takes; fall back if it is absent
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    # (named by full path: a Git for Windows install puts GNU tar on PATH
    # first, and GNU tar does not read zip files)
    $tar = "$env:SystemRoot\System32\tar.exe"
    if (Test-Path $tar) {
        & $tar -xf $zip -C $dest
        if ($LASTEXITCODE -ne 0) { throw "tar failed unpacking $zip" }
    } else {
        Expand-Archive -Path $zip -DestinationPath $dest -Force
    }
}

# --- the source: this checkout, another tree, or a commit of the fork --------
$sourceNote   = ''
$SourceBranch = $Branch
$sourceWhere  = ''
if ($Commit) {
    if ($Source) { throw '-Source and -Commit exclude each other' }
    $SourceCommit = $Commit.ToLower()
    if ($SourceCommit -notmatch '^[0-9a-f]{40}$') { throw '-Commit wants the full 40-character SHA, so that the record is unambiguous' }
    $sourceZip  = Join-Path $work "nastran-95-src-$($SourceCommit.Substring(0,8)).zip"
    $sourceDir  = Join-Path $work "NASTRAN-95-$SourceCommit"
    $cmakeBuild = Join-Path $work "cmake-build-$($SourceCommit.Substring(0,8))"
    $sourceWhere = "$SourceRepo @ $SourceCommit"
    if (-not (Test-Path (Join-Path $sourceDir 'CMakeLists.txt'))) {
        if (-not (Test-Path $sourceZip)) {
            Write-Host "Downloading $SourceRepo at $($SourceCommit.Substring(0,8))..." -ForegroundColor Cyan
            Invoke-WebRequest -Uri "https://github.com/$SourceRepo/archive/$SourceCommit.zip" -OutFile $sourceZip
        }
        Write-Host "Unpacking the source..." -ForegroundColor Cyan
        Expand-Zip $sourceZip $work
        if (-not (Test-Path (Join-Path $sourceDir 'CMakeLists.txt'))) {
            throw "Expected the source under $sourceDir and did not find it"
        }
    }
} else {
    # a local tree: its HEAD names the build, and local changes are said
    if ($Source) {
        $sourceDir = (Resolve-Path $Source).Path
        $cmakeBuild = Join-Path $work ('cmake-build-src-' + (Split-Path -Leaf $sourceDir))
    } else {
        $sourceDir = $repoRoot
        $cmakeBuild = Join-Path $work 'cmake-build-local'
    }
    if (-not (Test-Path (Join-Path $sourceDir 'CMakeLists.txt'))) { throw "$sourceDir has no CMakeLists.txt" }
    $SourceCommit = 'unknown'
    try {
        $head = (& git -C $sourceDir rev-parse HEAD 2>$null | Out-String).Trim()
        if ($LASTEXITCODE -eq 0 -and $head) {
            $SourceCommit = $head
            if (-not $SourceBranch) {
                $SourceBranch = (& git -C $sourceDir rev-parse --abbrev-ref HEAD | Out-String).Trim()
                if ($SourceBranch -eq 'HEAD') { $SourceBranch = 'detached' }
            }
            if ((& git -C $sourceDir status --porcelain --untracked-files=no | Out-String).Trim()) { $sourceNote = ' + local changes' }
            $remote = (& git -C $sourceDir remote get-url origin 2>$null | Out-String).Trim()
            if ($LASTEXITCODE -ne 0) { $remote = '' }
        }
    } catch { }
    $sourceWhere = "$sourceDir @ $SourceCommit"
    if ($remote) { $sourceWhere += " (origin $remote)" }
}
$sha8 = if ($SourceCommit -eq 'unknown') { 'unknown' } else { $SourceCommit.Substring(0, 8) }

# --- toolchain ---------------------------------------------------------------
if (-not (Test-Path (Join-Path $toolchainDir 'mingw64\bin\gfortran.exe'))) {
    if (-not (Test-Path $toolchainZip)) {
        Write-Host "Downloading the gfortran toolchain (260 MB)..." -ForegroundColor Cyan
        Invoke-WebRequest -Uri $ToolchainUrl -OutFile $toolchainZip
    }
    $hash = (Get-FileHash -Algorithm SHA256 $toolchainZip).Hash
    if ($hash -ne $ToolchainSha256) {
        Remove-Item $toolchainZip
        throw "Toolchain download hash $hash does not match the pinned $ToolchainSha256; deleted it, run again"
    }
    Write-Host "Unpacking the toolchain..." -ForegroundColor Cyan
    Expand-Zip $toolchainZip $toolchainDir
}
$bin      = Join-Path $toolchainDir 'mingw64\bin'
$gfortran = Join-Path $bin 'gfortran.exe'
$gcc      = Join-Path $bin 'gcc.exe'
$cmake    = Join-Path $bin 'cmake.exe'
$ninja    = Join-Path $bin 'ninja.exe'
$strip    = Join-Path $bin 'strip.exe'
$objdump  = Join-Path $bin 'objdump.exe'
foreach ($tool in @($gfortran, $gcc, $cmake, $ninja, $strip, $objdump)) {
    if (-not (Test-Path $tool)) { throw "$tool is missing from the toolchain" }
}
# gfortran finds its assembler and linker through PATH
$env:PATH = "$bin;$env:PATH"

# --- OpenBLAS, for the in-core doublet lattice solve and SOL 144 -------------
# Pinned by URL and SHA-256 - the Linux build's pin - and built with the Linux
# build's options (build_nastran95.sh says why each): DYNAMIC_ARCH over the
# PRESCOTT / SANDYBRIDGE / HASWELL / SKYLAKEX kernel sets (SSE3, AVX, AVX2,
# AVX-512) on a GENERIC baseline, so the kernels are chosen at start-up for
# the processor found (a Core Ultra takes HASWELL, a Zen 5 SKYLAKEX) and the
# executable runs on any x86-64; USE_OPENMP, the solver's own libgomp rather
# than a second thread pool; NO_AFFINITY, since the SOL 145 driver runs
# children side by side; static; the Fortran interface and LAPACK only.
# Built with OpenBLAS's own CMake and the toolchain's Ninja, not its
# Makefiles: those need a POSIX shell, and under Git for Windows' sh every
# recipe line is an MSYS fork - measured, 3,700 of the ~9,000 compiles in 50
# minutes. Two CMake spellings differ from make: DYNAMIC_ARCH there puts
# PRESCOTT in front of DYNAMIC_LIST itself (naming it again creates its
# kernel target twice and stops the configure), and -O2 is set as the
# Release flags (CMake's own Release default is -O3; the make build is -O2).
# The prebuilt OpenBLAS-0.3.34-x64.zip is not used: GCC 9.3, pthreads (the
# Win32 thread server beside libgomp), every kernel set.
if (-not (Test-Path $openblasLib)) {
    if (-not (Test-Path $openblasTgz)) {
        Write-Host "Downloading OpenBLAS $OpenBlasVersion..." -ForegroundColor Cyan
        Invoke-WebRequest -Uri $OpenBlasUrl -OutFile $openblasTgz
    }
    $hash = (Get-FileHash -Algorithm SHA256 $openblasTgz).Hash
    if ($hash -ne $OpenBlasSha256) {
        Remove-Item $openblasTgz
        throw "OpenBLAS download hash $hash does not match the pinned $OpenBlasSha256; deleted it, run again"
    }
    Write-Host "Building OpenBLAS $OpenBlasVersion (static, DYNAMIC_ARCH, OpenMP; CMake + Ninja, once)..." -ForegroundColor Cyan
    if (Test-Path $openblasDir) { Remove-Item -Recurse -Force $openblasDir }
    New-Item -ItemType Directory -Force -Path $openblasDir | Out-Null
    & "$env:SystemRoot\System32\tar.exe" -xzf $openblasTgz -C $openblasDir
    if ($LASTEXITCODE -ne 0) { throw "tar failed unpacking $openblasTgz" }
    $obSource = Join-Path $openblasDir "OpenBLAS-$OpenBlasVersion"
    $obBuild = Join-Path $openblasDir 'build'
    $dynamicList = (($OpenBlasCores -split ' ') | Where-Object { $_ -ne 'PRESCOTT' }) -join ';'
    $log = Join-Path $openblasDir 'build.log'
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'   # cmake talks on stderr; judged by exit code
    & $cmake -S $obSource -B $obBuild -G Ninja `
        '-DCMAKE_BUILD_TYPE=Release' "-DCMAKE_C_COMPILER=$($gcc.Replace('\', '/'))" `
        "-DCMAKE_Fortran_COMPILER=$($gfortran.Replace('\', '/'))" "-DCMAKE_MAKE_PROGRAM=$($ninja.Replace('\', '/'))" `
        '-DCMAKE_C_FLAGS_RELEASE=-O2' '-DCMAKE_Fortran_FLAGS_RELEASE=-O2' `
        '-DDYNAMIC_ARCH=ON' '-DTARGET=GENERIC' "-DDYNAMIC_LIST=$dynamicList" `
        '-DUSE_OPENMP=ON' '-DNO_AFFINITY=ON' '-DBUILD_SHARED_LIBS=OFF' '-DBUILD_STATIC_LIBS=ON' `
        '-DBUILD_WITHOUT_CBLAS=ON' '-DNO_LAPACKE=ON' '-DNUM_THREADS=256' '-DBUILD_TESTING=OFF' *> $log
    $configured = $LASTEXITCODE
    if ($configured -eq 0) { & $cmake --build $obBuild --parallel *>> $log }
    $built = $LASTEXITCODE
    $ErrorActionPreference = $saved
    $lib = Get-ChildItem (Join-Path $obBuild 'lib') -Filter 'libopenblas*.a' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($configured -ne 0 -or $built -ne 0 -or -not $lib) {
        Get-Content $log -Tail 30 | ForEach-Object { Write-Host $_ }
        throw "OpenBLAS did not build; see $log"
    }
    Copy-Item $lib.FullName $openblasLib
}

# --- configure and build -----------------------------------------------------
# CMake wants forward slashes in -D values; a backslash in one of these is an
# escape character, not a separator
$g = $gfortran.Replace('\', '/')
$c = $gcc.Replace('\', '/')
$n = $ninja.Replace('\', '/')
Write-Host "Source: $sourceWhere$sourceNote" -ForegroundColor Cyan
Write-Host "Configuring (Debug = -O0, static link, open core $OpenCoreWords words)..." -ForegroundColor Cyan
# Link flags. -static folds the runtime libraries in. The other three pin the
# executable at a fixed, low image base with ASLR off, and that is not
# cosmetic: NASTRAN stores memory addresses in default INTEGERs (LOCFX in
# mds/mapfns.f returns LOC(I)/4 as a 32-bit word address, and KORSZ, the
# open core size, is the difference of two of them). With the default
# high-entropy ASLR the 1 GB open core lands somewhere in a 47-bit space and
# about one boot in seventy has it straddling a 4 GB boundary, where the
# truncated difference wraps and every module is told the wrong amount of
# core. At image base 0x400000 every static address is below 128 MB and the
# arithmetic is exact.
$linkFlags = '-static -Wl,--disable-dynamicbase -Wl,--disable-high-entropy-va -Wl,--image-base,0x400000'
# What `nastran95 --version` prints: enough to trace a copy of the exe found
# on someone's machine back to a commit and a build. The fork caps it at 56
# characters (it becomes a Fortran character constant): the branch less its
# halo-ase- prefix, the short commit (+ when the tree had local changes),
# the compiler and the date
$gfortranVersion = (& $gfortran -dumpversion).Trim()
$label = if ($SourceBranch) { ($SourceBranch -replace '^halo-ase-', '') + ' ' } else { '' }
$buildId = "$label$sha8$(if ($sourceNote) { '+' }), gfortran $gfortranVersion, $(Get-Date -Format 'yyyy-MM-dd')"
& $cmake -S $sourceDir -B $cmakeBuild -G Ninja `
    '-DCMAKE_BUILD_TYPE=Debug' `
    "-DCMAKE_Fortran_COMPILER=$g" `
    "-DCMAKE_C_COMPILER=$c" `
    "-DCMAKE_MAKE_PROGRAM=$n" `
    "-DCMAKE_EXE_LINKER_FLAGS=$linkFlags" `
    "-DNASTRAN_OPEN_CORE_WORDS=$OpenCoreWords" `
    "-DNASTRAN_LAPACK_LIBRARIES=$($openblasLib.Replace('\', '/'))" `
    "-DNASTRAN_BUILD_ID=$buildId"
if ($LASTEXITCODE -ne 0) { throw 'cmake configure failed' }

Write-Host "Building (1,848 FORTRAN files and the C front end)..." -ForegroundColor Cyan
& $cmake --build $cmakeBuild --parallel
if ($LASTEXITCODE -ne 0) { throw 'build failed' }

# --- the two executables: strip, check, install --------------------------------
$targets = @(
    @{ built = 'nastran.exe';      installed = 'nastran95.exe';    role = 'the solver, reading the 1970s input NASA wrote it for' },
    @{ built = 'nastran95ase.exe'; installed = 'nastran95ase.exe'; role = 'the same solver reading the MSC dialect; SOL 144 and SOL 200; MSC-layout print file and OUTPUT4' }
)
$records = @(
    'nastran95.exe and nastran95ase.exe build record (written by standalone\build\build_nastran95.ps1)',
    '',
    "source      $sourceWhere$(if ($SourceBranch) { " (branch $SourceBranch)" })$sourceNote",
    "compiler    $((& $gfortran --version | Select-Object -First 1)); C front end with $((& $gcc --version | Select-Object -First 1))",
    'flags       -O0 -g -std=legacy -fdec -fallow-argument-mismatch -fallow-invalid-boz -fno-automatic -w -nocpp (Fortran); -g -Wall -Wextra at the gcc default of -O0 (C); then strip --strip-all',
    '            except the fenced optimised files CMakeLists.txt lists (-O2 or -O3, -fautomatic -ffp-contract=off, -fopenmp where threaded):',
    '            the doublet lattice kernel, rows and batches over k (AMGK), the PK loops, their QR and eigenvector solve, AMP''s pair loop in core (AMPK),',
    '            the in-core solve, the GINO run copies, the setup inner loops, the aerodynamic cache; the PK and doublet-lattice kernel files also built',
    '            for x86-64-v3 (-Wa,-muse-unaligned-vector-move: GCC bug 54412) and chosen at run time (N95_ISA=0: the baseline),',
    '            and msc\mscaest.c, SOL 144''s dense algebra (-O2 -ffp-contract=off, its solves through the LAPACK below)',
    "lapack      OpenBLAS $OpenBlasVersion (sha256 $($OpenBlasSha256.Substring(0,16).ToLower())...), static, DYNAMIC_ARCH TARGET=GENERIC DYNAMIC_LIST=`"$OpenBlasCores`" USE_OPENMP NO_AFFINITY",
    "link        $linkFlags -fopenmp (libgomp static)",
    "open core   $OpenCoreWords words",
    "built       $(Get-Date -Format 'yyyy-MM-dd HH:mm') on $env:COMPUTERNAME",
    ''
)
if (-not $NoInstall) { New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null }
foreach ($t in $targets) {
    $built = Join-Path $cmakeBuild ("bin\" + $t.built)
    if (-not (Test-Path $built)) { throw "$($t.built) was not built" }
    & $strip --strip-all $built
    if ($LASTEXITCODE -ne 0) { throw "strip failed on $($t.built)" }

    # check it really is standalone
    $imports = @(& $objdump -p $built | Select-String 'DLL Name:' | ForEach-Object {
        ($_ -split 'DLL Name:')[1].Trim() })
    $allowed = @('KERNEL32.dll', 'msvcrt.dll')
    $extra = @($imports | Where-Object { $allowed -notcontains $_ })
    if ($extra.Count -gt 0) {
        throw "$($t.built) imports more than the OS provides: $($extra -join ', ')"
    }
    # and that it starts: --version exercises the static runtime and the
    # start-up code without needing a deck. It prints on stderr, and cmd does
    # the redirection because PowerShell 5.1 turns a native command's stderr
    # into error records, which $ErrorActionPreference = 'Stop' would throw on
    $version = (& "$env:SystemRoot\System32\cmd.exe" /c "`"$built`" --version 2>&1" | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $version -notmatch 'NASTRAN-95') {
        throw "$($t.built) --version did not run: $version"
    }

    $dest = Join-Path $InstallDir $t.installed
    if ($NoInstall) {
        $dest = $built
    } else { try {
        Copy-Item $built $dest -Force
    } catch {
        # a run in progress holds the old executable open, and Windows lets
        # a running executable be renamed but not overwritten: put it aside
        # under a name that says so (gitignored; delete it when the run ends)
        $aside = Join-Path $InstallDir ($t.installed -replace '\.exe$', ('_in_use_{0:yyyyMMdd_HHmm}.exe' -f (Get-Date)))
        Rename-Item $dest $aside
        Copy-Item $built $dest -Force
        Write-Host "$($t.installed) was in use: the old one is now $aside - delete it when the run ends" -ForegroundColor Yellow
    } }
    $exe = Get-Item $dest
    $sha = (Get-FileHash -Algorithm SHA256 $exe.FullName).Hash
    $records += @(
        "$($t.installed)",
        "  role      $($t.role)",
        "  version   $version   (what --version prints)",
        "  imports   $($imports -join ', ')",
        "  size      $($exe.Length) bytes",
        "  sha256    $sha",
        ''
    )
    Write-Host ("Built {0} ({1:N0} bytes), imports {2}" -f $exe.FullName, $exe.Length, ($imports -join ', ')) -ForegroundColor Green
}
if ($NoInstall) {
    $records | ForEach-Object { Write-Host $_ }
    Write-Host 'not installed (-NoInstall): the executables are in the build tree above'
} else {
    $records | Set-Content -Encoding ASCII (Join-Path $InstallDir 'BUILD_INFO.txt')
}

Write-Host ''
Write-Host 'Now run the checks (skill build-nastran95), from the repo root:'
$hint = ''
if ($NoInstall) { $hint = ' --exe "' + (Join-Path $cmakeBuild 'bin\nastran95ase.exe') + '"' }
Write-Host "  python standalone\test\run_decks.py$hint"
Write-Host '  and, with VehicleDesign checked out beside the fork, its MATLAB tests in standalone\test'
