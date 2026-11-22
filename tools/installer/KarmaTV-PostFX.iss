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
AppName=KarmaTV PostFX for rFactor2
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
UninstallDisplayName=KarmaTV PostFX for rFactor2
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=KarmaTV-PostFX-Setup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=auto
SetupLogging=yes

[Languages]
; Inglese per primo: e' la lingua usata quando quella di Windows non e' tra queste
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "it"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "es"; MessagesFile: "compiler:Languages\Spanish.isl"
Name: "fr"; MessagesFile: "compiler:Languages\French.isl"
Name: "de"; MessagesFile: "compiler:Languages\German.isl"
Name: "pt"; MessagesFile: "compiler:Languages\BrazilianPortuguese.isl"

[Messages]
en.SelectDirLabel3=Choose the main rFactor 2 folder (the one that contains Bin64).
en.SelectDirBrowseLabel=It's usually ...\steamapps\common\rFactor 2. Click Next to continue.
it.SelectDirLabel3=Scegli la cartella principale di rFactor 2 (quella che contiene Bin64).
it.SelectDirBrowseLabel=Di solito è ...\steamapps\common\rFactor 2. Premi Avanti per continuare.
es.SelectDirLabel3=Elige la carpeta principal de rFactor 2 (la que contiene Bin64).
es.SelectDirBrowseLabel=Suele ser ...\steamapps\common\rFactor 2. Pulsa Siguiente para continuar.
fr.SelectDirLabel3=Choisissez le dossier principal de rFactor 2 (celui qui contient Bin64).
fr.SelectDirBrowseLabel=C'est généralement ...\steamapps\common\rFactor 2. Cliquez sur Suivant pour continuer.
de.SelectDirLabel3=Wähle den Hauptordner von rFactor 2 (der Ordner mit Bin64).
de.SelectDirBrowseLabel=Meist ist es ...\steamapps\common\rFactor 2. Klicke auf Weiter, um fortzufahren.
pt.SelectDirLabel3=Escolha a pasta principal do rFactor 2 (a que contém Bin64).
pt.SelectDirBrowseLabel=Normalmente é ...\steamapps\common\rFactor 2. Clique em Avançar para continuar.

[CustomMessages]
en.NotRf2Dir=This folder doesn't contain Bin64\rFactor2.exe.%nChoose the main rFactor 2 folder, for example:%n...\steamapps\common\rFactor 2
it.NotRf2Dir=In questa cartella non c'è Bin64\rFactor2.exe.%nScegli la cartella principale di rFactor 2, per esempio:%n...\steamapps\common\rFactor 2
es.NotRf2Dir=Esta carpeta no contiene Bin64\rFactor2.exe.%nElige la carpeta principal de rFactor 2, por ejemplo:%n...\steamapps\common\rFactor 2
fr.NotRf2Dir=Ce dossier ne contient pas Bin64\rFactor2.exe.%nChoisissez le dossier principal de rFactor 2, par exemple :%n...\steamapps\common\rFactor 2
de.NotRf2Dir=Dieser Ordner enthält kein Bin64\rFactor2.exe.%nWähle den Hauptordner von rFactor 2, zum Beispiel:%n...\steamapps\common\rFactor 2
pt.NotRf2Dir=Esta pasta não contém Bin64\rFactor2.exe.%nEscolha a pasta principal do rFactor 2, por exemplo:%n...\steamapps\common\rFactor 2
en.Rf2Open=rFactor 2 is running. Close the game and try again.
it.Rf2Open=rFactor 2 è aperto. Chiudi il gioco e riprova.
es.Rf2Open=rFactor 2 está abierto. Cierra el juego y vuelve a intentarlo.
fr.Rf2Open=rFactor 2 est ouvert. Fermez le jeu et réessayez.
de.Rf2Open=rFactor 2 läuft. Beende das Spiel und versuche es erneut.
pt.Rf2Open=O rFactor 2 está aberto. Feche o jogo e tente de novo.
en.Installing=Installing ReShade, add-on and shaders...
it.Installing=Installazione di ReShade, add-on e shader...
es.Installing=Instalando ReShade, el add-on y los shaders...
fr.Installing=Installation de ReShade, de l'add-on et des shaders...
de.Installing=ReShade, Add-on und Shader werden installiert...
pt.Installing=Instalando o ReShade, o add-on e os shaders...
en.InstallFailed=Installation failed (code %1).
it.InstallFailed=L'installazione non è riuscita (codice %1).
es.InstallFailed=La instalación falló (código %1).
fr.InstallFailed=L'installation a échoué (code %1).
de.InstallFailed=Installation fehlgeschlagen (Code %1).
pt.InstallFailed=A instalação falhou (código %1).
en.PluginMissing=rF2SharedMemoryMapPlugin is missing (Bin64\Plugins folder).%n%nThe effects work anyway, but without the plugin there's no automation (rain, replays, camera). Download it from GitHub: TheIronWolfModding/rF2SharedMemoryMapPlugin, then enable it in rF2's Plugins options.
it.PluginMissing=Manca rF2SharedMemoryMapPlugin (cartella Bin64\Plugins).%n%nGli effetti funzionano lo stesso, ma senza il plugin non ci sono gli automatismi (pioggia, replay, telecamera). Si scarica da GitHub: TheIronWolfModding/rF2SharedMemoryMapPlugin, poi va attivato nelle opzioni Plugin di rF2.
es.PluginMissing=Falta rF2SharedMemoryMapPlugin (carpeta Bin64\Plugins).%n%nLos efectos funcionan igualmente, pero sin el plugin no hay automatismos (lluvia, repeticiones, cámara). Descárgalo de GitHub: TheIronWolfModding/rF2SharedMemoryMapPlugin y actívalo en las opciones de Plugins de rF2.
fr.PluginMissing=rF2SharedMemoryMapPlugin est absent (dossier Bin64\Plugins).%n%nLes effets fonctionnent quand même, mais sans le plugin il n'y a pas d'automatismes (pluie, replays, caméra). Téléchargez-le sur GitHub : TheIronWolfModding/rF2SharedMemoryMapPlugin, puis activez-le dans les options Plugins de rF2.
de.PluginMissing=rF2SharedMemoryMapPlugin fehlt (Ordner Bin64\Plugins).%n%nDie Effekte funktionieren trotzdem, aber ohne das Plugin gibt es keine Automatik (Regen, Replays, Kamera). Lade es von GitHub herunter: TheIronWolfModding/rF2SharedMemoryMapPlugin, und aktiviere es in den Plugin-Optionen von rF2.
pt.PluginMissing=Falta o rF2SharedMemoryMapPlugin (pasta Bin64\Plugins).%n%nOs efeitos funcionam mesmo assim, mas sem o plugin não há automações (chuva, replays, câmera). Baixe no GitHub: TheIronWolfModding/rF2SharedMemoryMapPlugin e ative nas opções de Plugins do rF2.
en.Finished=KarmaTV PostFX is installed.%n%nStart rFactor 2 and press Delete to open the KarmaTV menu.%nInstall log: Bin64\KarmaTV-PostFX\install-log.txt
it.Finished=KarmaTV PostFX è installato.%n%nAvvia rFactor 2 e premi Canc per aprire il menu KarmaTV.%nRegistro dell'installazione: Bin64\KarmaTV-PostFX\install-log.txt
es.Finished=KarmaTV PostFX está instalado.%n%nInicia rFactor 2 y pulsa Supr para abrir el menú KarmaTV.%nRegistro de instalación: Bin64\KarmaTV-PostFX\install-log.txt
fr.Finished=KarmaTV PostFX est installé.%n%nLancez rFactor 2 et appuyez sur Suppr pour ouvrir le menu KarmaTV.%nJournal d'installation : Bin64\KarmaTV-PostFX\install-log.txt
de.Finished=KarmaTV PostFX ist installiert.%n%nStarte rFactor 2 und drücke Entf, um das KarmaTV-Menü zu öffnen.%nInstallationsprotokoll: Bin64\KarmaTV-PostFX\install-log.txt
pt.Finished=O KarmaTV PostFX está instalado.%n%nAbra o rFactor 2 e pressione Delete para abrir o menu KarmaTV.%nRegistro da instalação: Bin64\KarmaTV-PostFX\install-log.txt
en.RemoveProfiles=Also delete your KarmaTV profiles and settings?%n(No = they stay in Bin64\KarmaTV-PostFX, handy if you reinstall)
it.RemoveProfiles=Vuoi eliminare anche i tuoi profili e le impostazioni KarmaTV?%n(No = restano in Bin64\KarmaTV-PostFX, utili se reinstalli)
es.RemoveProfiles=¿Eliminar también tus perfiles y ajustes de KarmaTV?%n(No = se quedan en Bin64\KarmaTV-PostFX, útiles si reinstalas)
fr.RemoveProfiles=Supprimer aussi vos profils et réglages KarmaTV ?%n(Non = ils restent dans Bin64\KarmaTV-PostFX, utiles si vous réinstallez)
de.RemoveProfiles=Auch deine KarmaTV-Profile und Einstellungen löschen?%n(Nein = sie bleiben in Bin64\KarmaTV-PostFX, praktisch bei einer Neuinstallation)
pt.RemoveProfiles=Excluir também seus perfis e configurações do KarmaTV?%n(Não = ficam em Bin64\KarmaTV-PostFX, úteis se reinstalar)

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
      MsgBox(CustomMessage('NotRf2Dir'), mbError, MB_OK);
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
    Result := CustomMessage('Rf2Open');
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
  WizardForm.StatusLabel.Caption := CustomMessage('Installing');
  if not Exec('powershell.exe',
              PowerShellArgs(payload + '\install.ps1', '-Rf2Bin64 "' + bin64 + '" -Payload "' + payload + '" -Force -LogFile "' + InstallLog + '"'),
              '', SW_HIDE, ewWaitUntilTerminated, code) then
    code := -1;
  CopyFile(InstallLog, bin64 + '\KarmaTV-PostFX\install-log.txt', False);
  if code <> 0 then
  begin
    text := FmtMessage(CustomMessage('InstallFailed'), [IntToStr(code)]);
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
    MsgBox(CustomMessage('PluginMissing'), mbInformation, MB_OK);
  WizardForm.FinishedLabel.Caption := CustomMessage('Finished');
end;

function InitializeUninstall(): Boolean;
begin
  Result := True;
  if Rf2Running() then
  begin
    MsgBox(CustomMessage('Rf2Open'), mbError, MB_OK);
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
  if MsgBox(CustomMessage('RemoveProfiles'), mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    extra := extra + ' -RemovePresets';
  Exec('powershell.exe', PowerShellArgs(ExpandConstant('{app}\Bin64\KarmaTV-PostFX\uninstall\uninstall.ps1'), extra),
       '', SW_HIDE, ewWaitUntilTerminated, code);
end;
