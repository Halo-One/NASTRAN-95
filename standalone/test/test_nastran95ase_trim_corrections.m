% nastran95ase's SOL 144 corrections and divergence (fork branch
% halo-ase-sol144-corrections, fork SOL144.md "The correction matrices"):
% W2GJ (the downwash of incidence, camber and twist), FA2J
% (experimental pressure coefficients) and WKK (box force weights), and
% DIVERG, the static divergence roots. Checks that need no other solver,
% on decks that solve in a second or two:
%
%   - W2GJ = 0.01 rad on every horizontal box of decks/trim_small_aircraft
%     is an angle of attack: ANGLEA trims 0.01 lower and nothing else moves,
%     and the REF. COEFF. row is 0.01 times the ANGLEA row;
%   - WKK = 2 I at q is the plain deck at 2 q: the same trim and the same
%     deflections, every coefficient twice;
%   - FA2J = 0.1 on the wing (wing area = reference area) is a CZ of 0.1 in
%     the intercept, and the trim balances with it;
%   - decks/divergence_typical_section.dat: a rigid wing on a torsion spring
%     K diverges at q = K / (S c CMY_alpha), its own rigid CMY_alpha about
%     the axis;
%
% and three of the Aeroelastic Analysis User's Guide's examples, rebuilt
% from the guide (decks/aeroelastic_guide_examples), against Simcenter
% Nastran 2606's prints of the same decks (data/*_simcenter2606.f06,
% 2026-10-01; Simcenter prints the guide's own answers, which
% test_simcenter_tpl_aeroelastic checks): HA144A (DMI W2GJ / WKK / FA2J on
% a symmetric half model) - the derivative tables and the trim, HA144B's
% divergence request (Simcenter's guide, Table 2-2) - every root, and
% HA145C's BAH wing by
% strip theory - the static divergence speed. And where nastran95ase parts
% from Simcenter by design: HA144B with WKK = 2 I
% (decks/aeroelastic_guide_examples/ha144b_trim_diverg_wkk2.dat) against
% Simcenter's print of it - the same trim (WKK weights the trim
% aerodynamics in both), and divergence roots half of Simcenter's, whose
% DIVERG ignores WKK.
%
% On a build without SOL 144 these tests are incomplete, not failed; so are
% they without VehicleDesign's readers (utilities/jhc_library, found through
% VEHICLEDESIGN_ROOT or a checkout beside this fork).
%
% To run this from the local directory:
%
%   runtests("test_nastran95ase_trim_corrections.m")
%
function tests = test_nastran95ase_trim_corrections()
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
    out = tempname;
    mkdir(out);
    test_case.TestData.out = out;
    test_case.TestData.exe = exe;
    test_case.TestData.surfaces = {'ELEV', 'AILR', 'AILL'};

    % the small aircraft and its variants (76 boxes: 1000-1031 right wing,
    % 2000-2031 left wing, 3000-3003 and 3100-3103 the tail, 4000-4003 the
    % fin; the j set by ascending box id)
    base = fileread(fullfile(here, 'decks', 'trim_small_aircraft.dat'));
    horizontal = [1000:1031, 2000:2031, 3000:3003, 3100:3103];
    variants = struct( ...
        'base', '', ...
        'w2gj', dmij_w2gj(horizontal, 0.01), ...
        'wkk2', sprintf('DMI,WKK,0,3,1,0,,152,1\nDMI,WKK,1,1,2.0,THRU,152\n'), ...
        'q2', '', ...
        'fa2j', sprintf('DMI,FA2J,0,2,1,0,,76,1\nDMI,FA2J,1,1,0.1,THRU,64\n'));
    names = fieldnames(variants);
    for i = 1:numel(names)
        deck_text = strrep(base, 'ENDDATA', [variants.(names{i}), 'ENDDATA']);
        if strcmp(names{i}, 'q2')
            deck_text = strrep(deck_text, 'TRIM,1,0.1,40,', 'TRIM,1,0.1,80,');
            deck_text = strrep(deck_text, 'TRIM,2,0.1,120,', 'TRIM,2,0.1,240,');
        end
        test_case.TestData.print.(names{i}) = run_deck(test_case, names{i}, deck_text);
    end
    test_case.TestData.print.typical = run_deck(test_case, 'divergence_typical_section', ...
        fileread(fullfile(here, 'decks', 'divergence_typical_section.dat')));
    test_case.TestData.print.ha144a = run_deck(test_case, 'ha144a', ...
        fileread(fullfile(here, 'decks', 'aeroelastic_guide_examples', 'ha144a.dat')));
    test_case.TestData.print.ha144b = run_deck(test_case, 'ha144b_diverg_symmetric', ...
        fileread(fullfile(here, 'decks', 'aeroelastic_guide_examples', 'ha144b_diverg_symmetric.dat')));
    test_case.TestData.print.ha145c = run_deck(test_case, 'ha145c_diverg', ...
        fileread(fullfile(here, 'decks', 'aeroelastic_guide_examples', 'ha145c_diverg.dat')));
    test_case.TestData.print.ha144b_wkk2 = run_deck(test_case, 'ha144b_trim_diverg_wkk2', ...
        fileread(fullfile(here, 'decks', 'aeroelastic_guide_examples', 'ha144b_trim_diverg_wkk2.dat')));
    test_case.TestData.here = here;
end

function teardownOnce(test_case)
    rmdir(test_case.TestData.out, 's');
end

function setup(test_case)
    % a build without SOL 144: incomplete, with the reason
    xlat = fullfile(test_case.TestData.out, 'base', 'base_xlat.txt');
    if isfile(xlat)
        no_solution = regexp(fileread(xlat), 'The deck asks for (SOL \d+)[^\n]*', 'match', 'once');
        assumeEmpty(test_case, no_solution, sprintf('%s: this build has no SOL 144', ...
            test_case.TestData.exe));
    end
end

%% the runs

function test_every_deck_runs_to_the_end(test_case)
    names = fieldnames(test_case.TestData.print);
    for i = 1:numel(names)
        text = fileread(test_case.TestData.print.(names{i}));
        verifyEqual(test_case, numel(strfind(text, 'END OF JOB')), 1, names{i});
        verifyEmpty(test_case, regexp(text, 'FATAL MESSAGE', 'once'), names{i});
    end
end

%% W2GJ, WKK, FA2J

function test_uniform_w2gj_is_an_angle_of_attack(test_case)
    p = test_case.TestData.print;
    tb = read_nastran_trim_variables(p.base);
    tw = read_nastran_trim_variables(p.w2gj);
    verifyEqual(test_case, [tw.ANGLEA] - [tb.ANGLEA], [-0.01, -0.01], 'AbsTol', 2e-7, ...
        'W2GJ = 0.01 rad on the horizontal boxes trims at ANGLEA 0.01 lower');
    others = {'ELEV', 'AILR', 'AILL', 'URDD1', 'URDD2', 'URDD6'};
    for k = 1:2
        for o = others
            verifyEqual(test_case, tw(k).(o{1}), tb(k).(o{1}), 'AbsTol', 1e-7, o{1});
        end
    end
    [~, ids_b, u_b] = read_nastran_disp_subcase(p.base);
    [~, ids_w, u_w] = read_nastran_disp_subcase(p.w2gj);
    verifyEqual(test_case, ids_w, ids_b);
    verifyEqual(test_case, u_w, u_b, 'AbsTol', 1e-6 * max(abs(u_b(:))), 'the same deflections');
    sd = read_nastran_trim(p.w2gj, test_case.TestData.surfaces);
    for k = 1:2
        T = sd.all(k);
        cols = [1 2 3 4 6];
        verifyEqual(test_case, T.REF(:, cols), 0.01 * T.ALPHA(:, cols), 'AbsTol', 2e-7, ...
            'the intercept is 0.01 times ANGLEA in every column');
    end
end

function test_wkk_of_two_is_twice_the_dynamic_pressure(test_case)
    p = test_case.TestData.print;
    tk = read_nastran_trim_variables(p.wkk2);
    tq = read_nastran_trim_variables(p.q2);
    for k = 1:2
        for lab = tq(k).labels
            verifyEqual(test_case, tk(k).(lab{1}), tq(k).(lab{1}), 'AbsTol', 1e-7 + 1e-6 * abs(tq(k).(lab{1})), ...
                ['WKK = 2 I at q trims as 2 q: ', lab{1}]);
        end
    end
    [~, ~, u_k] = read_nastran_disp_subcase(p.wkk2);
    [~, ~, u_q] = read_nastran_disp_subcase(p.q2);
    verifyEqual(test_case, u_k, u_q, 'AbsTol', 1e-6 * max(abs(u_q(:))), 'and deflects as 2 q');
    sk = read_nastran_trim(p.wkk2, test_case.TestData.surfaces);
    sq = read_nastran_trim(p.q2, test_case.TestData.surfaces);
    sb = read_nastran_trim(p.base, test_case.TestData.surfaces);
    for k = 1:2
        for var = {'ALPHA', 'PITCH', 'ELEV', 'AILR', 'BETA', 'ROLL', 'YAW', 'URDD3'}
            A = sk.all(k).(var{1});
            B = sq.all(k).(var{1});
            verifyEqual(test_case, A, 2 * B, 'AbsTol', 2e-6 * max(abs(A(:))), ...
                [var{1}, ': its coefficients (q in the denominator) are twice the 2 q run''s']);
            if ~strcmp(var{1}, 'URDD3')
                C = sb.all(k).(var{1});
                verifyEqual(test_case, A(:, 1:2), 2 * C(:, 1:2), 'AbsTol', 2e-6 * max(abs(A(:))), ...
                    [var{1}, ': the rigid forces double']);
            end
        end
    end
end

function test_fa2j_enters_the_trim_balance(test_case)
    p = test_case.TestData.print;
    sd = read_nastran_trim(p.fa2j, test_case.TestData.surfaces);
    tv = read_nastran_trim_variables(p.fa2j);
    tb = read_nastran_trim_variables(p.base);
    for k = 1:2
        T = sd.all(k);
        % cp 0.1 on 8 m^2 of wing over S = 8: CZ 0.1, at the boxes' quarter
        % chords (x = 0.1875 m on average, the reference point at the
        % origin): CMY = -0.1 x 0.1875 / c
        verifyEqual(test_case, T.REF(3, 1:2), [0.1, 0.1], 'AbsTol', 1e-7, 'rigid CZ of FA2J');
        verifyEqual(test_case, T.REF(5, 1:2), [-0.01875, -0.01875], 'AbsTol', 1e-7, 'rigid CMY of FA2J');
        % the restrained elastic forces of the trim (the intercept plus every
        % variable at its value) balance the inertia
        for i_coef = [3 5]
            s = T.REF(i_coef, 3);
            for j = 1:numel(tv(k).labels)
                var = field_of(tv(k).labels{j});
                s = s + (T.(var)(i_coef, 3) - T.(var)(i_coef, 5)) * tv(k).values(j);
            end
            verifyEqual(test_case, s, 0, 'AbsTol', 1e-6, 'the trim balances with FA2J''s lift');
        end
        verifyLessThan(test_case, tv(k).ANGLEA, tb(k).ANGLEA, 'FA2J''s lift lowers the trimmed angle');
    end
end

%% divergence

function test_divergence_of_a_typical_section(test_case)
    p = test_case.TestData.print.typical;
    sd = read_nastran_trim(p, {});
    cmy_alpha = sd.all(1).ALPHA(5, 2); % rigid splined, about the axis
    K = 1000;
    S = 4;
    c = 1;
    div = read_nastran_divergence(p);
    verifyEqual(test_case, numel(div), 2, 'Mach 0 and 0.5');
    verifyEqual(test_case, [div.mach], [0, 0.5], 'AbsTol', 1e-9);
    verifyEqual(test_case, [div.subcase], [2, 2]);
    verifyEqual(test_case, div(1).q(1), K / (S * c * cmy_alpha), 'RelTol', 2e-6, ...
        'q_d = K / (S c CMY_alpha)');
    verifyEqual(test_case, div(1).p(1), 1i * sqrt(div(1).q(1)), 'RelTol', 1e-6, 'p = i sqrt(q)');
    verifyGreaterThan(test_case, div(1).q(2), 1e6 * div(1).q(1), 'the next roots are the stiff beam''s');
    verifyLessThan(test_case, div(2).q(1), div(1).q(1), 'compressibility lowers it');
    % at q = 1 the restrained CMY_alpha is the rigid one over 1 - q/q_d
    verifyEqual(test_case, sd.all(1).ALPHA(5, 3), cmy_alpha / (1 - 1 / div(1).q(1)), 'RelTol', 1e-5);
    verifyTrue(test_case, all(isnan(sd.all(1).ALPHA(:, 4))), 'no SUPORT: the unrestrained columns are N/A');
end

%% the guide's examples, against Simcenter's prints of the same decks

function test_ha144a_against_simcenter(test_case)
    % HA144A's derivative tables (two Mach 0.9 subcases, q = 40 and 1200;
    % the REF. COEFF. row is its DMI W2GJ, 0.1 deg on the wing) and trim
    % against Simcenter Nastran 2606's print of the same deck, which is the
    % guide's output (Simcenter 2606's guide, Table 6-4) to 1e-6
    % (test_simcenter_tpl_aeroelastic holds Simcenter to the table)
    sim = simcenter_fixture(test_case, 'ha144a');
    sd = read_nastran_trim(test_case.TestData.print.ha144a, {'ELEV'});
    sd_sim = read_nastran_trim(sim, {'ELEV'});
    verifyEqual(test_case, sd.qbar, [0, 40, 1200], 'AbsTol', 1e-9);
    verifyEqual(test_case, sd_sim.qbar, sd.qbar, 'AbsTol', 1e-9);
    for k = 1:2
        for var = {'REF', 'ALPHA', 'PITCH', 'URDD3', 'URDD5', 'ELEV'}
            for i_coef = [3 5] % CZ and CMY, the symmetric half model's
                values = sd_sim.all(k).(var{1})(i_coef, :);
                got = sd.all(k).(var{1})(i_coef, :);
                verifyEqual(test_case, got, values, 'AbsTol', 1.5e-6 * max(abs(values)) + 2e-9, ...
                    sprintf('subcase %d %s coefficient %d: Simcenter''s six columns', k, var{1}, i_coef));
            end
        end
    end
    tv = read_nastran_trim_variables(test_case.TestData.print.ha144a);
    tv_sim = read_nastran_trim_variables(sim);
    verifyEqual(test_case, [tv.ANGLEA], [tv_sim.ANGLEA], 'RelTol', 1e-6);
    verifyEqual(test_case, [tv.ELEV], [tv_sim.ELEV], 'RelTol', 1e-6);
end

function test_ha144b_divergence_against_simcenter(test_case)
    % HA144B's divergence request (Simcenter's guide, Table 2-2) on the
    % symmetric deck: every root against Simcenter's (its complex Lanczos;
    % the guide's printed roots, p. 4-4, to 6-7 digits), and the negative
    % root, p real: q = -p^2 = -72.006
    div = read_nastran_divergence(test_case.TestData.print.ha144b);
    div_sim = read_nastran_divergence(simcenter_fixture(test_case, 'ha144b_diverg_symmetric'));
    verifyEqual(test_case, numel(div), 1);
    verifyEqual(test_case, div.mach, 0, 'AbsTol', 1e-12);
    verifyEqual(test_case, div.q, div_sim.q, 'RelTol', 3e-6, 'Simcenter''s divergence roots');
    neg = div.complex.p(real(div.complex.p) < 0 & abs(imag(div.complex.p)) < 1e-9);
    neg_sim = div_sim.complex.p(real(div_sim.complex.p) < 0 & abs(imag(div_sim.complex.p)) < 1e-9);
    verifyEqual(test_case, neg(1), neg_sim(1), 'RelTol', 3e-6, 'the negative root');
end

function test_ha145c_strip_theory_against_simcenter(test_case)
    % the BAH wing by strip theory (CAERO4), clamped: the static divergence
    % speed (the guide's 1419.9 ft/s, Bisplinghoff, Ashley and Halfman) and
    % the next two against Simcenter's DIVERG of the same deck; RHOREF
    % 1.1468e-7 lb s^2/in^4
    div = read_nastran_divergence(test_case.TestData.print.ha145c);
    div_sim = read_nastran_divergence(simcenter_fixture(test_case, 'ha145c_diverg'));
    verifyEqual(test_case, numel(div), 1);
    rho = 1.1468e-7;
    V_ft_s = sqrt(2 * div.q / rho) / 12;
    V_sim_ft_s = sqrt(2 * div_sim.q / rho) / 12;
    verifyEqual(test_case, V_ft_s(1), V_sim_ft_s(1), 'AbsTol', 0.5, 'the static divergence speed');
    verifyEqual(test_case, V_ft_s(2:3), V_sim_ft_s(2:3), 'RelTol', 1e-3, 'the next two');
end

function test_wkk_weights_the_divergence_unlike_simcenter(test_case)
    % HA144B (antisymmetric) with WKK = 2 I, a roll trim and a divergence
    % subcase, against Simcenter Nastran 2606's print of the same deck. The
    % trim agrees: in both codes WKK multiplies the trim's box forces (2 I
    % at q is the plain deck at 2 q; ROLL 0.2032839 there). The divergence
    % does not, by design: Simcenter's DMAP hands DIVERGRS the unweighted
    % QKKS (scnas/nast/del/aestat.dat; aestatrs.dat forms WQKKS = WKK QKKS
    % for the trim alone), so its roots are the plain deck's, while
    % nastran95ase solves [K_ll - q Q_ll] with Q_ll weighted, as the guide
    % writes it (Simcenter 2606 Aeroelastic Analysis User's Guide, eq.
    % 1-67, p. 1-39, and eqs. 1-110 / 1-111, p. 1-56) and as its own trims
    % see it - its roots are half of Simcenter's. (A WKK deck's Simcenter
    % roots are nastran95ase's of the same deck without WKK.)
    fixture = fullfile(test_case.TestData.here, 'data', 'ha144b_trim_diverg_wkk2_simcenter2606.f06');
    prt = test_case.TestData.print.ha144b_wkk2;
    tv = read_nastran_trim_variables(prt);
    tv_sim = read_nastran_trim_variables(fixture);
    verifyEqual(test_case, tv(1).ROLL, tv_sim(1).ROLL, 'RelTol', 2e-6, 'the roll trim, Simcenter''s');
    verifyEqual(test_case, tv_sim(1).ROLL, 0.08541367, 'RelTol', 1e-7, 'the plain deck''s at 2 q');
    sd = read_nastran_trim(prt, {'AILE'});
    sd_sim = read_nastran_trim(fixture, {'AILE'});
    for var = {'ROLL', 'AILE'}
        B = sd_sim.all(1).(var{1});
        A = sd.all(1).(var{1});
        verifyLessThan(test_case, max(abs(A(:) - B(:))) / max(abs(B(:))), 2e-5, ...
            sprintf('%s derivatives against Simcenter''s', var{1}));
    end
    div = read_nastran_divergence(prt);
    div_sim = read_nastran_divergence(fixture);
    plain = [23.73470, 111.8315, 367.5739, 874.0095, 3028.890];
    verifyEqual(test_case, div_sim.q, plain, 'RelTol', 1e-6, ...
        'Simcenter: the plain deck''s roots, WKK not applied');
    verifyEqual(test_case, div.q, div_sim.q / 2, 'RelTol', 5e-6, ...
        'nastran95ase: the aerodynamics weighted by WKK = 2 I, half the dynamic pressure');
end

%% local functions

function print_file = run_deck(test_case, name, text)
    folder = fullfile(test_case.TestData.out, name);
    mkdir(folder);
    deck = fullfile(folder, [name, '.dat']);
    fid = fopen(deck, 'w');
    fprintf(fid, '%s', text);
    fclose(fid);
    [rc, cmdout] = system(n95_platform('cmd_env', {'N95_TIMEOUT', 5}, ...
        test_case.TestData.exe, deck, folder));
    print_file = fullfile(folder, [name, '.out']);
    assert(rc == 0 || isfile(print_file), '%s: %s', name, cmdout);
end

function text = dmij_w2gj(box_ids, value)
    % DMIJ W2GJ, fixed field: one column, component 3 of every box
    f8 = @(varargin) strjoin(cellfun(@(s) sprintf('%-8s', s), varargin, 'UniformOutput', false), '');
    lines = {f8('DMIJ', 'W2GJ', '0', '9', '2', '0', '', '', '1')};
    v = sprintf('%g', value);
    lines{end+1} = f8('DMIJ', 'W2GJ', '1', '1', '', sprintf('%d', box_ids(1)), '3', v);
    for i = 2:2:numel(box_ids)
        g = {sprintf('%d', box_ids(i)), '3', v, ''};
        if i + 1 <= numel(box_ids)
            g = [g, {sprintf('%d', box_ids(i + 1)), '3', v}]; %#ok<AGROW>
        end
        lines{end+1} = f8('', g{:}); %#ok<AGROW>
    end
    text = [strjoin(lines, newline), newline];
end

function var = field_of(label)
    % read_nastran_trim's field of a table label
    switch label
        case 'REF. COEFF.'
            var = 'REF';
        case 'ANGLEA'
            var = 'ALPHA';
        case 'SIDES'
            var = 'BETA';
        otherwise
            var = regexprep(upper(label), '[^A-Z0-9]', '');
    end
end

function print_file = simcenter_fixture(test_case, name)
    % Simcenter Nastran 2606's print of decks/aeroelastic_guide_examples/
    % <name>.dat (Linux, 2026-10-01), cut to the pages these tests read
    print_file = fullfile(test_case.TestData.here, 'data', [name, '_simcenter2606.f06']);
    assert(isfile(print_file), '%s is missing', print_file);
end
