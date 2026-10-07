#!/usr/bin/env bash
# build_nastran95.sh - build nastran95 and nastran95ase, the two standalone
# Linux executables, from this checkout.
#
#   bash standalone/build/build_nastran95.sh
#   ... --source <dir>       another local source tree instead of this checkout
#   ... --commit <sha>       that commit of Halo-One/NASTRAN-95, downloaded from
#                            GitHub, instead of any local tree: a release build,
#                            whose record names a commit anyone can fetch
#   ... --branch <name>      with --commit: the branch it is on, for the record
#                            and the --version label (a local tree's is read
#                            from git)
#   ... --install-dir <dir>  where the executables and BUILD_INFO_linux.txt go
#                            (default: standalone/, beside this build folder)
#   ... --work-dir <dir>     where OpenBLAS and the build trees live
#                            (default: standalone/build/work, gitignored)
#   ... --no-install         build and check, but leave the executables in the
#                            build tree (a candidate under test)
#   ... --clean              throw the work directory away first
#
#   NASTRAN_BUILD_JOBS=4 bash standalone/build/build_nastran95.sh     fewer compile jobs on a busy machine
#
# This is the Linux counterpart of build_nastran95.ps1, which builds the two
# .exe files for Windows. Read that script's header first: what the source
# is, why -O0, why open core is 256,000,000 words, why the link is static and
# what the rigid format library is doing inside the executable are all the
# same here and are all explained there. The skill
# .claude/skills/build-nastran95 walks through both.
#
# What is different about the Linux build, and only that:
#
#   * The compiler is the one on the machine, not a pinned download. The
#     Windows script pins winlibs GCC by URL and SHA-256 because Windows
#     ships no compiler at all; a Linux box has gfortran from its own
#     package manager, under its distribution's support. The version and
#     flags are recorded in BUILD_INFO_linux.txt instead of pinned, so a
#     build is traceable even though it is not byte-reproducible across
#     machines the way the Windows one is. gfortran 13 or newer.
#
#         sudo apt install gfortran make cmake ninja-build       # Debian, Ubuntu
#         sudo dnf install gcc-gfortran make cmake ninja-build   # Fedora, RHEL
#
#   * OpenBLAS, built here from its pinned release (URL and SHA-256, as
#     the Windows script pins its compiler), statically, for the in-core
#     solve of the doublet lattice matrix (ZGETRF + ZGETRS instead of the
#     unblocked LU NASA wrote; N95_AJJ_SOLVE=builtin at run time gives that
#     LU back) and SOL 144's dense algebra. DYNAMIC_ARCH: one executable
#     carries kernels for every x86-64 generation and picks them when it
#     starts, so it still runs on any 64-bit Linux; OpenMP threading, the
#     same libgomp as the solver's own threads. It is built once into the
#     work directory and reused.
#
#   * -no-pie, and -fno-pie on the way in. This is the ELF spelling of the
#     three PE flags the Windows link uses (--disable-dynamicbase,
#     --disable-high-entropy-va, --image-base 0x400000) and it matters for
#     exactly the same reason: LOCFX (mds/mapfns.f) returns an address as
#     LOC(I)/4 in a default INTEGER, so open core -- a 1 GB static array
#     -- has to live below 2 GB. Linux loads a position-independent
#     executable at 0x55... where that arithmetic is nonsense. The flags
#     are in CMakeLists.txt, not here; the check at the end of this script
#     proves the result came out where it should.
#
#   * -static folds glibc in as well as libgfortran, so the executables
#     run on any 64-bit Linux, not only on the distribution they were
#     built on. That is what `ldd` reporting "not a dynamic executable"
#     below is confirming.
#
# The two executables land in the install directory with no extension --
# nastran95 and nastran95ase -- beside any Windows .exe files there, which
# are not touched. The build record is BUILD_INFO_linux.txt, so that neither
# build overwrites the other's.
#
# Run time is a few minutes (about 90 seconds of it on 32 cores, and two
# more the first time, for OpenBLAS).

set -euo pipefail

# --- what is pinned ----------------------------------------------------------
SourceRepo='Halo-One/NASTRAN-95'   # where --commit downloads from
OpenBlasVersion='0.3.34'
OpenBlasUrl="https://github.com/OpenMathLib/OpenBLAS/releases/download/v${OpenBlasVersion}/OpenBLAS-${OpenBlasVersion}.tar.gz"
OpenBlasSha256='cd7e129868320cc2d033afa920e31202dfe0b8066a5b66661900ccc0f197dfed'
OpenBlasCores='PRESCOTT SANDYBRIDGE HASWELL SKYLAKEX'   # SSE3, AVX, AVX2+FMA, AVX-512
OpenCoreWords='256000000'   # 1 GB: a static array, costs nothing until used; below 2 GB with -no-pie
MinGfortran=13

# --- arguments ---------------------------------------------------------------
usage() { sed -n '2,/^$/p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; }
clean=0; no_install=0; source_arg=''; commit=''; branch=''; install_dir=''; work_dir=''
while [ $# -gt 0 ]; do
    case "$1" in
        --clean)        clean=1 ;;
        --no-install)   no_install=1 ;;
        --source)       source_arg="$2"; shift ;;
        --source=*)     source_arg="${1#*=}" ;;
        --commit)       commit="$2"; shift ;;
        --commit=*)     commit="${1#*=}" ;;
        --branch)       branch="$2"; shift ;;
        --branch=*)     branch="${1#*=}" ;;
        --install-dir)  install_dir="$2"; shift ;;
        --install-dir=*) install_dir="${1#*=}" ;;
        --work-dir)     work_dir="$2"; shift ;;
        --work-dir=*)   work_dir="${1#*=}" ;;
        -h|--help)      usage; exit 0 ;;
        *) echo "build_nastran95.sh: unknown argument $1" >&2; usage >&2; exit 1 ;;
    esac
    shift
done
if [ -n "$commit" ] && [ -n "$source_arg" ]; then
    echo "build_nastran95.sh: --source and --commit exclude each other" >&2; exit 1
fi

# --- layout ------------------------------------------------------------------
build_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"     # standalone/build
repo_root="$(dirname "$(dirname "$build_dir")")"
install_dir="${install_dir:-$(dirname "$build_dir")}"         # standalone/
work="${work_dir:-$build_dir/work}"
if [ "$clean" = 1 ]; then rm -rf "$work"; fi
mkdir -p "$work" "$install_dir"
work="$(cd "$work" && pwd)"
install_dir="$(cd "$install_dir" && pwd)"

openblas_tgz="$work/OpenBLAS-${OpenBlasVersion}.tar.gz"
openblas_dir="$work/OpenBLAS-${OpenBlasVersion}-static-omp-$(echo "$OpenBlasCores" | tr ' ' '-' | tr 'A-Z' 'a-z')"
openblas_lib="$openblas_dir/libopenblas.a"

# --- the tools ---------------------------------------------------------------
for tool in gfortran gcc make cmake ninja curl unzip tar perl strip readelf sha256sum; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "build_nastran95.sh: $tool is not installed." >&2
        echo "  Debian/Ubuntu: sudo apt install gfortran make cmake ninja-build curl unzip perl binutils" >&2
        echo "  Fedora/RHEL:   sudo dnf install gcc-gfortran make cmake ninja-build curl unzip perl binutils" >&2
        exit 1
    }
done
# -dumpversion is the major number alone on Debian and Ubuntu, so ask for
# the full one first; what goes in the build id is what `--version` prints
gfortran_version="$(gfortran -dumpfullversion 2>/dev/null || gfortran -dumpversion)"
if [ "${gfortran_version%%.*}" -lt "$MinGfortran" ]; then
    echo "build_nastran95.sh: gfortran ${gfortran_version} is older than ${MinGfortran}." >&2
    echo "  The dialect flags this code needs (-fdec, -fallow-argument-mismatch," >&2
    echo "  -fallow-invalid-boz) are not all there before then." >&2
    exit 1
fi

# --- the source: this checkout, another tree, or a commit of the fork --------
source_note=''; source_branch="$branch"; source_where=''
if [ -n "$commit" ]; then
    source_commit="$(echo "$commit" | tr 'A-F' 'a-f')"
    case "$source_commit" in
        *[!0-9a-f]*|?????????????????????????????????????????*|???????????????????????????????????????) ;;
    esac
    if [ "${#source_commit}" -ne 40 ]; then
        echo "build_nastran95.sh: --commit wants the full 40-character SHA, so that the record is unambiguous" >&2; exit 1
    fi
    source_zip="$work/nastran-95-src-${source_commit:0:8}.zip"
    # the unpacked tree gets a name of its own rather than the archive's, so
    # that it never shares a directory with a tree the Windows script unpacked
    source_dir="$work/NASTRAN-95-${source_commit}-linux"
    cmake_build="$work/cmake-build-linux-${source_commit:0:8}"
    source_where="$SourceRepo @ $source_commit"
    if [ ! -f "$source_dir/CMakeLists.txt" ]; then
        if [ ! -f "$source_zip" ]; then
            echo "Downloading ${SourceRepo} at ${source_commit:0:8}..."
            curl -sSL -o "$source_zip" "https://github.com/${SourceRepo}/archive/${source_commit}.zip"
        fi
        echo "Unpacking the source..."
        rm -rf "$source_dir" "$work/NASTRAN-95-${source_commit}"
        unzip -q "$source_zip" -d "$work"
        mv "$work/NASTRAN-95-${source_commit}" "$source_dir"
        [ -f "$source_dir/CMakeLists.txt" ] || {
            echo "build_nastran95.sh: expected the source under $source_dir" >&2; exit 1; }
    fi
else
    # a local tree: its HEAD names the build, and local changes are said
    if [ -n "$source_arg" ]; then
        source_dir="$(cd "$source_arg" && pwd)"
        cmake_build="$work/cmake-build-linux-src-$(basename "$source_dir")"
    else
        source_dir="$repo_root"
        cmake_build="$work/cmake-build-linux-local"
    fi
    [ -f "$source_dir/CMakeLists.txt" ] || { echo "build_nastran95.sh: $source_dir has no CMakeLists.txt" >&2; exit 1; }
    source_commit='unknown'; remote=''
    if head="$(git -C "$source_dir" rev-parse HEAD 2>/dev/null)"; then
        source_commit="$head"
        if [ -z "$source_branch" ]; then
            source_branch="$(git -C "$source_dir" rev-parse --abbrev-ref HEAD)"
            [ "$source_branch" = HEAD ] && source_branch='detached'
        fi
        [ -n "$(git -C "$source_dir" status --porcelain --untracked-files=no)" ] && source_note=' + local changes'
        remote="$(git -C "$source_dir" remote get-url origin 2>/dev/null || true)"
    fi
    source_where="$source_dir @ $source_commit"
    [ -n "$remote" ] && source_where="$source_where (origin $remote)"
fi
sha8="${source_commit:0:8}"

# --- OpenBLAS, for the in-core doublet lattice solve and SOL 144 -------------
# Pinned by URL and SHA-256. The options, and why:
#   DYNAMIC_ARCH=1 TARGET=GENERIC  kernels chosen at start-up for the processor
#                                  found; the code around them built for the
#   DYNAMIC_LIST=$OpenBlasCores    x86-64 baseline, so the executable runs on any
#                                  64-bit Linux. The list is one kernel set per
#                                  instruction set level - SSE3, AVX, AVX2+FMA,
#                                  AVX-512 - and OpenBLAS maps every other core to
#                                  the nearest below (Zen to HASWELL, Zen 4/5,
#                                  Cooper Lake and Sapphire Rapids to SKYLAKEX):
#                                  the same ZGETRF speed on a Zen 5, and each
#                                  executable 19 MB instead of 33 MB with every
#                                  core's full kernel table
#   USE_OPENMP=1                   threads from the same libgomp as the solver
#                                  (a pthreads OpenBLAS would bring a second
#                                  pool and fight it). In a static executable
#                                  it has to be told its thread count - the
#                                  fork does (mis/ampczs/ampczt_openblas.f)
#   NO_AFFINITY=1                  no thread pinning: the SOL 145 driver runs
#                                  several children side by side
#   NO_SHARED NO_CBLAS NO_LAPACKE  the static Fortran library, nothing else
# The two make targets run one after the other, never together: under -j,
# 'make libs netlib' has two ar processes writing one archive at once and
# leaves it corrupt ("file format not recognized").
if [ ! -f "$openblas_lib" ]; then
    if [ ! -f "$openblas_tgz" ]; then
        echo "Downloading OpenBLAS ${OpenBlasVersion}..."
        curl -sSL -o "$openblas_tgz" "$OpenBlasUrl"
    fi
    got="$(sha256sum "$openblas_tgz" | cut -c1-64)"
    if [ "$got" != "$OpenBlasSha256" ]; then
        echo "build_nastran95.sh: OpenBLAS-${OpenBlasVersion}.tar.gz has SHA-256 $got," >&2
        echo "  not the pinned $OpenBlasSha256; not using it." >&2
        rm -f "$openblas_tgz"; exit 1
    fi
    echo "Building OpenBLAS ${OpenBlasVersion} (static, DYNAMIC_ARCH, OpenMP; a couple of minutes, once)..."
    rm -rf "$openblas_dir" "$work/OpenBLAS-${OpenBlasVersion}"
    tar xzf "$openblas_tgz" -C "$work"
    mv "$work/OpenBLAS-${OpenBlasVersion}" "$openblas_dir"
    openblas_opts=(DYNAMIC_ARCH=1 TARGET=GENERIC "DYNAMIC_LIST=$OpenBlasCores"
                   USE_OPENMP=1 NO_AFFINITY=1
                   NO_SHARED=1 NO_CBLAS=1 NO_LAPACKE=1 NUM_THREADS=256
                   CC=gcc FC=gfortran)
    make -C "$openblas_dir" -j"$(nproc)" "${openblas_opts[@]}" libs   > "$openblas_dir/build.log" 2>&1
    make -C "$openblas_dir" -j"$(nproc)" "${openblas_opts[@]}" netlib >> "$openblas_dir/build.log" 2>&1
    [ -f "$openblas_lib" ] || {
        echo "build_nastran95.sh: OpenBLAS did not build; see $openblas_dir/build.log" >&2; exit 1; }
fi

# --- configure and build -----------------------------------------------------
# What `nastran95 --version` prints: enough to trace a copy found on someone's
# machine back to a commit and a build. The fork caps it at 56 characters (it
# becomes a Fortran character constant): the branch less its halo-ase- prefix,
# the short commit (+ when the tree had local changes), the compiler, the date
label=''; [ -n "$source_branch" ] && label="${source_branch#halo-ase-} "
plus=''; [ -n "$source_note" ] && plus='+'
build_id="${label}${sha8}${plus}, gfortran ${gfortran_version}, $(date +%Y-%m-%d)"
echo "Source: ${source_where}${source_note}"
echo "Configuring (Debug = -O0, static link, open core ${OpenCoreWords} words, OpenBLAS ${OpenBlasVersion})..."
cmake -S "$source_dir" -B "$cmake_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_Fortran_COMPILER="$(command -v gfortran)" \
    -DCMAKE_C_COMPILER="$(command -v gcc)" \
    -DCMAKE_MAKE_PROGRAM="$(command -v ninja)" \
    -DNASTRAN_OPEN_CORE_WORDS="$OpenCoreWords" \
    -DNASTRAN_LAPACK_LIBRARIES="$openblas_lib" \
    -DNASTRAN_BUILD_ID="$build_id" >/dev/null

echo "Building (1,848 FORTRAN files and the C front end)..."
cmake --build "$cmake_build" --parallel "${NASTRAN_BUILD_JOBS:-$(nproc)}"

# --- the two executables: strip, check, install ------------------------------
record="$install_dir/BUILD_INFO_linux.txt"
record_lines() {
    echo 'nastran95 and nastran95ase (Linux) build record (written by standalone/build/build_nastran95.sh)'
    echo ''
    echo "source      ${source_where}${source_branch:+ (branch $source_branch)}${source_note}"
    echo "compiler    $(gfortran --version | head -1); C front end with $(gcc --version | head -1)"
    echo 'flags       -O0 -g -std=legacy -fdec -fallow-argument-mismatch -fallow-invalid-boz -fno-automatic -w -fno-pie -nocpp (Fortran); -g -Wall -Wextra -fno-pie at the gcc default of -O0 (C); then strip --strip-all'
    echo '            except the fenced optimised files CMakeLists.txt lists (-O2 or -O3, -fautomatic -ffp-contract=off, -fopenmp where threaded):'
    echo '            the doublet lattice kernel, rows and batches over k (AMGK), the PK loops, their QR and eigenvector solve,'
    echo '            AMP pair loop in core (AMPK), the in-core solve, the GINO run copies, the setup inner loops; the PK and'
    echo '            doublet-lattice kernel files also built for x86-64-v3 and chosen at run time (N95_ISA=0: the baseline)'
    echo '            and msc/mscaest.c, SOL 144'"'"'s dense algebra (-O2 -ffp-contract=off, its solves through the LAPACK below)'
    echo "lapack      OpenBLAS ${OpenBlasVersion} (sha256 ${OpenBlasSha256:0:16}...), static, DYNAMIC_ARCH TARGET=GENERIC DYNAMIC_LIST=\"$OpenBlasCores\" USE_OPENMP NO_AFFINITY"
    echo 'link        -static -static-libgfortran -static-libgcc -no-pie -fopenmp (libgomp static)'
    echo "open core   $OpenCoreWords words"
    echo "built       $(date '+%Y-%m-%d %H:%M') on $(uname -n) ($(uname -sr))"
    echo ''
}
if [ "$no_install" = 1 ]; then record="$cmake_build/BUILD_INFO_linux.txt"; fi
record_lines > "$record"

install_one() {
    built="$cmake_build/bin/$1"; installed="$2"; role="$3"
    [ -f "$built" ] || { echo "build_nastran95.sh: $1 was not built" >&2; exit 1; }
    # strip a copy, not the build tree's own output: ninja tracks that file
    # and rewriting it behind ninja's back upsets the next incremental build
    # (its Fortran dyndep plan asserts). The build tree keeps its symbols,
    # which is what you want when something needs a debugger anyway.
    if [ "$no_install" = 1 ]; then
        dest="$cmake_build/bin/$installed"
    else
        dest="$install_dir/$installed"
    fi
    cp -f "$built" "$dest"
    strip --strip-all "$dest"
    built="$dest"

    # it stands alone: nothing to load at run time, so it runs on a
    # distribution that has never heard of gfortran
    if readelf -lW "$built" | grep -q INTERP; then
        echo "build_nastran95.sh: $1 wants a dynamic loader; the static link did not take" >&2
        exit 1
    fi
    # and it is at a fixed, low address: see the -no-pie note at the top.
    # This checks the thing that actually matters -- that the whole image,
    # open core included, is addressable in 32 bits -- rather than the flag
    elf_type="$(readelf -hW "$built" | awk '/^  Type:/ {print $2}')"
    # the top of the image: the highest virtual address any LOAD segment
    # reaches. Arithmetic in the shell rather than in awk, because mawk (the
    # default awk on Debian and Ubuntu) has no strtonum for the hex
    top=0
    while read -r vaddr memsz; do
        reach=$(( vaddr + memsz ))
        [ "$reach" -gt "$top" ] && top=$reach
    done < <(readelf -lW "$built" | awk '$1=="LOAD" {print $3, $6}')
    if [ "$elf_type" != 'EXEC' ] || [ "$top" -ge 2147483648 ]; then
        echo "build_nastran95.sh: $1 is $elf_type and reaches address $top; open core has to" >&2
        echo "  sit below 2 GB for LOCFX's 32-bit word addresses (-no-pie)" >&2
        exit 1
    fi

    # and it starts: --version exercises the static runtime and the start-up
    # code without needing a deck
    version="$("$built" --version 2>&1 | head -1)"
    case "$version" in
        NASTRAN-95*) ;;
        *) echo "build_nastran95.sh: $1 --version did not run: $version" >&2; exit 1 ;;
    esac

    size="$(stat -c%s "$dest")"
    sha="$(sha256sum "$dest" | cut -c1-64 | tr 'a-f' 'A-F')"
    {
        echo "$installed"
        echo "  role      $role"
        echo "  version   $version   (what --version prints)"
        echo "  links     $(ldd "$dest" 2>&1 | head -1 | sed 's/^[[:space:]]*//')"
        echo "  image     $elf_type, top of image at $(printf '0x%x' "$top")"
        echo "  size      $size bytes"
        echo "  sha256    $sha"
        echo ''
    } >> "$record"
    printf 'Built %s (%s bytes), statically linked, %s\n' "$dest" "$size" "$elf_type"
}

install_one nastran      nastran95    'the solver, reading the 1970s input NASA wrote it for'
install_one nastran95ase nastran95ase 'the same solver reading the MSC dialect; SOL 144 and SOL 200; MSC-layout print file and OUTPUT4'
if [ "$no_install" = 1 ]; then
    cat "$record"
    echo "not installed (--no-install): the executables are in $cmake_build/bin"
fi

echo ''
echo "Now run the checks (skill build-nastran95), from the repo root:"
if [ "$no_install" = 1 ]; then
    echo "  python3 standalone/test/run_decks.py --exe $cmake_build/bin/nastran95ase"
else
    echo "  python3 standalone/test/run_decks.py"
fi
echo "  and, with VehicleDesign checked out beside the fork, its MATLAB tests in standalone/test"
