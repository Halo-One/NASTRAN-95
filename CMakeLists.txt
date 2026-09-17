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
project(NASTRAN95 Fortran)

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

# The main program is generated so open core can be a build option. The
# template is bin/nastrn.f.in; @NASTRAN_OPEN_CORE_WORDS@ appears twice in it,
# once in the COMMON declaration and once in LENOPC, and they must agree.
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/bin/nastrn.f.in"
               "${CMAKE_CURRENT_BINARY_DIR}/nastrn.f" @ONLY)

# INCLUDE 'DSIOF.COM' and friends resolve relative to the including file for
# the tree's own sources, but the generated main program lives in the build
# directory, so give the compiler the include paths explicitly.
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/mds"
                    "${CMAKE_CURRENT_SOURCE_DIR}/mis")

add_library(nas STATIC ${MIS_SOURCES} ${MDS_SOURCES} ${BD_SOURCES})
target_compile_options(nas PRIVATE ${NASTRAN_FLAGS})

# bd/semdbd.f is NOT excluded, although the upstream CMake build excludes it.
# It is the only BLOCK DATA in the tree that initialises /XMSSG/ (the "0***
# USER FATAL MESSAGE" prefixes), /SEM/, /SYSTEM/, /XFIAT/ and /OSCENT/, and
# without it the executable links and then runs on zeroed control blocks. There
# is no second copy: bd/ has no sembd.f, whatever the name suggests.

# --- executables -----------------------------------------------------------
# Block data are only linked in if something references them, and nothing
# does -- they exist to initialise COMMON. Naming the objects on the
# executable's own source list is the portable way to force them in.
add_executable(nastran "${CMAKE_CURRENT_BINARY_DIR}/nastrn.f" ${BD_SOURCES})
target_compile_options(nastran PRIVATE ${NASTRAN_FLAGS})
target_link_libraries(nastran PRIVATE nas)

add_executable(nasthelp "${CMAKE_CURRENT_SOURCE_DIR}/um/nasthelp.f")
target_compile_options(nasthelp PRIVATE ${NASTRAN_FLAGS})

add_executable(nastplot "${CMAKE_CURRENT_SOURCE_DIR}/utility/nastplot.f")
target_compile_options(nastplot PRIVATE ${NASTRAN_FLAGS})

add_executable(ff "${CMAKE_CURRENT_SOURCE_DIR}/utility/ff.f")
target_compile_options(ff PRIVATE ${NASTRAN_FLAGS})
target_link_libraries(ff PRIVATE nas)

add_executable(chkfil "${CMAKE_CURRENT_SOURCE_DIR}/mds/chkfil.f")
target_compile_options(chkfil PRIVATE ${NASTRAN_FLAGS})

set_target_properties(nastran nasthelp nastplot ff chkfil PROPERTIES
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
if(NASTRAN_STATIC_RUNTIME AND CMAKE_Fortran_COMPILER_ID MATCHES "GNU")
  foreach(target nastran nasthelp nastplot ff chkfil)
    target_link_options(${target} PRIVATE
                        -static -static-libgfortran -static-libgcc)
  endforeach()
endif()

install(TARGETS nastran nasthelp nastplot ff chkfil RUNTIME DESTINATION bin)
