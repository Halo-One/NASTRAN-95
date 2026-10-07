% nastran95ase's parallel SOL 145 path on a deck that solves in a second:
% decks/two_subcase_flutter.dat, a cantilever beam with one CAERO1 panel,
% two subcases (mach 0.10 and 0.20, PKNL on three matched points each),
% the third point of each marked with a negative velocity, DISP and
% OFREQUENCY in the case control. The driver runs one child per subcase
% and joins the prints; these tests hold the joined print to what a
% single run prints and what VehicleDesign's readers expect: the MSC layout
% (read_nastran_grids finds the sorted echo, the renumbered reference
% grid is put back), one FLUTTER SUMMARY case per subcase (read_nastran_
% flutter), the PK modal vectors and the physical complex eigenvectors at
% the marked points (read_nastran_pk_eigenvectors, read_nastran_complex_
% eigenvectors), and a flutter mode animated from them; the same flutter
% restarted off a checkpointed modes run (scr=no, restart=, optp=) gives
% the same print without solving the modes again; and the same print
% from the serial solve: the PK loops, doublet lattice rows and in-core
% solve are threaded (fork branch halo-ase-sol145-perf, both systems), and
% every one of them has to give the serial answer to the bit.
%
% The readers are VehicleDesign's (utilities/jhc_library): n95_platform('readers')
% finds them through VEHICLEDESIGN_ROOT or a checkout beside this fork, and
% without them the tests are incomplete. The executable is standalone/nastran95ase.
%
% To run this from the local directory:
%
%   runtests("test_nastran95ase_flutter_subcases.m")
%
function tests = test_nastran95ase_flutter_subcases()
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
    deck = fullfile(here, 'decks', 'two_subcase_flutter.dat');
    out = tempname;
    mkdir(out);
    test_case.TestData.out = out;

    % the run: two children, the prints joined
    [rc, cmdout] = system(n95_platform('cmd', exe, deck, out));
    assert(rc == 0, 'nastran95ase exit code %d:\n%s', rc, cmdout);
    test_case.TestData.print = fullfile(out, 'two_subcase_flutter.out');
    assert(isfile(test_case.TestData.print), 'no joined print file');

    % the beam as the animator reads a config's include folder: the bars,
    % the rigid arms and the beam's node list
    include_dir = fullfile(out, 'include');
    mkdir(include_dir);
    write_lines(fullfile(include_dir, 'beam_spline_groups.bdf'), {'SET1,1010000,1,2,3,4,5,6'});
    write_lines(fullfile(include_dir, 'wing_R.bdf'), { ...
        'CBAR,1,1,1,2,1.0,0.0,0.0', 'CBAR,2,1,2,3,1.0,0.0,0.0', 'CBAR,3,1,3,4,1.0,0.0,0.0', ...
        'CBAR,4,1,4,5,1.0,0.0,0.0', 'CBAR,5,1,5,6,1.0,0.0,0.0', ...
        'RBAR,101,1,7,123456,,,123456,0.0', 'RBAR,102,6,8,123456,,,123456,0.0'});
    test_case.TestData.include_dir = include_dir;
    test_case.TestData.exe = exe;
    test_case.TestData.deck = deck;
end

function teardownOnce(test_case)
    close all;
    rmdir(test_case.TestData.out, 's');
end

%% the joined print's layout

function test_joined_print_is_in_msc_layout(test_case)
    prt = test_case.TestData.print;
    text = fileread(prt);
    lines = regexp(text, '[^\r\n]+', 'match');
    verifyEqual(test_case, nnz(contains(lines, 'S O R T E D   B U L K   D A T A   E C H O')), 2, ...
        'one sorted echo per subcase, with the MSC layout''s spacing');
    verifyFalse(test_case, any(contains(lines, 'B U L K    D A T A')), 'NASTRAN-95''s own title is rewritten');
    verifyFalse(test_case, contains(text, sprintf('\n\n')) || contains(text, sprintf('\r\n\r\n')), ...
        'no zero-length line (MSC Nastran''s print wrote a blank line as one space)');
    verifyEqual(test_case, nnz(contains(lines, 'END OF JOB')), 2);

    % the grids of the first echo, the reference grid under its own id
    g = read_nastran_grids(prt);
    verifyEqual(test_case, sort(double(g.grid_IDs(:))), [(1:8)'; 99999999]);
    verifyEqual(test_case, double(g.xyz_grid(double(g.grid_IDs) == 6, :)), [0, 1, 0], 'AbsTol', 1e-9);
end

%% the flutter summaries

function test_one_flutter_case_per_subcase(test_case)
    vg = read_nastran_flutter(test_case.TestData.print);
    verifyEqual(test_case, numel(vg), 2);
    verifyEqual(test_case, [vg.mach_number], [0.1, 0.2], 'AbsTol', 1e-9);
    verifyEqual(test_case, [vg.n_points], [3, 3]);
    verifyEqual(test_case, [vg.n_modes], [6, 6]);
    verifyTrue(test_case, all([vg.matched]), 'PKNL solved as PK on matched points');
    verifyEqual(test_case, vg(1).velocity(:, 1), [10; 20; 30], 'AbsTol', 1e-9);
    verifyEqual(test_case, vg(2).velocity(:, 1), [20; 40; 60], 'AbsTol', 1e-9);
    verifyEqual(test_case, vg(1).density(:, 1), [1; 0.9; 0.8], 'AbsTol', 1e-9);
    % the beam's first bending mode, near its in-vacuo frequency at every point
    verifyEqual(test_case, vg(1).frequency(:, 1), 27.98 * ones(3, 1), 'AbsTol', 0.5);
end

%% the eigenvectors at the marked points

function test_eigenvectors_at_the_marked_points(test_case)
    prt = test_case.TestData.print;
    vg = read_nastran_flutter(prt);
    pk = read_nastran_pk_eigenvectors(prt);
    cv = read_nastran_complex_eigenvectors(prt);
    % PARAM PKVECT 1: the modal vector of every root at every loop (three
    % loops, two subcases, six roots), the header naming the loop and its
    % velocity; the physical recovery stays the marked loops' (the third)
    verifyEqual(test_case, numel(pk), 36, 'every root of every loop, the modal coefficients');
    verifyEqual(test_case, sort(unique([pk.loop])), [1, 2, 3], 'the loops named in the headers');
    verifyEqual(test_case, nnz([pk.loop] == 3), 12, 'the marked loop''s twelve');
    verifyEqual(test_case, sort(unique([pk.subcase])), [2010, 2020], 'the subcase of the page header');
    verifyEqual(test_case, nnz([pk.subcase] == 2010), 18, 'eighteen in each');
    verifyEqual(test_case, sort(unique([pk.velocity])), [10, 20, 30, 40, 60], 'AbsTol', 1e-6, 'the loops'' velocities');
    pk = pk([pk.loop] == 3);
    verifyEqual(test_case, numel(cv), 12, 'and its physical vector at the DISP set');
    verifyTrue(test_case, all(arrayfun(@(v) numel(v.v), pk) == 6), 'six SOL 103 modes each');
    for i_cv = 1:numel(cv)
        verifyEqual(test_case, cv(i_cv).ids, (1:6)', 'the DISP set''s points');
        verifyEqual(test_case, size(cv(i_cv).phi), [6, 6]);
        % the physical vector's eigenvalue is one of the PK eigenvalues
        verifyLessThan(test_case, min(abs([pk.eigenvalue] - cv(i_cv).eigenvalue)), ...
            1e-3 * max(abs(cv(i_cv).eigenvalue), 1));
    end
    % the marked point is the third of each subcase: its frequencies are
    % the summary's third row
    for i_case = 1:2
        f_marked = sort(vg(i_case).frequency(3, :));
        f_vectors = sort([cv((1:6) + 6 * (i_case - 1)).f_Hz]);
        verifyEqual(test_case, f_vectors, f_marked, 'RelTol', 1e-4);
    end
end

%% a flutter mode drawn from the print

function test_animates_a_mode_and_prints_its_eigenvector(test_case)
    prt = test_case.TestData.print;
    gif_file = fullfile(test_case.TestData.out, 'mode.gif');
    [gif_out, mode] = animate_nastran_flutter_mode(prt, test_case.TestData.include_dir, 1, ...
        'n_frames', 4, 'fps', 50, 'gif', gif_file, 'info', struct('mach', 0.1, 'i_mode', 1));
    verifyEqual(test_case, gif_out, gif_file);
    verifyEqual(test_case, numel(imfinfo(gif_file)), 4);
    verifyEqual(test_case, numel(mode.ids), 6);

    vec = evalc_quiet(@() print_flutter_eigenvector(prt, 1));
    verifyEqual(test_case, numel(vec.modal.v), 6);
    verifyEqual(test_case, numel(vec.physical.ids), 6);
    verifyTrue(test_case, any(contains(vec.text, 'largest coefficients')));
end

%% the threaded solve is the serial one

function test_threaded_solve_is_the_serial_solve(test_case)
    % the deck again with every threaded path off - the PK loops one after
    % the other (N95_PK_THREADS=1), GEND's own loop over the doublet
    % lattice rows (N95_DLM_THREADS=1), the built-in LU instead of LAPACK's
    % (N95_AJJ_SOLVE=builtin) - and with the PK loops on two threads: the
    % print files are the same line for line but for the clock and the date.
    % A build without these switches (the Windows one, halo-ase-sol145)
    % ignores them and passes trivially
    runs = { ...
        'serial', {'N95_PK_THREADS', 1, 'N95_DLM_THREADS', 1, 'N95_AJJ_SOLVE', 'builtin'}; ...
        'pk2',    {'N95_PK_THREADS', 2}};
    base = print_lines(test_case.TestData.print);
    for i_run = 1:size(runs, 1)
        out = fullfile(test_case.TestData.out, runs{i_run, 1});
        mkdir(out);
        [rc, cmdout] = system(n95_platform('cmd_env', runs{i_run, 2}, ...
            test_case.TestData.exe, test_case.TestData.deck, out));
        verifyEqual(test_case, rc, 0, sprintf('%s run, exit code %d:\n%s', runs{i_run, 1}, rc, cmdout));
        other = print_lines(fullfile(out, 'two_subcase_flutter.out'));
        verifyEqual(test_case, numel(other), numel(base), sprintf('%s run: the print''s length', runs{i_run, 1}));
        if numel(other) == numel(base)
            i_diff = find(~strcmp(other, base), 1);
            verifyEmpty(test_case, i_diff, sprintf('%s run differs from the default at line %d', ...
                runs{i_run, 1}, i_diff));
        end
    end
end

%% the flutter restarted off a checkpointed modes run

function test_restart_off_the_modes_run(test_case)
    % decks/two_subcase_modes.dat is the same beam with SOL 103 and the
    % structural cards alone. Run with scr=no into a directory it leaves
    % its checkpoint tape and dictionary there; the flutter deck restarted
    % off them (restart=<modes deck> optp=<that directory>) carries only
    % its own cards, NASTRAN-95 goes straight to the aerodynamics, and
    % the joined print is the direct run's to every printed digit, with
    % the modes run's eigenvalue table spliced in after each sorted echo
    % (read_khh), the merged sorted echo (read_nastran_grids) and the
    % eigenvectors at the marked points (read_nastran_complex_eigenvectors)
    here = fileparts(mfilename('fullpath'));
    exe = test_case.TestData.exe;
    modes_deck = fullfile(here, 'decks', 'two_subcase_modes.dat');
    flutter_deck = fullfile(here, 'decks', 'two_subcase_flutter.dat');
    out = test_case.TestData.out;
    modes_out = fullfile(out, 'modes');
    restart_out = fullfile(out, 'restart');

    [rc, cmdout] = system(n95_platform('cmd', exe, modes_deck, 'scr=no', modes_out));
    assert(rc == 0, 'the modes run: exit code %d:\n%s', rc, cmdout);
    verifyTrue(test_case, isfile(fullfile(modes_out, 'two_subcase_modes.nptp')), 'the checkpoint tape');
    verifyTrue(test_case, isfile(fullfile(modes_out, 'two_subcase_modes.dic')), 'and the dictionary');
    verifyTrue(test_case, contains(cmdout, '9464'), 'UIM 9464 says the checkpoint is kept');

    [rc, cmdout] = system(n95_platform('cmd', exe, flutter_deck, ['restart=', modes_deck], ...
        ['optp=', modes_out], restart_out));
    assert(rc == 0, 'the restart: exit code %d:\n%s', rc, cmdout);
    verifyTrue(test_case, contains(cmdout, '9461'), 'UIM 9461 names the restart');
    verifyFalse(test_case, contains(cmdout, '9467'), 'no card of the restart deck changes the structure');
    prt = fullfile(restart_out, 'two_subcase_flutter.out');
    assert(isfile(prt), 'no joined print of the restart');
    text = fileread(prt);
    verifyFalse(test_case, contains(text, 'FATAL'));
    verifyTrue(test_case, contains(text, 'RIGID FORMAT SWITCH'), 'a modified restart from SOL 103 into AERO 10');
    % the child's module log: the direct run's has READ and its FEER
    % passes, the restart's neither (the DMAP listing in the print names
    % every module, executed or not)
    child_log = fileread(fullfile(restart_out, 's2010', 'two_subcase_flutter_s2010.log'));
    verifyFalse(test_case, contains(child_log, 'FEER'), 'the eigensolution is not executed again');

    % the summaries, digit for digit
    vg_direct = read_nastran_flutter(test_case.TestData.print);
    vg_restart = read_nastran_flutter(prt);
    verifyEqual(test_case, numel(vg_restart), 2);
    for i_case = 1:2
        verifyEqual(test_case, vg_restart(i_case).frequency, vg_direct(i_case).frequency);
        verifyEqual(test_case, vg_restart(i_case).damping, vg_direct(i_case).damping);
        verifyEqual(test_case, vg_restart(i_case).velocity, vg_direct(i_case).velocity);
    end

    % what the readers want from a flutter print: the generalized
    % stiffnesses of the six modes off the spliced eigenvalue table
    % (read_khh reads on past a table's rows, so only the first six of
    % either print are the modes')
    khh = diag(read_khh(prt));
    khh_direct = diag(read_khh(test_case.TestData.print));
    verifyGreaterThanOrEqual(test_case, numel(khh), 6, 'the modes run''s eigenvalue table is in the print');
    verifyEqual(test_case, khh(1:6), khh_direct(1:6), 'RelTol', 1e-6);
    verifyEqual(test_case, sqrt(khh(1)) / (2 * pi), 27.98, 'AbsTol', 0.5, 'the first bending mode''s frequency');
    g = read_nastran_grids(prt);
    verifyEqual(test_case, sort(double(g.grid_IDs(:))), [(1:8)'; 99999999], 'the merged sorted echo');
    cv = read_nastran_complex_eigenvectors(prt);
    verifyEqual(test_case, numel(cv), 12, 'the eigenvectors at the marked points');
end

%% the eigenvector rebuilt from the modal vector and the modes print

function test_rebuilds_the_eigenvector_from_the_modal_vector(test_case)
    % at the marked loop the print has both: the rebuilt vector (PHI q
    % from the modes print's shapes) must be the recovered one at the
    % elastic axis nodes, up to the complex scale; at an unmarked loop
    % only the modal vector exists, and the animator draws from it
    here = fileparts(mfilename('fullpath'));
    exe = test_case.TestData.exe;
    out = test_case.TestData.out;
    prt = test_case.TestData.print;
    modes_out = fullfile(out, 'modes_for_rebuild');
    modes_print = fullfile(modes_out, 'two_subcase_modes.out');
    if ~isfile(modes_print)
        [rc, cmdout] = system(n95_platform('cmd', exe, fullfile(here, 'decks', 'two_subcase_modes.dat'), modes_out));
        assert(rc == 0, 'the modes run: exit code %d:\n%s', rc, cmdout);
    end

    % the reader's points are in print order, the FLFACT's: point 3 of
    % subcase 1 is its marked loop 3; the first root there
    vg = read_nastran_flutter(prt);
    i_pt = 3;
    which = struct('f_Hz', vg(1).frequency(i_pt, 1), 'damping', vg(1).damping(i_pt, 1), ...
        'loop', 3, 'subcase', vg(1).subcase);
    inc = test_case.TestData.include_dir;
    [~, rec] = animate_nastran_flutter_mode(prt, inc, which, 'n_frames', 2, 'gif', '', 'info', struct('mach', 0.1, 'i_mode', 1));
    verifyTrue(test_case, all(ismember(1:6, rec.ids)), 'the recovered vector at the elastic axis nodes');
    [~, reb] = rebuilt(prt, which, modes_print, inc);
    verifyTrue(test_case, all(ismember(1:6, reb.ids)), 'the rebuilt one at the modes print''s points');
    [~, i_a] = ismember(1:6, rec.ids);
    [~, i_b] = ismember(1:6, reb.ids);
    a = rec.phi(i_a, 3);
    b = reb.phi(i_b, 3);
    a = a / a(6); b = b / b(6);
    verifyEqual(test_case, b, a, 'AbsTol', 1e-3, 'the tip-normalised plunge of the two agree');

    % an unmarked loop: no recovered vector, the modal one and the modes
    % print give the gif
    i_pt = 1;
    which = struct('f_Hz', vg(1).frequency(i_pt, 1), 'damping', vg(1).damping(i_pt, 1), ...
        'loop', 1, 'subcase', vg(1).subcase);
    gif_file = fullfile(out, 'rebuilt.gif');
    [g, m] = animate_nastran_flutter_mode(prt, inc, which, 'n_frames', 3, 'gif', gif_file, 'rebuild', true, ...
        'modes_print', modes_print, 'info', struct('mach', 0.1, 'i_mode', 1));
    verifyEqual(test_case, g, gif_file);
    verifyEqual(test_case, numel(imfinfo(gif_file)), 3);
    verifyTrue(test_case, all(ismember(1:6, m.ids)));
end

function [g, m] = rebuilt(prt, which, modes_print, inc)
    % the rebuild forced over the recovered vector
    [g, m] = animate_nastran_flutter_mode(prt, inc, which, 'n_frames', 2, 'gif', '', 'rebuild', true, ...
        'modes_print', modes_print, 'info', struct('mach', 0.1, 'i_mode', 1));
end

%% helpers

function lines = print_lines(prt)
    % the print file's lines, the run's clock and the page headers' date
    % taken out (the only things two runs of one solve may differ in)
    text = strrep(fileread(prt), sprintf('\r'), '');
    lines = regexp(text, '[^\n]*', 'match');
    lines = lines(~contains(lines, {'END TIME', 'WALL CLOCK', 'TIME ESTIMATE'}) & ...
        cellfun(@isempty, regexp(lines, '^ +DATE: ', 'once')));
    lines = regexprep(lines, '/ [A-Z]{3} [ 0-9]{2}, [0-9]{2} / PAGE', '/ DATE / PAGE');
end

function out = evalc_quiet(fun)
    evalc('out = fun();');
end

function write_lines(filename, lines)
    fid = fopen(filename, 'w');
    fprintf(fid, '%s\n', lines{:});
    fclose(fid);
end
