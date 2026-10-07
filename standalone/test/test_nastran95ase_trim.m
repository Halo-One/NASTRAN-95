% nastran95ase's SOL 144 (static aeroelastic trim, fork branch
% halo-ase-sol144: the module AETRIM in a DMAP program the front end
% writes) on a deck that solves in a second: decks/trim_small_aircraft.dat,
% a free-free beam aircraft (fuselage, an 8 m straight wing, a tail and a
% fin, 20.9 kg, SUPORT1 at the wing root) trimmed for level 1 g flight at
% Mach 0.1 and q = 40 and 120 Pa, the left aileron linked to the right.
% These tests hold the print to what VehicleDesign's readers expect (the
% stability derivative tables read_nastran_trim reads, the trim variables
% of read_nastran_trim_variables, OLOAD, DISPLACEMENT per subcase) and to
% physics that needs no other solver: the inertia derivative of URDD3 is
% weight / (q S); the rigid splined derivatives are the unsplined ones
% (every box is splined); the trimmed aerodynamic lift is the weight and
% OLOAD (aerodynamic minus inertial loads) sums to zero; a symmetric
% aircraft trims with the ailerons and the lateral accelerations at zero
% and deflects symmetrically; the support grid does not move. The
% Simcenter comparison is the monarch deck's, in
% test_nastran95ase_aeroelastic.m (test_sol144_trim).
%
% On a build without SOL 144 the front end stops on the deck (UFM 9110) and
% these tests are incomplete, not failed. The readers are VehicleDesign's
% (utilities/jhc_library, found through VEHICLEDESIGN_ROOT or a checkout
% beside this fork); without them, too, the tests are incomplete.
%
% To run this from the local directory:
%
%   runtests("test_nastran95ase_trim.m")
%
function tests = test_nastran95ase_trim()
    tests = functiontests(localfunctions);
end

function setupOnce(test_case)
    here = fileparts(mfilename('fullpath'));          % standalone/test
    nastran_dir = fileparts(here);                    % standalone: the executables
    addpath(here);
    % the readers (read_nastran_*, animate_*) are VehicleDesign's: a checkout
    % named by VEHICLEDESIGN_ROOT, else one beside this fork; without them
    % these tests are incomplete, not failed
    readers = n95_platform('readers');
    assumeTrue(test_case, ~isempty(readers), ['VehicleDesign''s utilities/jhc_library was not found: ', ...
        'set VEHICLEDESIGN_ROOT, or check VehicleDesign out beside this fork']);
    addpath(readers);
    exe = n95_platform('exe', nastran_dir, 'nastran95ase');
    assumeTrue(test_case, isfile(exe), sprintf('%s is not built (standalone/build/build_nastran95)', exe));
    deck = fullfile(here, 'decks', 'trim_small_aircraft.dat');
    out = tempname;
    mkdir(out);
    test_case.TestData.out = out;
    [rc, cmdout] = system(n95_platform('cmd_env', {'N95_TIMEOUT', 5}, exe, deck, out));
    test_case.TestData.rc = rc;
    test_case.TestData.cmdout = cmdout;
    test_case.TestData.print = fullfile(out, 'trim_small_aircraft.out');
    test_case.TestData.xlat = fullfile(out, 'trim_small_aircraft_xlat.txt');
    test_case.TestData.exe = exe;
    test_case.TestData.deck = deck;
    % the weight: 20.9 kg at the deck's g, 1 / PARAM AUNITS
    test_case.TestData.weight = 20.9 / 0.101937;
    test_case.TestData.surfaces = {'ELEV', 'AILR', 'AILL'};
end

function teardownOnce(test_case)
    % nothing to remove when setupOnce stopped on an assumption (no readers,
    % no executable): the tests are then incomplete, not failed
    if isfield(test_case.TestData, 'out') && isfolder(test_case.TestData.out)
        rmdir(test_case.TestData.out, 's');
    end
end

function setup(test_case)
    % a build without SOL 144: incomplete, with the reason
    xlat = test_case.TestData.xlat;
    if isfile(xlat)
        no_solution = regexp(fileread(xlat), 'The deck asks for (SOL \d+)[^\n]*', 'match', 'once');
        assumeEmpty(test_case, no_solution, sprintf('%s: this build has no SOL 144', ...
            test_case.TestData.exe));
    end
end

%% the run

function test_runs_to_the_end(test_case)
    verifyEqual(test_case, test_case.TestData.rc, 0, test_case.TestData.cmdout);
    text = fileread(test_case.TestData.print);
    verifyEqual(test_case, numel(strfind(text, 'END OF JOB')), 1);
    verifyEmpty(test_case, regexp(text, 'FATAL MESSAGE', 'once'));
end

%% the stability derivatives

function test_derivative_tables(test_case)
    sd = read_nastran_trim(test_case.TestData.print, test_case.TestData.surfaces);
    verifyEqual(test_case, sd.qbar, [0, 40, 120], 'AbsTol', 1e-9, 'the two subcases'' q');
    verifyEqual(test_case, sd.mach, [0.1, 0.1, 0.1], 'AbsTol', 1e-9);
    verifyEqual(test_case, [sd.c_ref, sd.b_ref, sd.S_ref], [1, 8, 8], 'AbsTol', 1e-9);
    q = [40, 120];
    for k = 1:2
        T = sd.all(k);
        % the inertia of a unit URDD3 (1 g with AUNITS = 1/g): weight / (q S)
        weight = test_case.TestData.weight;
        verifyEqual(test_case, T.URDD3(3, 5), weight / (q(k) * 8), 'RelTol', 1e-5, ...
            'CZ of URDD3, inertial restrained: weight / (q S)');
        verifyEqual(test_case, T.URDD3(3, 4), 0, 'AbsTol', 1e-12, ...
            'no unrestrained derivative of an acceleration');
        % every box is splined: rigid splined = rigid unsplined
        for var = {'ALPHA', 'PITCH', 'ELEV', 'AILR', 'BETA', 'ROLL', 'YAW'}
            M = T.(var{1});
            verifyEqual(test_case, M(:, 2), M(:, 1), 'AbsTol', 2e-5 * max(abs(M(:, 1))), ...
                sprintf('%s: rigid splined against unsplined', var{1}));
        end
        % the signs of the classic derivatives, in the aerodynamic axes
        % (x aft, y right, z up): lift up with alpha, nose-down pitching
        % moment (static stability), side force and yawing moment against
        % sideslip, roll damping
        verifyGreaterThan(test_case, T.ALPHA(3, 1), 4.5, 'CZ_alpha (an aspect ratio 8 wing and a tail)');
        verifyLessThan(test_case, T.ALPHA(3, 1), 6.0);
        verifyLessThan(test_case, T.ALPHA(5, 1), 0, 'CMY_alpha < 0: statically stable');
        verifyLessThan(test_case, T.BETA(2, 1), 0, 'CY_beta < 0');
        verifyLessThan(test_case, T.BETA(6, 1), 0, 'CMZ_beta < 0 in these axes: weathercock stable');
        verifyLessThan(test_case, T.ROLL(4, 1), 0, 'CMX_roll < 0: roll damping');
        verifyLessThan(test_case, T.PITCH(5, 1), 0, 'CMY_pitch < 0: pitch damping');
        % the ailerons are mirror images
        verifyEqual(test_case, T.AILL([1 3 5], 1), T.AILR([1 3 5], 1), 'AbsTol', 1e-5, 'symmetric terms');
        verifyEqual(test_case, T.AILL([2 4 6], 1), -T.AILR([2 4 6], 1), 'AbsTol', 1e-5, 'antisymmetric terms');
    end
    % the elastic corrections grow with q
    d40 = abs(sd.all(1).ALPHA(3, 3) / sd.all(1).ALPHA(3, 1) - 1);
    d120 = abs(sd.all(2).ALPHA(3, 3) / sd.all(2).ALPHA(3, 1) - 1);
    verifyGreaterThan(test_case, d120, d40, 'the elastic increment of CZ_alpha grows with q');
end

%% the trim

function test_trim_variables(test_case)
    tv = read_nastran_trim_variables(test_case.TestData.print);
    verifyEqual(test_case, numel(tv), 2, 'a table per subcase');
    verifyEqual(test_case, [tv.subcase], [1, 2]);
    verifyEqual(test_case, [tv.q], [40, 120], 'AbsTol', 1e-9);
    for k = 1:2
        verifyEqual(test_case, tv(k).labels, {'ELEV', 'AILR', 'AILL', 'ANGLEA', 'SIDES', ...
            'ROLL', 'PITCH', 'YAW', 'URDD1', 'URDD2', 'URDD3', 'URDD4', 'URDD5', 'URDD6'}, ...
            'the variables in ID order');
        verifyEqual(test_case, tv(k).status, {'FREE', 'FREE', 'LINKED', 'FREE', 'FIXED', ...
            'FIXED', 'FIXED', 'FIXED', 'FREE', 'FREE', 'FIXED', 'FIXED', 'FIXED', 'FREE'});
        verifyEqual(test_case, tv(k).URDD3, 1, 'AbsTol', 1e-12);
        verifyEqual(test_case, tv(k).AILL, -tv(k).AILR, 'AbsTol', 1e-12, ...
            'AELINK AILL AILR 1.0: u_D + C u_I = 0');
        verifyLessThan(test_case, abs([tv(k).AILR, tv(k).URDD1, tv(k).URDD2, tv(k).URDD6]), 1e-6, ...
            'a symmetric aircraft trims with no aileron and no lateral acceleration');
        verifyGreaterThan(test_case, tv(k).ANGLEA, 0);
    end
    % lift ~ 1/q at constant weight
    verifyEqual(test_case, tv(2).ANGLEA / tv(1).ANGLEA, 40 / 120, 'RelTol', 0.05);
    % recorded from this build (fork halo-ase-sol144, 2026-09-30)
    verifyEqual(test_case, [tv.ANGLEA], [1.338831e-01, 4.353156e-02], 'RelTol', 1e-5);
    verifyEqual(test_case, [tv.ELEV], [-1.299589e-01, -4.268741e-02], 'RelTol', 1e-5);
end

%% the loads and the displacements

function test_oload_balances(test_case)
    ol = read_nastran_oload(test_case.TestData.print);
    verifyEqual(test_case, [ol.subcase], [1, 2]);
    for k = 1:2
        F = sum(ol(k).loads_N(:, 1:3), 1);
        verifyEqual(test_case, F, [0, 0, 0], 'AbsTol', 1e-6 * 205, ...
            'aerodynamic minus inertial loads balance on the trimmed free vehicle');
        % the nose mass carries no aerodynamics: its load is its inertia
        nose = ol(k).loads_N(ol(k).grid_ids == 1, 3);
        verifyEqual(test_case, nose, -8.0 / 0.101937, 'RelTol', 1e-5, 'grid 1: -m g');
    end
end

function test_displacements(test_case)
    [subs, ids, u] = read_nastran_disp_subcase(test_case.TestData.print);
    verifyEqual(test_case, subs, [1; 2]);
    verifyEqual(test_case, squeeze(u(ids == 2, :, :)), zeros(6, 2), 'AbsTol', 1e-12, ...
        'the SUPORT grid: displacements are relative to it');
    for k = 1:2
        tipR = u(ids == 14, :, k);
        tipL = u(ids == 24, :, k);
        verifyGreaterThan(test_case, tipR(3), 0, 'the wing bends up');
        verifyEqual(test_case, tipL([3 5]), tipR([3 5]), 'RelTol', 1e-4, 'symmetric bending');
        verifyEqual(test_case, tipL(4), -tipR(4), 'RelTol', 1e-4, 'antisymmetric R1');
    end
end

function test_aerodynamic_pressures_and_forces(test_case)
    text = fileread(test_case.TestData.print);
    lines = regexp(text, '[^\n]*', 'match');
    i0 = find(contains(lines, 'AERODYNAMIC FORCES ON THE AERODYNAMIC ELEMENTS'), 1);
    verifyNotEmpty(test_case, i0, 'AEROF printed');
    i1 = find(contains(lines, 'AERODYNAMIC PRESSURES ON THE AERODYNAMIC ELEMENTS'), 1);
    verifyNotEmpty(test_case, i1, 'APRES printed');
    % the boxes' normal forces of subcase 1 (the table runs over page
    % breaks, to the next subcase's tables): the horizontal surfaces lift
    % the weight
    fz = 0;
    n = 0;
    for i = i0 + 1:numel(lines)
        if contains(lines{i}, 'N O N - D I M E N S I O N A L') || ...
                contains(lines{i}, 'A E R O S T A T I C') || ...
                contains(lines{i}, 'D I S P L A C E M E N T')
            break
        end
        tok = regexp(lines{i}, '^\s+1\s+(\d+)\s+LS\s+\S+\s+\S+\s+(\S+)', 'tokens', 'once');
        if isempty(tok)
            continue
        end
        n = n + 1;
        if str2double(tok{1}) < 4000
            fz = fz + str2double(tok{2});
        end
    end
    verifyEqual(test_case, n, 76, 'one row per box');
    verifyEqual(test_case, fz, test_case.TestData.weight, 'RelTol', 1e-5, 'the lift is the weight');
end
