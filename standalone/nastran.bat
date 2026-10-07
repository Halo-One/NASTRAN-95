@echo off
rem  A stand-in for the licensed solver's launcher (Simcenter Nastran's, MSC's), so
rem  that scripts which call `nastran <deck>` run on the licence-free solver instead.
rem
rem  Nothing uses it unless you ask for it. To switch this shell over:
rem
rem      set PATH=%~dp0;%PATH%
rem
rem  or, permanently, put this folder AHEAD of the licensed solver's bin directory
rem  on your PATH. To switch back, take it off again.
rem
rem  It works because nastran95ase.exe takes the same command line the licensed
rem  launcher takes - `nastran deck.dat [out=dir]`, plus scr=/bat=/old=/append=
rem  accepted and ignored - reads the same MSC-dialect deck, and writes the print
rem  file as <stem>.out beside the deck, in MSC's layout.
rem
rem  Read README.md in this folder, "The MSC dialect", before trusting a result:
rem  this is the 1995 COSMIC solver with a translator in front of it, not
rem  Simcenter Nastran, and <stem>_xlat.txt records every substitution it made.
rem
rem  It says so on every run, deliberately: someone with a licensed seat who put
rem  this folder on the PATH by accident should not be able to mistake one solver
rem  for the other.

echo [nastran.bat] NASTRAN-95 standalone (nastran95ase.exe), not Simcenter Nastran.
echo [nastran.bat] Translation log: ^<stem^>_xlat.txt.  See standalone\README.md.
"%~dp0nastran95ase.exe" %*
