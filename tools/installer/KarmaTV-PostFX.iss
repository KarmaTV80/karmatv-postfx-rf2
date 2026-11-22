; KarmaTV PostFX - installer per rFactor 2 (Inno Setup 6).
; Non compilare a mano: usa tools\package.ps1, che prepara dist\payload e passa la versione.
;
; L'installer trova rF2 nelle librerie di Steam, copia il pacchetto in una cartella
; temporanea ed esegue install.ps1 -Payload (stessa logica dell'installazione da sorgenti).
; La disinstallazione (App installate di Windows) esegue uninstall.ps1.

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef PayloadDir
  #define PayloadDir "..\..\dist\payload"
#endif
#ifndef OutputDir
  #define OutputDir "..\..\dist"
#endif

[Setup]
AppId={{CE98C0B3-324F-4916-B9E1-E81D0A97C373}
AppName=KarmaTV PostFX per rFactor 2
AppVersion={#AppVersion}
AppVerName=KarmaTV PostFX {#AppVersion}
AppPublisher=KarmaTV
DefaultDirName={code:DefaultRf2Dir}
AppendDefaultDirName=no
DirExistsWarning=no
UsePreviousAppDir=yes
DisableProgramGroupPage=yes
Uninstallable=yes
UninstallFilesDir={app}\Bin64\KarmaTV-PostFX\uninstall
UninstallDisplayName=KarmaTV PostFX per rFactor 2
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=KarmaTV-PostFX-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
SetupLogging=yes

[Languages]
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"

[Messages]
it.SelectDirLabel3=Scegli la cartella principale di rFactor 2 (quella che contiene Bin64).
it.SelectDirBrowseLabel=Di solito è ...\steamapps\common\rFactor 2. Premi Avanti per continuare.

[Files]
; Pacchetto: usato solo durante l'installazione
Source: "{#PayloadDir}\*"; DestDir: "{tmp}\payload"; Flags: recursesubdirs ignoreversion deleteafterinstall
; Script di rimozione, conservato per la disinstallazione
Source: "{#PayloadDir}\uninstall.ps1"; DestDir: "{app}\Bin64\KarmaTV-PostFX\uninstall"; Flags: ignoreversion

[Code]
var
  InstallLog: String;

function SteamLibraries(): TArrayOfString;
var
  steam, vdf, line, path: String;
  lines: TArrayOfString;
  i, n, q: Integer;
begin
  n := 0;
  SetArrayLength(Result, 0);
  if not RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', steam) then
    if not RegQueryStringValue(HKLM, 'SOFTWARE\WOW6432Node\Valve\Steam', 'InstallPath', steam) then
      steam := ExpandConstant('{commonpf32}\Steam');
  StringChangeEx(steam, '/', '\', True);
  SetArrayLength(Result, 1);
  Result[0] := steam;
  n := 1;
  vdf := steam + '\steamapps\libraryfolders.vdf';
  if LoadStringsFromFile(vdf, lines) then
    for i := 0 to GetArrayLength(lines) - 1 do
    begin
      line := Trim(lines[i]);
      if Pos('"path"', line) = 1 then
      begin
        // riga: "path"		"E:\\SteamLibrary"
        path := Copy(line, 7, Length(line));
        q := Pos('"', path);
        if q > 0 then
        begin
          path := Copy(path, q + 1, Length(path));
          q := Pos('"', path);
          if q > 0 then
          begin
            path := Copy(path, 1, q - 1);
            StringChangeEx(path, '\\', '\', True);
            SetArrayLength(Result, n + 1);
            Result[n] := path;
            n := n + 1;
          end;
        end;
      end;
    end;
end;

function DefaultRf2Dir(Param: String): String;
var
  libs: TArrayOfString;
  i: Integer;
  dir: String;
begin
  libs := SteamLibraries();
  Result := ExpandConstant('{commonpf32}\Steam\steamapps\common\rFactor 2');
  for i := 0 to GetArrayLength(libs) - 1 do
  begin
    dir := libs[i] + '\steamapps\common\rFactor 2';
    if FileExists(dir + '\Bin64\rFactor2.exe') then
    begin
      Result := dir;
      exit;
    end;
  end;
end;

function Rf2Running(): Boolean;
var
  code: Integer;
begin
  // tasklist | find: codice 0 se rFactor2.exe e' in esecuzione
  Result := Exec(ExpandConstant('{cmd}'), '/C tasklist /NH /FI "IMAGENAME eq rFactor2.exe" | find /I "rFactor2.exe" >NUL',
                 '', SW_HIDE, ewWaitUntilTerminated, code) and (code = 0);
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    if not FileExists(AddBackslash(WizardDirValue()) + 'Bin64\rFactor2.exe') then
    begin
      MsgBox('In questa cartella non c''è Bin64\rFactor2.exe.' + #13#10 +
             'Scegli la cartella principale di rFactor 2, per esempio:' + #13#10 +
             '...\steamapps\common\rFactor 2', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  waited: Integer;
begin
  Result := '';
  // Aggiornamento dal gioco: l'installer parte mentre rF2 si sta ancora chiudendo
  waited := 0;
  while WizardSilent() and Rf2Running() and (waited < 60) do
  begin
    Sleep(1000);
    waited := waited + 1;
  end;
  if Rf2Running() then
    Result := 'rFactor 2 è aperto. Chiudi il gioco e riprova.';
end;

function PowerShellArgs(script, extra: String): String;
begin
  Result := '-NoProfile -ExecutionPolicy Bypass -File "' + script + '" ' + extra;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  code: Integer;
  bin64, payload, text: String;
  log: AnsiString;
begin
  if CurStep <> ssPostInstall then
    exit;
  bin64 := ExpandConstant('{app}\Bin64');
  payload := ExpandConstant('{tmp}\payload');
  InstallLog := ExpandConstant('{tmp}\install-log.txt');
  WizardForm.StatusLabel.Caption := 'Installazione di ReShade, add-on e shader...';
  if not Exec('powershell.exe',
              PowerShellArgs(payload + '\install.ps1', '-Rf2Bin64 "' + bin64 + '" -Payload "' + payload + '" -Force -LogFile "' + InstallLog + '"'),
              '', SW_HIDE, ewWaitUntilTerminated, code) then
    code := -1;
  CopyFile(InstallLog, bin64 + '\KarmaTV-PostFX\install-log.txt', False);
  if code <> 0 then
  begin
    text := 'L''installazione non è riuscita (codice ' + IntToStr(code) + ').';
    if LoadStringFromFile(InstallLog, log) then
      text := text + #13#10#13#10 + Copy(String(log), Length(log) - 1500, 1500);
    MsgBox(text, mbCriticalError, MB_OK);
    Abort;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if CurPageID <> wpFinished then
    exit;
  if not FileExists(ExpandConstant('{app}\Bin64\Plugins\rFactor2SharedMemoryMapPlugin64.dll')) then
    MsgBox('Manca rF2SharedMemoryMapPlugin (cartella Bin64\Plugins).' + #13#10#13#10 +
           'Gli effetti funzionano lo stesso, ma senza il plugin non ci sono gli automatismi ' +
           '(pioggia, replay, telecamera). Si scarica da GitHub: TheIronWolfModding/rF2SharedMemoryMapPlugin, ' +
           'poi va attivato nelle opzioni Plugin di rF2.', mbInformation, MB_OK);
  WizardForm.FinishedLabel.Caption :=
    'KarmaTV PostFX è installato.' + #13#10#13#10 +
    'Avvia rFactor 2 e premi Canc per aprire il menu KarmaTV.' + #13#10 +
    'Registro dell''installazione: Bin64\KarmaTV-PostFX\install-log.txt';
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  if Rf2Running() then
  begin
    MsgBox('rFactor 2 è aperto. Chiudi il gioco e riprova.', mbError, MB_OK);
    Result := False;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  code: Integer;
  extra: String;
begin
  if CurUninstallStep <> usUninstall then
    exit;
  extra := '-Rf2Bin64 "' + ExpandConstant('{app}\Bin64') + '"';
  if MsgBox('Vuoi eliminare anche i tuoi profili e le impostazioni KarmaTV?' + #13#10 +
            '(No = restano in Bin64\KarmaTV-PostFX, utili se reinstalli)', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    extra := extra + ' -RemovePresets';
  Exec('powershell.exe', PowerShellArgs(ExpandConstant('{app}\Bin64\KarmaTV-PostFX\uninstall\uninstall.ps1'), extra),
       '', SW_HIDE, ewWaitUntilTerminated, code);
end;
