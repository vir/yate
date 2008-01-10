; -- yate.iss --
; Yate script for Inno Setup Compiler.
; http://www.innosetup.com/
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING .ISS SCRIPT FILES!

[Setup]
#include "yateiss.inc"
AppName=Yet Another Telephony Engine
AppPublisher=Null Team Impex SRL
AppPublisherURL=http://yate.null.ro/
DefaultDirName={pf}\Yate
DefaultGroupName=Yate
UninstallDisplayIcon={app}\null_team.ico
Compression=lzma
SolidCompression=yes
OutputBaseFilename=yate-setup
SetupIconFile=null_team.ico

[Types]
Name: "full"; Description: "Full installation"
Name: "client"; Description: "VoIP client installation"
Name: "server"; Description: "Server installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom
Name: "engine"; Description: "Engine only (unlikely)"

[Components]
Name: "engine"; Description: "Engine library"; Types: full client server engine custom; Flags: fixed
Name: "client"; Description: "VoIP Clients"; Types: full client
Name: "client\qt"; Description: "Client based on Qt"; Types: full client
Name: "client\qt\run"; Description: "Qt runtime libraries"; Types: full client
Name: "client\gtk"; Description: "Client based on Gtk"; Types: full
Name: "client\gtk\skinned"; Description: "Graphical skin"; Types: full
Name: "server"; Description: "Server files"; Types: full server
Name: "server\cluster"; Description: "Server clustering modules"; Types: full
Name: "driver"; Description: "Protocol drivers"; Types: full client server
Name: "driver\base"; Description: "Files, tones, mixers"; Types: full client server custom
Name: "driver\sip"; Description: "SIP Protocol driver"; Types: full client server
Name: "driver\h323"; Description: "H.323 Protocol driver"; Types: full client server
Name: "driver\iax"; Description: "IAX Protocol driver"; Types: full client server
Name: "driver\jingle"; Description: "Jingle Protocol driver"; Types: full server
;Name: "driver\wp"; Description: "Wanpipe card driver"; Types: full server
Name: "database"; Description: "Database drivers"; Types: full server
Name: "database\my"; Description: "MySQL database driver"; Types: full server
Name: "database\my\run"; Description: "MySQL client libraries"; Types: full server
Name: "database\pg"; Description: "PostgreSQL database driver"; Types: full server
Name: "database\pg\run"; Description: "PostgreSQL client libraries"; Types: full server
Name: "codecs"; Description: "Audio codecs"; Types: full client server
Name: "codecs\gsm"; Description: "GSM codec"; Types: full client server
Name: "codecs\ilbc"; Description: "iLBC codec"; Types: full client server
Name: "external"; Description: "External interfaces"; Types: full server
Name: "external\php"; Description: "PHP5 scripting"; Types: full server
Name: "debug"; Description: "Extra debugging support"; Types: full engine
Name: "devel"; Description: "Module development files"; Types: full engine
Name: "devel\doc"; Description: "Development documentation"; Types: full

[Tasks]
Name: "qlaunch"; Description: "Create a &Quick Launch icon"; GroupDescription: "Additional icons:"; Components: client; Flags: unchecked
Name: "desktop"; Description: "Create a &Desktop icon"; GroupDescription: "Additional icons:"; Components: client; Flags: unchecked

[Files]
Source: "Release\libyate.dll"; DestDir: "{app}"; Components: engine
Source: "Release\libyqt4.dll"; DestDir: "{app}"; Components: client\qt
Source: "Release\yate-qt4.exe"; DestDir: "{app}"; Components: client\qt
Source: "Release\libygtk2.dll"; DestDir: "{app}"; Components: client\gtk
Source: "Release\yate-gtk2.exe"; DestDir: "{app}"; Components: client\gtk
;Source: "..\share\sounds\ring.wav"; DestDir: "{app}\sounds"; Components: client
Source: "Runtimes\qtcore4.dll"; DestDir: "{app}"; Components: client\qt\run
Source: "Runtimes\qtgui4.dll"; DestDir: "{app}"; Components: client\qt\run
Source: "Runtimes\qtxml4.dll"; DestDir: "{app}"; Components: client\qt\run
Source: "Release\yate-service.exe"; DestDir: "{app}"; Components: server
Source: "Release\yate-console.exe"; DestDir: "{app}"; Components: debug

Source: "Release\server\accfile.yate"; DestDir: "{app}\modules\server"; Components: client server
Source: "Release\analyzer.yate"; DestDir: "{app}\modules"; Components: server debug
Source: "Release\callfork.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\callgen.yate"; DestDir: "{app}\modules"; Components: debug
Source: "Release\cdrbuild.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\cdrfile.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\conference.yate"; DestDir: "{app}\modules"; Components: driver\base
Source: "Release\client\dsoundchan.yate"; DestDir: "{app}\modules\client"; Components: client
Source: "Release\dumbchan.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\extmodule.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\msgsniff.yate"; DestDir: "{app}\modules"; Components: debug
Source: "Release\regexroute.yate"; DestDir: "{app}\modules"; Components: client server debug
Source: "Release\server\regfile.yate"; DestDir: "{app}\modules\server"; Components: server
Source: "Release\rmanager.yate"; DestDir: "{app}\modules"; Components: server debug
Source: "Release\tonegen.yate"; DestDir: "{app}\modules"; Components: driver\base
Source: "Release\tonedetect.yate"; DestDir: "{app}\modules"; Components: driver\base
Source: "Release\wavefile.yate"; DestDir: "{app}\modules"; Components: driver\base
Source: "Release\server\yradius.yate"; DestDir: "{app}\modules\server"; Components: server
Source: "Release\server\register.yate"; DestDir: "{app}\modules\server"; Components: server
Source: "Release\server\dbpbx.yate"; DestDir: "{app}\modules\server"; Components: server
Source: "Release\pbx.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\server\pbxassist.yate"; DestDir: "{app}\modules\server"; Components: server
Source: "Release\server\park.yate"; DestDir: "{app}\modules\server"; Components: server
Source: "Release\server\queues.yate"; DestDir: "{app}\modules\server"; Components: server
Source: "Release\server\sipfeatures.yate"; DestDir: "{app}\modules\server"; Components: server

Source: "Release\server\heartbeat.yate"; DestDir: "{app}\modules\server"; Components: server\cluster
Source: "Release\server\mgcpca.yate"; DestDir: "{app}\modules\server"; Components: server\cluster
Source: "Release\server\mgcpgw.yate"; DestDir: "{app}\modules\server"; Components: server\cluster

;Source: "Release\wpchan.yate"; DestDir: "{app}\modules"; Components: driver\wp
Source: "Release\yrtpchan.yate"; DestDir: "{app}\modules"; Components: driver\sip driver\h323 driver\jingle
Source: "Release\ysipchan.yate"; DestDir: "{app}\modules"; Components: driver\sip
Source: "Release\h323chan.yate"; DestDir: "{app}\modules"; Components: driver\h323
Source: "Release\yiaxchan.yate"; DestDir: "{app}\modules"; Components: driver\iax
Source: "Release\yjinglechan.yate"; DestDir: "{app}\modules"; Components: driver\jingle
Source: "Release\ystunchan.yate"; DestDir: "{app}\modules"; Components: driver\jingle

Source: "Release\gsmcodec.yate"; DestDir: "{app}\modules"; Components: codecs\gsm
Source: "Release\ilbccodec.yate"; DestDir: "{app}\modules"; Components: codecs\ilbc

Source: "Release\server\mysqldb.yate"; DestDir: "{app}\modules\server"; Components: database\my
Source: "Runtimes\libmysql.dll"; DestDir: "{app}"; Components: database\my\run
Source: "Release\server\pgsqldb.yate"; DestDir: "{app}\modules\server"; Components: database\pg
Source: "Runtimes\libpq.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\comerr32.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\libeay32.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\ssleay32.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\krb5_32.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\libintl-2.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\libiconv-2.dll"; DestDir: "{app}"; Components: database\pg\run

Source: "..\COPYING"; DestName: "COPYING.txt"; DestDir: "{app}"
Source: "..\packing\yate.url"; DestDir: "{app}"
Source: "null_team.ico"; DestDir: "{app}"
Source: "..\conf.d\*.conf.sample"; DestDir: "{app}\conf.d"

Source: "..\share\help\*.yhlp"; DestDir: "{app}\share\help"; Components: client
Source: "..\conf.d\providers.conf.default"; DestName: "providers.conf"; DestDir: "{app}\conf.d"; Components: client
Source: "..\share\skins\default\qt4client.??"; DestDir: "{app}\share\skins\default"; Components: client\qt
Source: "..\share\skins\default\account.ui"; DestDir: "{app}\share\skins\default"; Components: client\qt
Source: "..\share\skins\default\addrbook.ui"; DestDir: "{app}\share\skins\default"; Components: client\qt
Source: "..\share\skins\default\confirm.ui"; DestDir: "{app}\share\skins\default"; Components: client\qt
Source: "..\share\skins\default\events.ui"; DestDir: "{app}\share\skins\default"; Components: client\qt
Source: "..\share\skins\default\help.ui"; DestDir: "{app}\share\skins\default"; Components: client\qt
Source: "..\share\skins\default\gtk2client.??"; DestDir: "{app}\share\skins\default"; Components: client\gtk
Source: "..\share\skins\default\*.png"; DestDir: "{app}\share\skins\default"; Components: client

Source: "..\share\skins\skinned\gtk2client.??"; DestDir: "{app}\share\skins\skinned"; Components: client\gtk\skinned
Source: "..\share\skins\skinned\*.png"; DestDir: "{app}\share\skins\skinned"; Components: client\gtk\skinned
Source: "..\conf.d\yate-qt4.conf.default"; DestName: "yate-qt4.conf"; DestDir: "{app}\conf.d"; Components: client\qt; Flags: skipifsourcedoesntexist
Source: "..\conf.d\yate-gtk2.conf.default"; DestName: "yate-gtk2.conf"; DestDir: "{app}\conf.d"; Components: client\gtk\skinned; Flags: skipifsourcedoesntexist

Source: "..\share\scripts\*.php"; DestDir: "{app}\share\scripts"; Components: external\php

Source: "Release\libyate.lib"; DestDir: "{app}\devel"; Components: devel
Source: "..\yate*.h"; DestDir: "{app}\devel"; Components: devel
Source: "yateversn.h"; DestDir: "{app}\devel"; Components: devel
Source: "version.rc"; DestDir: "{app}\devel"; Components: devel
Source: "..\clients\gtk2\gtk2client.h"; DestDir: "{app}\devel"; Components: devel
Source: "Release\libygtk2.lib"; DestDir: "{app}\devel"; Components: devel
Source: "..\clients\qt4\qt4client.h"; DestDir: "{app}\devel"; Components: devel
Source: "Release\libyqt4.lib"; DestDir: "{app}\devel"; Components: devel
Source: "..\README"; DestName: "README.txt"; DestDir: "{app}\devel"; Components: devel
Source: "..\ChangeLog"; DestName: "ChangeLog.txt"; DestDir: "{app}\devel"; Components: devel
Source: "..\docs\*.html"; DestDir: "{app}\devel\docs"; Components: devel\doc
Source: "..\docs\api\*.html"; DestDir: "{app}\devel\docs\api"; Components: devel\doc; Flags: skipifsourcedoesntexist
Source: "..\docs\api\*.png"; DestDir: "{app}\devel\docs\api"; Components: devel\doc; Flags: skipifsourcedoesntexist
Source: "..\docs\api\*.css"; DestDir: "{app}\devel\docs\api"; Components: devel\doc; Flags: skipifsourcedoesntexist

Source: "Release\msvcrtest.dll"; Flags: dontcopy
Source: "Runtimes\vcredist_x86.exe"; DestDir: "{app}"; Flags: skipifsourcedoesntexist dontcopy nocompression
Source: "Runtimes\gtk+-2.6.9-setup.exe"; DestDir: "{app}"; Components: client; Flags: skipifsourcedoesntexist dontcopy nocompression

[Icons]
Name: "{group}\Yate Client (Qt)"; Filename: "{app}\yate-qt4.exe"; Parameters: "-n yate-qt4 -w ""{app}"""; Components: client\qt
Name: "{group}\Yate Client (Gtk)"; Filename: "{app}\yate-gtk2.exe"; Parameters: "-n yate-gtk2 -w ""{app}"""; Components: client\gtk
Name: "{group}\Yate Console"; Filename: "{app}\yate-console.exe"; Parameters: "-n yate-console -w ""{app}"""; Components: debug
Name: "{group}\Register Service"; Filename: "{app}\yate-service.exe"; Parameters: "--install -w ""{app}"""; Components: server
Name: "{group}\Unregister Service"; Filename: "{app}\yate-service.exe"; Parameters: "--remove"; Components: server
Name: "{group}\Uninstall"; Filename: "{uninstallexe}"
Name: "{group}\Yate Web Site"; Filename: "{app}\yate.url"
Name: "{group}\Developer docs"; Filename: "{app}\devel\docs\index.html"; Components: devel\doc
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\Yate Client (Qt)"; Filename: "{app}\yate-qt4.exe"; Parameters: "-n yate-qt4 -w ""{app}"""; Components: client\qt; Tasks: qlaunch
Name: "{userdesktop}\Yate Client (Qt)"; Filename: "{app}\yate-qt4.exe"; Parameters: "-n yate-qt4 -w ""{app}"""; Components: client\qt; Tasks: desktop
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\Yate Client (Gtk)"; Filename: "{app}\yate-gtk2.exe"; Parameters: "-n yate-gtk2 -w ""{app}"""; Components: client\gtk; Tasks: qlaunch
Name: "{userdesktop}\Yate Client (Gtk)"; Filename: "{app}\yate-gtk2.exe"; Parameters: "-n yate-gtk2 -w ""{app}"""; Components: client\gtk; Tasks: desktop

[Run]
Filename: "{app}\yate-service.exe"; Description: "Register service"; Parameters: "--install -w ""{app}"""; Components: server
Filename: "net.exe"; Description: "Start service"; Components: server; Parameters: "start yate"; Flags: postinstall skipifsilent unchecked
Filename: "{app}\yate-qt4.exe"; Parameters: "-n yate-qt4 -w ""{app}"""; Description: "Launch Qt client"; Components: client\qt; Flags: postinstall nowait skipifsilent unchecked
Filename: "{app}\yate-gtk2.exe"; Parameters: "-n yate-gtk2 -w ""{app}"""; Description: "Launch Gtk client"; Components: client\gtk; Flags: postinstall nowait skipifsilent unchecked

[UninstallRun]
Filename: "net.exe"; Parameters: "stop yate"; Components: server
Filename: "{app}\yate-service.exe"; Parameters: "--remove"; Components: server

[Code]
var
    CrtLoadable : Boolean;
    GtkRegistry : Boolean;
    GtkLoadable : Boolean;

function CrtTrue() : Integer;
external 'crt_true@files:msvcrtest.dll cdecl delayload setuponly';

function GtkTrue() : Integer;
external 'gtk_true@LIBGTK-WIN32-2.0-0.DLL stdcall delayload setuponly';

function InitializeSetup() : Boolean;
begin
    try
        CrtLoadable := CrtTrue() <> 0;
        UnloadDLL('MSVCRTEST.DLL');
    except
        CrtLoadable := False;
    end;
    GtkRegistry := RegValueExists(HKEY_LOCAL_MACHINE, 'SOFTWARE\GTK\2.0', 'DllPath');
    try
        GtkLoadable := GtkTrue() <> 0;
        UnloadDLL('LIBGTK-WIN32-2.0-0.DLL');
    except
        GtkLoadable := False;
    end;
    Result := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
    msg : String;
    url : String;
    err : Integer;
begin
    if (CurStep = ssInstall) then begin
        if not CrtLoadable then begin
            msg := 'MSVCR80.DLL is not installed or loadable';
            msg := msg + #13 #13 'Do you want to install Microsoft Runtime 8.0 now?';
            repeat
                err := SuppressibleMsgBox(msg, mbConfirmation, MB_YESNOCANCEL, IDNO);
                if (err = IDCANCEL) and ExitSetupMsgBox() then Abort;
            until err <> IDCANCEL;
            if err = IDYES then begin
                url := 'vcredist_x86.exe';
                try
                    ExtractTemporaryFile(url);
                    url := ExpandConstant('{tmp}\') + url;
                    CrtLoadable := FileExists(url) and ShellExec('open', url, '', '', SW_SHOW, ewWaitUntilTerminated, err);
                except
                end;
                if not CrtLoadable then begin
                  url := 'http://yate.null.ro/msvcr80.php';
                  if not ShellExec('open', url, '', '', SW_SHOW, ewNoWait, err) then
                      MsgBox('Browser failed. Please go to:' #13 + url,mbError,MB_OK);
                end;
            end;
        end;
        if IsComponentSelected('client\gtk') and not (GtkRegistry and GtkLoadable) then begin
            msg := 'Gtk2 client installation requested' #13 'but Gtk2 is not ';
            if GtkRegistry then msg := msg + 'loadable'
            else if GtkLoadable then msg := msg + 'in Registry'
            else msg := msg + 'installed';
            msg := msg + #13 #13 'Do you want to install Gtk2 now?';
            repeat
                err := SuppressibleMsgBox(msg, mbConfirmation, MB_YESNOCANCEL, IDNO);
                if (err = IDCANCEL) and ExitSetupMsgBox() then Abort;
            until err <> IDCANCEL;
            if err = IDYES then begin
                url := 'gtk+-2.6.9-setup.exe';
                try
                    ExtractTemporaryFile(url);
                    url := ExpandConstant('{tmp}\') + url;
                    if FileExists(url) and ShellExec('open', url, '', '', SW_SHOW, ewWaitUntilTerminated, err) then
                        exit;
                except
                end;
                url := 'http://yate.null.ro/gtk2win.php';
                if not ShellExec('open', url, '', '', SW_SHOW, ewNoWait, err) then
                    MsgBox('Browser failed. Please go to:' #13 + url,mbError,MB_OK);
            end;
        end;
    end;
end;

