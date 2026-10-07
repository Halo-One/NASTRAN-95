function out = n95_platform(what, varargin)
% n95_platform - the one place the NASTRAN tests decide what operating
% system they are on.
%
%   n95_platform('exe', nastran_dir, 'nastran95ase')   full path to a deliverable
%   n95_platform('cmd', exe, arg1, arg2, ...)          a command line for system()
%   n95_platform('cmd_env', env, exe, arg1, ...)       the same with environment
%                                                      variables set for that one
%                                                      command (env: {'NAME', value, ...})
%   n95_platform('which', 'nastran')                   first match on the PATH, or ''
%   n95_platform('simcenter')                          Simcenter Nastran's launcher,
%                                                      or '' when there is none
%   n95_platform('simcenter_cmd', exe, deck, out_dir)  a command line that runs a
%                                                      deck through it, waited for
%   n95_platform('running', 'analysis')                true when a process of that
%                                                      name is running
%   n95_platform('readers')                            VehicleDesign's utilities/jhc_library
%                                                      (the read_nastran_* readers the
%                                                      tests use), or '' when there is none
%
% The two build scripts in standalone/build produce differently named
% deliverables from the same source - nastran95.exe and nastran95ase.exe on
% Windows, nastran95 and nastran95ase on Linux - and each writes its own
% build record (BUILD_INFO.txt, BUILD_INFO_linux.txt). Everything else that
% differs between the two is a shell habit, and all of it is below. A new
% Windows-ism in a test belongs in here, not in an ispc at the call site.

switch lower(char(what))

    case 'exe'
        % nastran95ase.exe or nastran95ase; the caller names the stem
        nastran_dir = char(varargin{1});
        stem        = char(varargin{2});
        if ispc
            out = fullfile(nastran_dir, [stem '.exe']);
        else
            out = fullfile(nastran_dir, stem);
        end

    case 'cmd'
        out = command_line({}, varargin{:});

    case 'cmd_env'
        out = command_line(varargin{1}, varargin{2:end});

    case 'which'
        % where a bare name on the PATH resolves to - the file system() would
        % actually run. `where` lists every match in PATH order and exits 1
        % when there is none; `command -v` is the POSIX spelling
        name = char(varargin{1});
        if ispc
            [rc, txt] = system(['where ' name]);
        else
            [rc, txt] = system(['command -v ' name]);
        end
        out = '';
        if rc == 0
            matches = strsplit(strtrim(txt), newline);
            out = strtrim(matches{1});
        end

    case 'simcenter'
        % Simcenter Nastran's launcher: SIMCENTER_NASTRAN (the full path of
        % its nastran executable) when set, else the nastran on the PATH
        % when it is Simcenter's - not the repo's shim, nor any other
        % NASTRAN - else the newest default install (on linux also a
        % user install under ~/opt/simcenter_nastran)
        out = getenv('SIMCENTER_NASTRAN');
        if ~isempty(out)
            return
        end
        on_path = n95_platform('which', 'nastran');
        if contains(lower(on_path), 'simcenter')
            out = on_path;
            return
        end
        if ispc
            installs = dir(fullfile(getenv('ProgramFiles'), 'Siemens', ...
                'SimcenterNastran_*', 'bin', 'nastran.exe'));
        else
            installs = [dir('/opt/Siemens/SimcenterNastran_*/bin/nastran'); ...
                dir(fullfile(getenv('HOME'), 'opt', 'simcenter_nastran', 'bin', 'nastran'))];
        end
        out = '';
        if ~isempty(installs)
            [~, i_new] = max([installs.datenum]);
            out = fullfile(installs(i_new).folder, installs(i_new).name);
        end

    case 'simcenter_cmd'
        % the deck through Simcenter, the print files into out_dir as
        % <stem>.f06 etc.; the windows launcher always waits for the job
        % (batch= is a linux keyword there, answered with a warning), the
        % linux one only with batch=no
        exe  = char(varargin{1});
        deck = char(varargin{2});
        out_dir = char(varargin{3});
        out = command_line({}, exe, deck);
        out = [out ' scr=yes out="' out_dir '"'];
        if ~ispc
            out = [out ' batch=no'];
        end

    case 'readers'
        % the readers are VehicleDesign's, not this repository's: the
        % checkout VEHICLEDESIGN_ROOT names, else one beside this fork
        % (../VehicleDesign); '' when neither has utilities/jhc_library
        root = getenv('VEHICLEDESIGN_ROOT');
        if isempty(root)
            fork = fileparts(fileparts(fileparts(mfilename('fullpath'))));
            root = fullfile(fileparts(fork), 'VehicleDesign');
        end
        out = fullfile(root, 'utilities', 'jhc_library');
        if ~isfolder(out)
            out = '';
        end

    case 'running'
        % a process of this name (Simcenter's analysis.exe on windows,
        % analysis on linux); the site has one Simcenter seat, so
        % a second job cannot check out a licence while one runs
        name = char(varargin{1});
        if ispc
            [~, tasks] = system('tasklist');
            out = contains(tasks, [name '.exe']);
        else
            [rc, ~] = system(['pgrep -x ' name ' > /dev/null']);
            out = rc == 0;
        end

    otherwise
        error('n95_platform: unknown request "%s"', char(what));
end
end

function out = command_line(env, exe, varargin)
% a command line for system(). matlab hands the string to cmd.exe on
% windows and to /bin/sh elsewhere. cmd.exe strips the outer quotes from
% a line that starts with a quoted path whenever more than two quote
% characters are present, and "call" is what stops it; sh needs no such
% thing and would take the word call as a command. An environment
% variable for one command is "set NAME=value&& " in cmd.exe (no space
% before the &&, or the value ends in one) and "NAME=value " in sh.
args = '';
for k = 1:numel(varargin)
    args = [args ' "' char(varargin{k}) '"'];  %#ok<AGROW>
end
prefix = '';
for k = 1:2:numel(env)
    value = env{k + 1};
    if isnumeric(value)
        value = num2str(value);
    end
    if ispc
        prefix = [prefix 'set ' env{k} '=' char(value) '&& '];  %#ok<AGROW>
    else
        prefix = [prefix env{k} '=' char(value) ' '];  %#ok<AGROW>
    end
end
if ispc
    out = [prefix 'call "' char(exe) '"' args];
else
    out = [prefix '"' char(exe) '"' args];
end
end
