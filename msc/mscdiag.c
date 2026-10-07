/* HALO: what the solver's own fatal messages mean, and what to do.
 *
 * NASTRAN-95 says "USER FATAL MESSAGE 3097, SYMMETRIC DECOMPOSITION OF
 * DATA BLOCK KLL ABORTED BECAUSE THE FOLLOWING COLUMNS ARE SINGULAR"
 * and then a table of internal column numbers, and stops. That is a
 * complete diagnosis to someone who has read the 1972 Programmer's
 * Manual and nothing at all to anyone else. MSC's message catalogue
 * (util/analysis.txt in its installation, 8,655 entries) pairs every
 * number with a "User information" paragraph saying what the condition
 * usually is and what usually fixes it; this file does the same for the
 * messages this solver actually produces, in the same three-part shape
 * the front end uses everywhere: what happened, where, what to do.
 *
 * It runs on the way out, in both executables, whenever the print file
 * carries a fatal: the first fatal message is repeated on the terminal
 * (so that nobody has to open a 20 MB print file to learn that a grid
 * was missing) followed by the explanation for its number, when there
 * is one. Numbers without an entry are repeated without comment; the
 * table grows as messages are met.
 *
 * The numbers are NASA's, not MSC's: the two catalogues diverged in the
 * 1970s and share almost none.
 */
#include "msc.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *num;        /* as printed, e.g. "3097" or "2101A"       */
    const char *what;
    const char *fix;
} diag_entry;

static const diag_entry catalogue[] = {
    { "300",
      "A number or word on a card could not be read. In bulk data the "
      "field is underlined in the print file with the reason beneath. On "
      "an executive control, case control or NASTRAN card the card is "
      "quoted after ERROR IN XRCARD ROUTINE, and a blank line ahead of "
      "CEND is quoted empty and reported as OUTPUT BUFFER TOO SMALL.",
      "Fix the underlined field: two decimal points, a dangling exponent "
      "or an embedded blank (a number wider than eight columns spills "
      "into the next field). For the XRCARD form correct the quoted card "
      "and delete blank lines between the first card and CEND; "
      "nastran95ase rewrites the deck and is not stopped by them." },
    { "307",
      "A bulk data card name is not one this solver knows; the name is "
      "printed after the message. Card reading goes on, so several of "
      "these can appear, and the job stops once the bulk data has been "
      "read.",
      "Check the spelling, and the list of cards NASTRAN-95 has: CQUAD4 "
      "and CTRIA3 are there, but no CBEAM, CBUSH, RBAR, RBE2, EIGRL or "
      "PBARL. nastran95ase translates those six and the other common MSC "
      "cards; nastran95 reads only the 1970s ones." },
    { "311",
      "Two bulk data cards of the same type carry the same "
      "identification number in field 2.",
      "Grid, element, property and material ids must each be unique "
      "within their card type; load and constraint set cards may share a "
      "set id and are not checked. The SORTED CARD COUNT is the second "
      "card of the pair in the sorted echo, where both appear together." },
    { "313",
      "The named card's word count is not one its NASTRAN-95 layout "
      "allows: too many fields, a missing continuation line, or a name "
      "in a field that takes a number (a name counts as two words). The "
      "line beneath gives the allowed minimum and maximum, and last the "
      "count found.",
      "Compare the card with its 1970s layout in the User's Manual, "
      "section 2.4. Too many words is nearly always an MSC trailing "
      "field: OFFT in field 9 of a CBAR is the usual one, and leaving "
      "that field blank clears it. Too few means a required field or a "
      "second line is missing." },
    { "315",
      "A field on the named card holds the wrong kind of value for its "
      "position: an integer where the card wants a real (MAT1 with E of "
      "70 instead of 70.0), a real where it wants an id, or data in a "
      "field the 1970s layout leaves blank (MSC's SEID in field 9 of a "
      "GRID). The number after the name is the card id.",
      "Find the card in the sorted echo and give every real a decimal "
      "point, every integer none, and clear any field the NASTRAN-95 "
      "card description does not list. Message 300 is the same kind of "
      "fault caught earlier, on a number that could not be read at all." },
    { "316",
      "A card has illegal data: a field holds a value outside what the "
      "card allows, such as a negative id or a component list with a "
      "digit above 6. When a 300 for the same card comes first this 316 "
      "is only the consequence, and the id prints as 0.",
      "Compare the card in the sorted echo with its description in the "
      "NASTRAN User's Manual; the field is not always the obvious one. A "
      "field of the wrong type is 315 and a continuation without a "
      "parent is 209, so neither of those is the cause here." },
    { "340",
      "A rigid format that needs PARAM cards found none. DISP rigid "
      "formats 10, 11 and 12 (modal complex eigenvalue, modal frequency "
      "response, modal transient) want PARAM LMODES, or PARAM LFREQ and "
      "PARAM HFREQ, to choose the modes that carry the response. Rigid "
      "format 3 needs none.",
      "Add PARAM LMODES n (the number of modes to keep) or PARAM LFREQ "
      "f1 and PARAM HFREQ f2 (a band in Hz) to the bulk data, not both. "
      "With other PARAM cards present but none of these, the same lack "
      "prints 341 instead and the fix is the same." },
    { "505",
      "An executive control card has a name the solver does not know. "
      "The 1995 executive control is ID, APP, SOL, TIME, DIAG, ALTER, "
      "ENDALTER, INSERT, DELETE, CHKPNT, RESTART, UMF, UMFEDIT, PREC, "
      "INTERACT, BEGIN and CEND. A missing CEND gives it too, because "
      "the first case control card is then read as an executive one.",
      "Remove or rename the card, and check that CEND is there. NASTRAN "
      "BANDIT=-1 and its kind must be line 1, ahead of every comment. "
      "ASSIGN and the other MSC file management statements are not "
      "executive control here: nastran95ase reads ASSIGN OUTPUT4 and "
      "drops the rest, nastran95 does not." },
    { "507",
      "An executive control card is not in the form NASTRAN-95 wants, "
      "and IMHERE says which: 530 is the ID card, which needs two names "
      "joined by a comma (ID MYJOB,STATIC), and 110 is TIME, which takes "
      "a positive whole number of minutes (TIME 2, not 2.0 or 0).",
      "Fix the card the echo shows just above the message. Both slip in "
      "from MSC habit, which has no ID card and takes a real on TIME." },
    { "615",
      "The name after SET on a case control card is not an integer, as "
      "in SET A = 1,2,3. The message follows the card in the case "
      "control echo.",
      "SET lists are named by an integer, SET 10 = 1,2,3, and referred "
      "to by that integer on the output request. A SET with no name at "
      "all is reported as 614 instead, and a misspelled command as 601." },
    { "617",
      "The case control card echoed just above has no value, or a value "
      "that is not the positive integer expected there. A PARAM card in "
      "the case control lands here whatever its value: MSC accepts PARAM "
      "above BEGIN BULK, NASTRAN-95 reads it only in the bulk data.",
      "Move every PARAM card below BEGIN BULK. Otherwise give the card a "
      "positive integer set id (SPC = 1, LOAD = 1, METHOD = 10); a name "
      "where a number belongs is 604 instead." },
    { "2007",
      "An element card names a grid point that no GRID card defines. The "
      "element id and the grid id are in the message. Any element that "
      "attaches to grids raises it, including the CELAS2 cards "
      "nastran95ase makes from a CBUSH, which keep the CBUSH's grid ids "
      "under a new element id.",
      "Find the element in the sorted echo and check the grid id. In a "
      "deck built from INCLUDE files it is usually a file left out: a "
      "joint, lumped-mass or bungee file attaches to grids that live in "
      "the surface or boom file it belongs with." },
    { "2010",
      "An element names a property card (PBAR, PROD, PSHELL) that is not "
      "in the deck; the element id and the property id are in the "
      "message. When no card of that type exists at all the message is "
      "2011 instead.",
      "Add the property card, or fix the id on the element. In a deck "
      "built from INCLUDE files each component file carries its own "
      "properties and the shared ones sit in a common file, so a "
      "sub-configuration must include that common file and the file of "
      "every component its joints reach." },
    { "2050",
      "A SUPORT names a grid point that does not exist in the model.",
      "The grid id on the SUPORT card matches no GRID or SPOINT card in "
      "the deck: a typing slip, or the grid was deleted or renumbered "
      "and the SUPORT was not. The sorted echo shows what is there. "
      "Under nastran95ase a SUPORT1 in the MSC deck becomes this SUPORT, "
      "so correct the SUPORT1." },
    { "2053",
      "The case control SPC = n selects a set that no SPC, SPC1 or "
      "SPCADD card carries, or an SPCADD names a member set that is "
      "missing.",
      "Match SPC = n to a set id in the sorted echo, or include the file "
      "that carries it. A free-free run on SUPORT needs no SPC command "
      "at all. nastran95ase keeps your selection, joining it to its own "
      "set through an SPCADD when it auto-constrains, so the set to look "
      "for is the one you wrote." },
    { "2101A",
      "A degree of freedom is in two dependent sets that may not "
      "overlap. The names at the end of the message say which: UM "
      "dependent on a rigid element or MPC, US on an SPC or the PS field "
      "of its GRID, UO on an OMIT, UAUR on a SUPORT, UAUL on an ASET. A "
      "degree of freedom may be in at most one of them.",
      "Take the degree of freedom out of one of the two sets. For a "
      "rigid element make the grid that carries the SPC or SUPORT the "
      "independent end: the IG field of a CRIGD1 or CRIGD2. Under "
      "nastran95ase do the same on the RBAR in the MSC deck, putting "
      "that grid in GA with CNA 123456." },
    { "2140B",
      "A grid or scalar point id above 2,147,483 is in the deck, so GP1 "
      "cuts the SEQGP and SEQEP sequence numbers from four levels to "
      "three or two, and a SEQGP or SEQEP card uses more levels than "
      "remain. In a substructuring run the large id alone is fatal.",
      "Renumber the large ids below 2,147,483, or write the sequence "
      "numbers with no more levels than user warning 2140A, printed just "
      "above, says remain. Dropping the SEQGP and SEQEP cards clears it "
      "too, except in a substructuring run." },
    { "2192",
      "A rigid element names a grid point that no GRID card defines "
      "(RIGD1 in the message is a CRIGD1). Under nastran95ase every RBAR "
      "became a CRIGD1 with the same element id, so the id in the "
      "message is the RBAR's, and the grid may be a renumbered one when "
      "the deck had ids above 16,777,215.",
      "Check the two grid ids on that RBAR or CRIGD1 in the sorted echo. "
      "In a deck built from INCLUDE files the rigid links in a joint or "
      "lumped-mass file attach to grids of the component file they "
      "belong with: include that file, or leave out the joint and "
      "lumped files of a component that is not in the run." },
    { "2200",
      "The rigid body mass matrix on the SUPORT degrees of freedom is "
      "singular: along one of the reference motions the model has no "
      "mass, so the rigid body modes cannot be normalised. With CONM2 "
      "masses that carry no rotary inertia this is rotation about an "
      "axis all the masses lie on, or a mass file left out.",
      "Give that motion mass: rotary inertia I11, I22 and I33 on the "
      "CONM2 cards, or the missing lumped-mass INCLUDE. Density on a "
      "straight beam adds no inertia about its own axis. Masses off the "
      "axis, or a ground-test SPC instead of SUPORT, also pass it." },
    { "2215",
      "A property card (PBAR, PROD, PSHELL) names a material id that no "
      "MAT1 card defines; the material id and the property id are in the "
      "message. With no MAT1 in the deck at all the message is 2016.",
      "Add the MAT1, or fix the MID on the property card. In a deck "
      "built from INCLUDE files every MAT1 usually sits in one common "
      "file, so a new property needs its MAT1 written there and every "
      "sub-configuration deck must include that file." },
    { "2423",
      "A degree of freedom is made dependent twice: two rigid elements "
      "(CRIGD1, which is what nastran95ase turns RBAR and RBE2 into, "
      "CRIGD2, CRIGDR) or MPC cards both name it as their dependent "
      "end. One line per component, then GP4 stops with 3037.",
      "SIL is an internal degree of freedom number, not a grid id: DIAG "
      "21 in the executive control prints the table that maps them (UIM "
      "2118), and with six dofs a grid and no resequencing, SIL 31 to 36 "
      "is the sixth grid in sorted order. Let each dependent grid appear "
      "on one rigid element only, or turn the second one round." },
    { "3005",
      "A matrix the named subroutine had to factor or invert has a zero "
      "pivot. KLL or MAA in FACTOR is the stiffness or the mass matrix, "
      "and UFM 3097 above lists its singular columns. SCRATCH2 in MCE1B "
      "is the MPC dependent partition: the MPC equations are not "
      "independent. (NONE) in SMA3A is a GENEL with a singular Z or K.",
      "For KLL, constrain or connect the degrees of freedom UFM 3097 "
      "names, or add PARAM AUTOSPC 1. For MAA, give those degrees of "
      "freedom mass: a density on the MAT1, or CONM2 inertia. For "
      "MCE1B, rewrite the MPC set so that the dependent dof of one "
      "equation does not appear in another. For SMA3A, give the GENEL a "
      "Z or K of full rank." },
    { "3008",
      "A module ran out of open core, the solver's fixed working "
      "memory. The line under the number names the subroutine, and "
      "ADDITIONAL CORE REQUIRED says how short it was when the module "
      "knows. END OF JOB still prints after it.",
      "Set OCMEM in the environment higher (nastran95 defaults to "
      "2,000,000 words, nastran95ase to three quarters of the build's "
      "open core); the in-memory database, DBMEM, gets the rest of it. "
      "Rebuild with a larger -DNASTRAN_OPEN_CORE_WORDS if the whole of "
      "it is not enough." },
    { "3019",
      "The print file passed the MAXLINES limit and the job was stopped.",
      "Add MAXLINES = 999999 to the case control (nastran95ase does), or "
      "print less: DISPLACEMENT = ALL on a large model is thousands of "
      "pages." },
    { "3031",
      "A set the case control selects is not in the bulk data. The "
      "message names the set id, the table it was looked for in and the "
      "module: SLT is the static LOAD, DLT the DLOAD, FRL the FREQ, TRL "
      "the TSTEP, EED the CMETHOD, NLFT the NONLINEAR set. A missing "
      "METHOD set comes out as 3032 instead.",
      "Check the id against the set ids on the bulk data cards of that "
      "kind. A set that holds only SPCD cards is never found in SLT, "
      "whether or not other load sets exist: give it a FORCE as well, "
      "one of zero magnitude on any grid will do." },
    { "3032",
      "The case control selects a set the bulk data does not carry. The "
      "table in the message says which kind: EED holds the EIGR cards, "
      "so METHOD = n found no EIGR with set id n, and EDT is a DEFORM "
      "set. Set 0 means nothing was selected at all, usually a modes run "
      "with no METHOD line.",
      "Make the id in the case control match field 2 of the EIGR or "
      "DEFORM card, or add the card. nastran95ase writes METHOD from the "
      "EIGRL it translates when the case control names none; in a "
      "hand-written nastran95 deck put the METHOD = n line in yourself." },
    { "3037",
      "JOB TERMINATED IN SUBROUTINE xxxx is the generic stop that about "
      "a hundred routines call after printing their own reason, which is "
      "the message just above this one. From SCHEME it is BANDIT, the "
      "grid resequencer: its scratch array, 150 words or a tenth of the "
      "grid count, was too small for the level structure.",
      "Read the message above this one first. For SCHEME, turn BANDIT "
      "off with NASTRAN BANDIT=-1 as the very first line of the deck "
      "(nastran95ase does this above 300 grids), or enlarge the array "
      "with NASTRAN BANDTDIM=n, n from 2 to 9. The answer does not "
      "depend on resequencing." },
    { "3056",
      "A GRAV load needs the mass matrix and none was built. Only two "
      "places raise it, the static load generator SSG1 and the transient "
      "one TRLGA, both on a GRAV. Every MAT1 the elements reference has "
      "no RHO and the deck has no CONM2 and no nonstructural mass, so "
      "the mass matrix is purged rather than zero.",
      "Put RHO in field 6 of the MAT1 (mass density in the model's "
      "units), or add CONM2 cards or NSM on the properties, so that a "
      "mass matrix exists. For a statics run that did not mean to have "
      "gravity, take the GRAV out of the LOAD set instead." },
    { "3097",
      "The named data block is singular in the listed columns. KLL is "
      "the stiffness: those degrees of freedom have no stiffness, or the "
      "model is a mechanism. MAA is the mass matrix, which GIV and MGIV "
      "factor instead, so lumped CBAR mass and a CONM2 with only a mass "
      "leave the rotations massless and the columns come in threes.",
      "On KLL, constrain those columns with SPC1 or connect them: "
      "rotations of grids that only rods or springs hold, and every dof "
      "of a grid that only a mass holds. PARAM AUTOSPC 1 makes "
      "NASTRAN-95 add the SPC1 cards itself and print them (UIM 2435); "
      "nastran95ase does the same and reports it. On MAA, switch EIGR "
      "GIV to FEER, or OMIT1 456 the rotations, or add CONM2 I11, I22 "
      "and I33." },
    { "6206",
      "The SOF already holds data and the PASSWORD in this run's "
      "substructure control deck is not the one it was created with; "
      "every run that uses one SOF must give the same PASSWORD. The SOF "
      "is <deck>.sof beside the print file unless SOF1 in the "
      "environment names another file.",
      "Use the PASSWORD the earlier phase used, and give every phase the "
      "same SOF: run the phases under one deck name, or set SOF1 to the "
      "file. A leftover SOF from another model can be deleted, or "
      "declared SOF(1) = name,size,NEW to be started afresh." },
    { "3118",
      "A CROD or CONROD has zero length: its two grid points sit at the "
      "same coordinates, so the element has no axis. The element id in "
      "the message is the rod, and the same grid named at both ends is "
      "caught earlier as 316.",
      "Check the coordinates of the two grids on the named element. A "
      "spring between coincident points is a CELAS2, not a rod; "
      "nastran95ase makes that choice for CBUSH cards itself, so a rod "
      "of zero length is one written by hand." },
    { "3145",
      "A constraint card (SPC, SPC1, SPCD, SUPORT, MPC, OMIT, OMIT1, "
      "ASET or ASET1) names a grid point with the component field blank "
      "or zero. A blank component is legal only for a scalar point; the "
      "card name at the end of the message says which card.",
      "Put the component digits in the C field: 123456 to fix a grid, 3 "
      "for a vertical enforced displacement. SPC and SPCD are SID G C D "
      "and SUPORT is ID C. A decimal value in that field is a format "
      "error, message 315, instead." },
    { "3147",
      "Two SPC cards in the selected set, or in sets an SPCADD joins, "
      "give the same grid component and at least one carries a non-zero "
      "enforced displacement. Only SPC cards are counted: the same "
      "component on an SPC1 and an SPC passes this check.",
      "Keep one SPC card per component: delete the duplicate, or put the "
      "plain fixities on SPC1 cards. An enforced displacement that "
      "differs between subcases belongs on an SPCD whose id that subcase "
      "selects with LOAD = n, with a FORCE of that id so the set "
      "exists." },
    { "3176",
      "A CBAR cannot form its element axes: its two end grids sit at the "
      "same point, offsets included, or its orientation vector, X1 X2 X3 "
      "or the line from GA to G0, runs along the bar. A zero vector, or "
      "a G0 that is GA itself, is caught earlier as 316.",
      "Check X1 X2 X3 (or G0) against the end grids GA and GB: the "
      "vector needs a component normal to the bar, so G0 must not lie on "
      "the bar's line. A vertical member given the usual 0. 0. 1. "
      "orientation, or two stations at the same coordinates, are the "
      "common ones; give a vertical spar 0. 1. 0. instead." },
    { "3178",
      "A LOAD combination card names a set id that no static load card "
      "(FORCE, MOMENT, PLOAD, GRAV and their kind) carries. A set "
      "holding only SPCD cards does not count, and a LOAD card cannot "
      "name another LOAD card.",
      "Every set on a LOAD card must be the id of real load cards in the "
      "bulk data, and a LOAD naming another LOAD has to be folded into "
      "one card. An SPCD set is not reached through a LOAD card: select "
      "it directly with LOAD = n in the case control and give it a FORCE "
      "of that id so the set exists." },
    { "3179",
      "A LOAD combination card lists the same load set id twice.",
      "Name each set once and fold the scale factors into one: 1.5 of "
      "set 1 plus 2.0 of set 1 is 3.5 of set 1. Two loads that should "
      "add with different factors need different set ids on their FORCE, "
      "MOMENT or GRAV cards." },
    { "2386",
      "FEER's shifted stiffness matrix (K plus the shift times M) was "
      "still singular after the shift had been raised twice by a factor "
      "of 100: some analysis-set degrees of freedom have neither "
      "stiffness nor mass, and no shift can help them. A mechanism that "
      "carries mass passes this test; a massless free freedom does not.",
      "The UFM 3097 lines above list the singular columns (LAMA there is "
      "the shifted matrix, not the eigenvalue table). Constrain them "
      "with SPC1, give them mass (CONM2 with rotary inertia), or remove "
      "them with ASET or OMIT. Grids nothing attaches to, and rotations "
      "of massless grids on rods or springs, are the usual ones." },
    { "2391",
      "FEER's tridiagonal reduction produced no usable rows, so there is "
      "no reduced problem to solve. Since the 2394 guard, this follows a "
      "start vector with no positive mass norm: the mass matrix is "
      "singular or indefinite along it.",
      "Look for UWM 2394 just above. Give the massless degrees of freedom "
      "mass (CONM2 rotary inertia terms I11, I22, I33) or remove them "
      "(ASET/OMIT), and check that no CONM2 carries a negative mass or "
      "an inertia tensor that is not positive definite." },
    { "2395",
      "FEER's QR iteration on the reduced tridiagonal matrix got a NaN, "
      "or ran 200 sweeps per eigenvalue plus 1000 without converging; "
      "NASA's loop had no bound. The case it guards against, a trial "
      "vector that lost its mass norm on a semi-definite mass matrix, is "
      "caught earlier as UWM 2394, so this should be rare.",
      "If UWM 2394 appears earlier, the mass matrix is singular or "
      "indefinite along some direction: give the lumped masses rotary "
      "inertia (CONM2 I11, I22, I33), ASET or OMIT the massless "
      "rotations, and check for a negative CONM2 mass. Without a 2394, "
      "report the deck; DIAG 16 prints every row of the reduction." },
    { "GINO",
      "The solver's own file system (GINO) stopped the run. Most are a "
      "file it could not read as written: a control word where data was "
      "expected, or a read past the end. The same banner also covers "
      "running out of disk space or of file slots. The lines under the "
      "number name the file and usually the unit.",
      "A file read as written wrong is a defect in the solver's logic, "
      "not in the model, and the log (deck.log) names the module it "
      "happened in: report it with the deck and the log. One such path, "
      "FEER's reseed after a null trial vector with the vectors held in "
      "core, is fixed in this fork." },
    { "-8",
      "A module ran out of open core, the solver's fixed working memory, "
      "and the stop printed no number at all. When it does print one it "
      "is 3008, which has its own entry.",
      "Set OCMEM in the environment higher (nastran95 defaults to "
      "2,000,000 words, nastran95ase to three quarters of the build's "
      "open core); the in-memory database, DBMEM, gets the rest of it. "
      "Rebuild with a larger -DNASTRAN_OPEN_CORE_WORDS if the whole of "
      "it is not enough." },
    { NULL, NULL, NULL }
};

/* HALO: a copy of the line with every run of blanks collapsed to one.
 * The solver spells its messages two ways: most modules write
 * "*** USER FATAL MESSAGE nnnn, text" through WRTMSG, while the ones
 * that go through mis/msgwrt.f pad each word into a field of its own
 * and write "*** USER FATAL    MESSAGE  nnnn" with the text on the
 * next line. Matching the first spelling only meant that a run whose
 * single fatal was of the second kind got no explanation at all -- and
 * the exit handler, which tested the same way, called it a success.
 * mds/hexit.f squeezes the same way for the same reason.              */
static void squeeze(const char *in, char *out, size_t n)
{
    size_t k = 0;
    int    blank = 0;
    while (*in && k + 1 < n) {
        if (*in == ' ' || *in == '\t') {
            if (!blank) { out[k++] = ' '; blank = 1; }
        } else {
            out[k++] = *in;
            blank = 0;
        }
        in++;
    }
    out[k] = '\0';
}

/* the message number as printed: the token after "MESSAGE", digits and
 * a trailing letter, comma or period stripped                          */
static void message_number(const char *line, char *num, size_t n)
{
    const char *p = strstr(line, "MESSAGE");
    size_t k = 0;
    num[0] = '\0';
    if (!p) return;
    p += 7;
    while (*p == ' ') p++;
    while (*p && k < n - 1 && (isalnum((unsigned char) *p) || *p == '-')) num[k++] = *p++;
    num[k] = '\0';
}

static void wrap(FILE *fp, const char *indent, const char *text)
{
    const char *p = text;
    int col = 0;
    /* the caller has written the label the first line hangs from */
    while (*p) {
        const char *q = p;
        int wl = 0;
        while (*q && *q != ' ') { q++; wl++; }
        if (col + wl > 70 && col > 0) { fprintf(fp, "\n%s", indent); col = 0; }
        fwrite(p, 1, (size_t) wl, fp);
        col += wl;
        p = q;
        while (*p == ' ') p++;
        if (*p) { fputc(' ', fp); col++; }
    }
    fputc('\n', fp);
}

/* Scan the print file; on the first fatal, repeat it and explain it.
 * Returns 1 if a fatal was found.                                     */
int msc_diag(const char *prt)
{
    FILE *fp = fopen(prt, "r");
    char  line[512], sq[512], num[16];
    int   found = 0, shown = 0, core = 0;
    if (!fp) return 0;
    while (fgets(line, sizeof(line), fp)) {
        int fatal;
        squeeze(line, sq, sizeof(sq));
        /* the same rule as HFATAL in mds/hexit.f, so that the two agree
         * on what a fatal is: asterisks, then USER or SYSTEM, then
         * FATAL MESSAGE. Testing for "FATAL MESSAGE" alone also caught
         * "*** USER POTENTIALLY FATAL MESSAGE nnnn" (mis/xgpidg.f), a
         * note that the run went on past, and would have explained that
         * line instead of the real fatal below it.                    */
        fatal = (strstr(sq, "*** USER FATAL MESSAGE") != NULL ||
                 strstr(sq, "*** SYSTEM FATAL MESSAGE") != NULL);
        /* GINO, the solver's file system, stops a run with "I/O
         * SUBSYSTEM ERROR NUMBER nnn" and no numbered message at all;
         * the exit handler counts it as a fatal and so does this     */
        int gino = (strstr(sq, "I/O SUBSYSTEM ERROR") != NULL);
        if (gino) fatal = 1;
        /* an out-of-core stop prints no message at all: ERRTRC after a
         * data block table is its signature                          */
        if (strstr(sq, "ERRTRC CALLED")) core = 1;
        if (!fatal) continue;
        found = 1;
        if (shown) continue;
        shown = 1;
        if (gino) strcpy(num, "GINO");
        else      message_number(sq, num, sizeof(num));
        fprintf(stderr, "\nnastran: the solver stopped on this message:\n");
        {
            size_t n = strlen(line);
            while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        }
        fprintf(stderr, "    %s\n", line + (line[0] == '0' ? 1 : 0));
        /* the continuation lines of the message, up to a blank line */
        {
            int k;
            for (k = 0; k < 3 && fgets(line, sizeof(line), fp); k++) {
                size_t n = strlen(line);
                while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
                if (msc_isblank_line(line)) break;
                fprintf(stderr, "    %s\n", line);
            }
        }
        {
            int i;
            for (i = 0; catalogue[i].num; i++) {
                if (msc_streq(catalogue[i].num, num)) {
                    fprintf(stderr, "  WHAT  ");
                    wrap(stderr, "        ", catalogue[i].what);
                    fprintf(stderr, "  FIX   ");
                    wrap(stderr, "        ", catalogue[i].fix);
                    break;
                }
            }
            if (!catalogue[i].num)
                fprintf(stderr, "  (no explanation on file for message %s; "
                        "the NASTRAN User's Manual, section 6, has the "
                        "list)\n", num);
        }
    }
    fclose(fp);
    if (!found && core) {
        int i;
        fprintf(stderr, "\nnastran: the solver stopped without a numbered "
                "message, which is how it runs out of open core.\n");
        for (i = 0; catalogue[i].num; i++)
            if (msc_streq(catalogue[i].num, "-8")) {
                fprintf(stderr, "  WHAT  "); wrap(stderr, "        ", catalogue[i].what);
                fprintf(stderr, "  FIX   "); wrap(stderr, "        ", catalogue[i].fix);
            }
        return 1;
    }
    return found;
}
