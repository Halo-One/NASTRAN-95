# ---------------------------------------------------------------------------
# NASTRAN-95, built with CMake + Ninja.
#
# Descended from the CMakeLists.txt in https://github.com/Foadsf/NASTRAN-95,
# which was the first working build of this drop; rewritten here for gfortran
# on Windows, for a configurable open core, and for -O0 by default.
#
# Typical use is through nastran95/build.ps1 (or build.sh) in the ai_tools
# repo, which supplies the conda-provided compiler. By hand:
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build
#
# Build types, and why the default is the slow one:
#   Debug   -O0. The reference build. 1970s FORTRAN with a hand-rolled memory
#           manager gives an optimizer plenty of rope, so a result is only
#           trusted once it has been reproduced here.
#   Release -O2, after Debug and Release have been shown to agree across the
#           demo suite. -O3 is not offered; upstream used it, we have not
#           earned it.
# ---------------------------------------------------------------------------
cmake_minimum_required(VERSION 3.20)
project(NASTRAN95 Fortran C)

# Do not run the C preprocessor over this source. Nothing in the tree uses it
# -- the INCLUDE statements are Fortran INCLUDE, not #include -- and running it
# is actively wrong: mis/ascm04.f has the comment "REPLACEING !* BY /* IN RDMAP
# ARRAY", and /* opens a C comment that never closes. CMake would otherwise
# preprocess every file on the way to scanning it for module dependencies,
# which F77 does not have either.
set(CMAKE_Fortran_PREPROCESS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# --- open core -------------------------------------------------------------
# NASTRAN allocates nothing. It declares one COMMON block, ZZZZZZ, and hands
# out slices of it; every module takes "open core" as an argument and indexes
# into it. The size is therefore fixed when the executable is linked, and a
# model that does not fit gets a -8 MESAGE rather than a slow run. NASA
# shipped 14,000,000 words (56 MB).
#
# The array is INTEGER but the same words are EQUIVALENCEd to REAL and to
# DOUBLE PRECISION all over the code, so this is words, not elements of any
# one type, and default kinds must stay at 4 bytes for the aliasing to work.
#
# This is a whole-image static array, so it counts against the 2 GB that the
# default (small) code model addresses with 32-bit displacements. 400M words
# is 1.6 GB and links; much beyond that needs -mcmodel=medium and a linker
# that agrees. The honest fix is to make it ALLOCATABLE, which is a real
# refactor of LOCFX and the DBM address arithmetic, not a flag.
set(NASTRAN_OPEN_CORE_WORDS "14000000" CACHE STRING
    "Open core size in 4-byte words; 14000000 = 56 MB is what NASA shipped")

# --- flags -----------------------------------------------------------------
if(CMAKE_Fortran_COMPILER_ID MATCHES "GNU")
  # -std=legacy               F66/F77 constructs gfortran now rejects by
  #                           default: shared DO termination, Hollerith in
  #                           more places, arithmetic IF on reals.
  # -fdec                     DEC/VMS extensions the UNIVAC and VAX branches
  #                           of this code use.
  # -fallow-argument-mismatch The whole open-core idiom is passing one array
  #                           to routines that declare it INTEGER, REAL or
  #                           DOUBLE PRECISION as it suits them. Without this
  #                           every such call is an error in gfortran 10+.
  # -fallow-invalid-boz       Hollerith and BOZ constants used as data.
  # -fno-automatic            Load-bearing. Code of this era assumes every
  #                           local is static and survives between calls;
  #                           gfortran's default of stack locals silently
  #                           breaks routines that rely on it.
  # -w                        The warning volume from 417k lines of F77 is
  #                           not information. Turn warnings on deliberately
  #                           when chasing something.
  #
  # NOT here, and not to be added: -fdefault-real-8, -fdefault-integer-8 or
  # -freal-4-real-8. They change the size of a word, and open core aliases
  # INTEGER*4, REAL*4 and REAL*8 through the same array; the build succeeds
  # and the answers are garbage.
  set(NASTRAN_FLAGS
      -std=legacy -fdec -fallow-argument-mismatch -fallow-invalid-boz
      -fno-automatic -w)
  set(CMAKE_Fortran_FLAGS_DEBUG "-O0 -g")
  set(CMAKE_Fortran_FLAGS_RELEASE "-O2")
elseif(CMAKE_Fortran_COMPILER_ID MATCHES "Intel")
  # Kept from the Foadsf build so an ifort build stays possible; untested here.
  set(NASTRAN_FLAGS /integer-size:32 /Qdiag-disable:10448 /Qdiag-disable:warn)
  set(CMAKE_Fortran_FLAGS_DEBUG "/Od")
  set(CMAKE_Fortran_FLAGS_RELEASE "/O2")
else()
  message(WARNING "Untested Fortran compiler: ${CMAKE_Fortran_COMPILER_ID}")
  set(NASTRAN_FLAGS "")
endif()

# --- sources ---------------------------------------------------------------
# GLOB rather than a list of 1,848 names, with CONFIGURE_DEPENDS so adding a
# file re-runs configure.
file(GLOB MIS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mis/*.f")
file(GLOB MDS_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/mds/*.f")
file(GLOB BD_SOURCES  CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/bd/*.f")

# mds/nastrn.f is a byte-for-byte copy of bin/nastrn.f, PROGRAM statement and
# all. Leaving it in the library gives two main programs in one link, which
# happens to work only because the linker never has a reason to pull the
# second one in. Exclude it and say so.
list(REMOVE_ITEM MDS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/mds/nastrn.f")
# chkfil is its own standalone program, built separately below.
list(REMOVE_ITEM MDS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/mds/chkfil.f")
# mds/hoswin.f and mds/hosunx.f are the same four operating-system calls
# (make, remove and enter a directory; where TEMP is) against the Windows
# and the POSIX names of the C runtime. One of them is compiled.
if(WIN32)
  list(REMOVE_ITEM MDS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/mds/hosunx.f")
else()
  list(REMOVE_ITEM MDS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/mds/hoswin.f")
endif()

# --- the MSC dialect front end ----------------------------------------------
# msc/ is new in this fork and is not NASA's. It reads the deck MSC Nastran
# would read -- free field, nested INCLUDEs, SOL 103, RBE2, CBUSH, EIGRL --
# and rewrites it as the input this solver was written for, then rewrites the
# print file into the layout MSC prints. It is C because it is string
# handling and lookup tables, and because a CHARACTER in fixed-form Fortran
# is a blank-padded fixed-length thing that fights every line of it.
file(GLOB MSC_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/msc/*.c")
# mscgfwrap.c is linked into each executable directly, not through the
# library: the reference to its __wrap_ symbol only appears when the linker
# reaches libgfortran, after it has finished with this library.
list(REMOVE_ITEM MSC_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/msc/mscgfwrap.c")
set_source_files_properties(${MSC_SOURCES} PROPERTIES LANGUAGE C)

# The main program is generated so open core can be a build option. The
# template is bin/nastrn.f.in; @NASTRAN_OPEN_CORE_WORDS@ appears twice in it,
# once in the COMMON declaration and once in LENOPC, and they must agree.
#
# NASTRAN_BUILD_ID is the other substitution: one short line saying which
# build this is, printed by `nastran --version` and at the top of the usage
# text. Whoever builds a distributable sets it (commit, compiler, date);
# blank means nobody did. It goes into a character constant on a fixed-form
# line, hence the length limit.
set(NASTRAN_BUILD_ID "" CACHE STRING
    "Short build identification printed by `nastran --version`")
string(LENGTH "${NASTRAN_BUILD_ID}" _build_id_length)
if(_build_id_length GREATER 56)
  message(FATAL_ERROR "NASTRAN_BUILD_ID must be 56 characters or fewer")
endif()
if(NASTRAN_BUILD_ID MATCHES "'")
  message(FATAL_ERROR "NASTRAN_BUILD_ID may not contain an apostrophe")
endif()
# @NASTRAN_PATH_SEP@ is the character code of the separator the main
# program joins an output directory to a file name with: 92 on Windows,
# 47 everywhere else. It is substituted rather than tested at run time
# because this Fortran is compiled without the C preprocessor, and a
# wrong one is silent -- the directory becomes part of the file name.
if(WIN32)
  set(NASTRAN_PATH_SEP "92")
else()
  set(NASTRAN_PATH_SEP "47")
endif()

# @NASTRAN_ASE_MODE@ is the other: one template, two main programs. The
# plain one reads the input NASA wrote the solver for; the ASE one reads the
# MSC dialect. Same solver, same arithmetic, different front door.
set(NASTRAN_ASE_MODE ".FALSE.")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/bin/nastrn.f.in"
               "${CMAKE_CURRENT_BINARY_DIR}/nastrn.f" @ONLY)
set(NASTRAN_ASE_MODE ".TRUE.")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/bin/nastrn.f.in"
               "${CMAKE_CURRENT_BINARY_DIR}/nastrnase.f" @ONLY)
set(NASTRAN_ASE_MODE ".FALSE.")

# --- the rigid format library, compiled in ----------------------------------
# NASTRAN reads its rigid formats (rf/, the DMAP for every solution sequence)
# at run time from the directory named RFDIR. Rather than ship rf/ beside
# the executable and hope it is found, utility/hrfgen.f turns every file in
# rf/ into Fortran WRITE statements at build time, and the main program
# writes the library into its scratch directory at start-up (unless RFDIR
# is set, which still wins). Nothing about how RFOPEN reads them changes.
file(GLOB RF_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/rf/*")
list(SORT RF_FILES)
add_executable(hrfgen "${CMAKE_CURRENT_SOURCE_DIR}/utility/hrfgen.f")
target_compile_options(hrfgen PRIVATE ${NASTRAN_FLAGS})
set(HRFLIB_F "${CMAKE_CURRENT_BINARY_DIR}/hrflib.f")
add_custom_command(
  OUTPUT  "${HRFLIB_F}"
  COMMAND hrfgen "${HRFLIB_F}" ${RF_FILES}
  DEPENDS hrfgen ${RF_FILES}
  COMMENT "Generating hrflib.f from rf/"
  VERBATIM)

# INCLUDE 'DSIOF.COM' and friends resolve relative to the including file for
# the tree's own sources, but the generated main program lives in the build
# directory, so give the compiler the include paths explicitly.
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/mds"
                    "${CMAKE_CURRENT_SOURCE_DIR}/mis")

# hrflib.f goes into the library, not just into nastran: HCLEAN (called from
# PEXIT, which everything reaches) references HRFDEL, so any executable that
# links nas needs it, ff included.
add_library(nas STATIC ${MIS_SOURCES} ${MDS_SOURCES} ${BD_SOURCES}
                       ${MSC_SOURCES} "${HRFLIB_F}")
target_compile_options(nas PRIVATE $<$<COMPILE_LANGUAGE:Fortran>:${NASTRAN_FLAGS}>)
# The C is written to C89 so that it builds with whatever is to hand in ten
# years' time, and warnings in it are read, not silenced like the Fortran's.
target_compile_options(nas PRIVATE $<$<COMPILE_LANGUAGE:C>:-Wall;-Wextra>)

# mis/ampcz.f (Halo One, 2026) is the in-core complex solve that replaces the
# out-of-core decomposition of the doublet lattice matrices in AMP. It is new
# code with no static locals and no open-core aliasing of its own, so it is
# the one file in the tree compiled optimised and, when the toolchain has it,
# threaded with OpenMP (the trailing update of the LU and the right hand
# sides). Everything else stays at -O0 for the reasons above. The link is
# static, so libgomp is folded in like libgfortran.
# The doublet lattice kernel is the other place a flutter run spends its
# time: the kernel functions and their integrals (mis/incro.f, tker.f,
# subp.f, subpb.f, idf1.f, idf2.f, snpdf.f, subi.f, akp2.f, asycon.f,
# suba.f, intert.f) and the two routines that assemble the D matrices from
# them (amgb1b.f, amgb1c.f, amgb1d.f). Those are arithmetic on their
# arguments - no GINO, no open core - and are compiled optimised, with the
# tree's other flags (-fno-automatic included) unchanged. The drivers
# around them (amg.f, amgb1.f, amgb1a.f, dlamg.f, ...) write the matrices
# through GINO and stay at -O0: with them optimised the aerodynamic matrix
# came out unusable (a division by zero in the transpose that reads it).
if(CMAKE_Fortran_COMPILER_ID MATCHES "GNU")
  set(DLM_KERNEL_SOURCES "")
  foreach(f incro tker subp subpb idf1 idf2 snpdf subi akp2 asycon suba intert
            amgb1b amgb1c amgb1d)
    list(APPEND DLM_KERNEL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/mis/${f}.f")
  endforeach()
  set_source_files_properties(${DLM_KERNEL_SOURCES} PROPERTIES COMPILE_OPTIONS "-O2")
endif()
# The PK method's own arithmetic (Halo One, 2026): mis/fa1pkq.f holds
# FA1PKA + HSBG + ATEIG and the GMMATS product FA1PKE asks for, rewritten
# without the argument aliasing the originals rely on (one array passed as
# both a REAL input and a DOUBLE PRECISION work area), and mis/egnvct.f is
# the complex eigenvector solve FA1PKT and FA1PKV call for every root. On
# a 120-mode basis the PK loop is nine tenths a 240-square QR per root per
# iteration, and these are where it goes. -fautomatic because they are
# also called from threads (mis/fa1pkp.f), -ffp-contract=off so that no
# toolchain that defaults to FMA can move a bit: they give the -O0 answer
# exactly, which is how they are checked.
# mis/fa1pkp.f (Halo One, 2026) solves the flutter loops of one Mach side
# by side, a thread per loop, with FA1PKE's arithmetic transcribed onto
# arrays of its own, and hands FA1PKE each loop to write in order; it is
# compiled the same way and threaded with OpenMP when the toolchain has it.
if(CMAKE_Fortran_COMPILER_ID MATCHES "GNU")
  set_source_files_properties(
      "${CMAKE_CURRENT_SOURCE_DIR}/mis/fa1pkq.f"
      "${CMAKE_CURRENT_SOURCE_DIR}/mis/egnvct.f"
      PROPERTIES COMPILE_OPTIONS "-O2;-fautomatic;-ffp-contract=off")
  find_package(OpenMP COMPONENTS Fortran)
  if(OpenMP_Fortran_FOUND)
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/mis/fa1pkp.f"
        PROPERTIES COMPILE_OPTIONS
        "-O2;-fautomatic;-ffp-contract=off;${OpenMP_Fortran_FLAGS}")
  else()
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/mis/fa1pkp.f"
        PROPERTIES COMPILE_OPTIONS "-O2;-fautomatic;-ffp-contract=off")
  endif()
endif()
if(CMAKE_Fortran_COMPILER_ID MATCHES "GNU")
  find_package(OpenMP COMPONENTS Fortran)
  if(OpenMP_Fortran_FOUND)
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/mis/ampcz.f"
        PROPERTIES COMPILE_OPTIONS "-O2;-fautomatic;${OpenMP_Fortran_FLAGS}")
    # -fopenmp at link time, not the imported target: that names the
    # toolchain's libgomp.dll.a and the executable then wants libgomp-1.dll;
    # with -static on the link, -fopenmp resolves -lgomp to libgomp.a
    target_link_options(nas PUBLIC ${OpenMP_Fortran_FLAGS})
  else()
    set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/mis/ampcz.f"
        PROPERTIES COMPILE_OPTIONS "-O2;-fautomatic")
  endif()
endif()

# bd/semdbd.f is NOT excluded, although the upstream CMake build excludes it.
# It is the only BLOCK DATA in the tree that initialises /XMSSG/ (the "0***
# USER FATAL MESSAGE" prefixes), /SEM/, /SYSTEM/, /XFIAT/ and /OSCENT/, and
# without it the executable links and then runs on zeroed control blocks. There
# is no second copy: bd/ has no sembd.f, whatever the name suggests.

# --- executables -----------------------------------------------------------
# Block data are only linked in if something references them, and nothing
# does -- they exist to initialise COMMON. Naming the objects on the
# executable's own source list is the portable way to force them in.
add_executable(nastran "${CMAKE_CURRENT_BINARY_DIR}/nastrn.f" ${BD_SOURCES}
                       "${CMAKE_CURRENT_SOURCE_DIR}/msc/mscgfwrap.c")
target_compile_options(nastran PRIVATE ${NASTRAN_FLAGS})
target_link_libraries(nastran PRIVATE nas)

# nastran95ase: the same solver reading the MSC dialect.
add_executable(nastranase "${CMAKE_CURRENT_BINARY_DIR}/nastrnase.f" ${BD_SOURCES}
                          "${CMAKE_CURRENT_SOURCE_DIR}/msc/mscgfwrap.c")
target_compile_options(nastranase PRIVATE ${NASTRAN_FLAGS})
target_link_libraries(nastranase PRIVATE nas)
set_target_properties(nastranase PROPERTIES OUTPUT_NAME "nastran95ase")

add_executable(nasthelp "${CMAKE_CURRENT_SOURCE_DIR}/um/nasthelp.f")
target_compile_options(nasthelp PRIVATE ${NASTRAN_FLAGS})

add_executable(nastplot "${CMAKE_CURRENT_SOURCE_DIR}/utility/nastplot.f")
target_compile_options(nastplot PRIVATE ${NASTRAN_FLAGS})

add_executable(ff "${CMAKE_CURRENT_SOURCE_DIR}/utility/ff.f"
                  "${CMAKE_CURRENT_SOURCE_DIR}/msc/mscgfwrap.c")
target_compile_options(ff PRIVATE ${NASTRAN_FLAGS})
target_link_libraries(ff PRIVATE nas)

add_executable(chkfil "${CMAKE_CURRENT_SOURCE_DIR}/mds/chkfil.f")
target_compile_options(chkfil PRIVATE ${NASTRAN_FLAGS})

set_target_properties(nastran nastranase nasthelp nastplot ff chkfil PROPERTIES
                      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# --- standalone executables -------------------------------------------------
# Link the compiler's own runtime in, so the result is a file you can copy to
# another machine and run.
#
# Without this, nastran.exe imports libgfortran-5.dll and libgcc_s_seh-1.dll
# out of the conda environment it was built in. Anyone who copies the
# executable somewhere else -- which is the obvious thing to do with a solver,
# and what someone driving it from a flutter workflow will do -- gets
#
#     The code execution cannot proceed because libgfortran-5.dll was not found
#
# from Windows itself, before a line of Fortran has run. Everything else it
# imports is api-ms-win-crt-*, which is the Windows Universal CRT and is part
# of the operating system on Windows 10 and later.
#
# The cost is about 1 MB on an 18 MB binary and giving up the ability to
# upgrade libgfortran underneath it, which is not a thing anyone wants to do
# to a solver they have validated.
option(NASTRAN_STATIC_RUNTIME
       "Link libgfortran/libgcc statically so the executables stand alone" ON)
#
# On Linux the same flag folds glibc in as well, and the result runs on any
# distribution without so much as a libgfortran installed. Apple ships no
# static libSystem, so there -static cannot link at all and only the
# compiler's own runtime is folded in; libSystem is on every Mac anyway.
if(NASTRAN_STATIC_RUNTIME AND CMAKE_Fortran_COMPILER_ID MATCHES "GNU")
  if(APPLE)
    set(NASTRAN_STATIC_FLAGS -static-libgfortran -static-libgcc)
  else()
    set(NASTRAN_STATIC_FLAGS -static -static-libgfortran -static-libgcc)
  endif()
  foreach(target nastran nastranase nasthelp nastplot ff chkfil hrfgen)
    target_link_options(${target} PRIVATE ${NASTRAN_STATIC_FLAGS})
  endforeach()
endif()

# --- addresses have to stay inside 32 bits (ELF) ----------------------------
# The Windows link pins the image base at 0x400000 and turns ASLR off. This
# is the same requirement spelled for ELF, and it is no more cosmetic here:
# LOCFX (mds/mapfns.f) returns LOC(I)/4 into a default INTEGER and KORSZ
# subtracts two of those, so open core -- a 256 MB static array -- has to sit
# below 2 GB. A position-independent executable is loaded at 0x55... on
# Linux, where that subtraction is nonsense and every module is handed the
# wrong amount of core. -no-pie puts the image back at 0x400000, which is
# where the PE build sits. The honest fix is still to make open core
# ALLOCATABLE and rework LOCFX, and it is still a refactor, not a flag.
if(UNIX AND NOT APPLE AND CMAKE_Fortran_COMPILER_ID MATCHES "GNU")
  foreach(target nas nastran nastranase nasthelp nastplot ff chkfil hrfgen)
    target_compile_options(${target} PRIVATE -fno-pie)
  endforeach()
  foreach(target nastran nastranase nasthelp nastplot ff chkfil hrfgen)
    target_link_options(${target} PRIVATE -no-pie)
  endforeach()
endif()

# --- the libgfortran format-copy workaround ---------------------------------
# libgfortran copies a run-time format string with strndup and then hashes
# the copy for the format's full declared length. NASTRAN's formats are
# built in zero-filled INTEGER arrays, so the copy is short and the hash
# reads past it - a crash or not depending on where the heap block landed
# (NASA demos d03021a, d03031a, d07021a, d07022a). msc/mscgfwrap.c is a
# full-length copy; --wrap routes the runtime's own calls to it. Every
# executable that links the library (which holds msc/) gets it.
if(CMAKE_Fortran_COMPILER_ID MATCHES "GNU")
  foreach(target nastran nastranase ff)
    target_link_options(${target} PRIVATE
                        "-Wl,--wrap=_gfortrani_fc_strdup_notrim")
  endforeach()
endif()

install(TARGETS nastran nastranase nasthelp nastplot ff chkfil RUNTIME DESTINATION bin)
