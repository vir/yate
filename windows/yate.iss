; -- yate.iss --
; Yate script for Inno Setup Compiler.
; http://www.innosetup.com/
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING .ISS SCRIPT FILES!

[Setup]
AppName=Yet Another Telephony Engine
AppVerName=Yate version 0.9.0
AppPublisher=Null Team Impex SRL
AppPublisherURL=http://yate.null.ro/
AppVersion=0.9.0
VersionInfoVersion=0.9.0
DefaultDirName={pf}\Yate
DefaultGroupName=Yate
UninstallDisplayIcon={app}\null_team.ico
Compression=lzma
SolidCompression=yes
OutputBaseFilename=yate-setup

[Types]
Name: "full"; Description: "Full installation"
Name: "client"; Description: "VoIP client installation"
Name: "server"; Description: "Server installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom
Name: "engine"; Description: "Engine only (unlikely)"

[Components]
Name: "engine"; Description: "Engine library"; Types: full client server engine custom; Flags: fixed
Name: "client"; Description: "Client files"; Types: full client
Name: "client\skin"; Description: "Client skins"; Types: full client
Name: "client\skin\tabbed"; Description: "Tabbed skin"; Types: full client
Name: "server"; Description: "Server files"; Types: full server
Name: "driver"; Description: "Protocol drivers"; Types: full client server
Name: "driver\base"; Description: "Files, tones, mixers"; Types: full client server custom
Name: "driver\sip"; Description: "SIP Protocol driver"; Types: full client server
Name: "driver\h323"; Description: "H.323 Protocol driver"; Types: full client server
Name: "driver\h323\run"; Description: "OpenH323 library"; Types: full client server
Name: "driver\iax"; Description: "IAX Protocol driver"; Types: full client server
Name: "driver\wp"; Description: "Wanpipe card driver"; Types: full server
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

[Tasks]
Name: "qlaunch"; Description: "Create a &Quick Launch icon"; GroupDescription: "Additional icons:"; Components: client; Flags: unchecked
Name: "desktop"; Description: "Create a &Desktop icon"; GroupDescription: "Additional icons:"; Components: client; Flags: unchecked

[Files]
Source: "Release\libyate.dll"; DestDir: "{app}"; Components: engine
Source: "Release\libygtk2.dll"; DestDir: "{app}"; Components: client
Source: "Release\yate-gtk2.exe"; DestDir: "{app}"; Components: client
Source: "Release\yate-service.exe"; DestDir: "{app}"; Components: server
Source: "Release\yate-console.exe"; DestDir: "{app}"; Components: debug

Source: "Release\accfile.yate"; DestDir: "{app}\modules"; Components: client server
Source: "Release\callfork.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\callgen.yate"; DestDir: "{app}\modules"; Components: debug
Source: "Release\cdrbuild.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\cdrfile.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\conference.yate"; DestDir: "{app}\modules"; Components: driver\base
Source: "Release\dsoundchan.yate"; DestDir: "{app}\modules"; Components: client
Source: "Release\extmodule.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\msgsniff.yate"; DestDir: "{app}\modules"; Components: debug
Source: "Release\regexroute.yate"; DestDir: "{app}\modules"; Components: client server debug
Source: "Release\regfile.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\rmanager.yate"; DestDir: "{app}\modules"; Components: server debug
Source: "Release\tonegen.yate"; DestDir: "{app}\modules"; Components: driver\base
Source: "Release\wavefile.yate"; DestDir: "{app}\modules"; Components: driver\base
Source: "Release\register.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\dbpbx.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\yradius.yate"; DestDir: "{app}\modules"; Components: server

Source: "Release\wpchan.yate"; DestDir: "{app}\modules"; Components: driver\wp
Source: "Release\yrtpchan.yate"; DestDir: "{app}\modules"; Components: driver\sip driver\h323
Source: "Release\ysipchan.yate"; DestDir: "{app}\modules"; Components: driver\sip
Source: "Release\h323chan.yate"; DestDir: "{app}\modules"; Components: driver\h323
Source: "Runtimes\ptlib.dll"; DestDir: "{app}"; Components: driver\h323\run
Source: "Runtimes\openh323.dll"; DestDir: "{app}"; Components: driver\h323\run
Source: "Release\iaxchan.yate"; DestDir: "{app}\modules"; Components: driver\iax

Source: "Release\gsmcodec.yate"; DestDir: "{app}\modules"; Components: codecs\gsm
Source: "Release\ilbccodec.yate"; DestDir: "{app}\modules"; Components: codecs\ilbc

Source: "Release\mysqldb.yate"; DestDir: "{app}\modules"; Components: database\my
Source: "Runtimes\libmysql.dll"; DestDir: "{app}"; Components: database\my\run
Source: "Release\pgsqldb.yate"; DestDir: "{app}\modules"; Components: database\pg
Source: "Runtimes\libpq.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\comerr32.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\libeay32.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\ssleay32.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\krb5_32.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\libintl-2.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\libiconv-2.dll"; DestDir: "{app}"; Components: database\pg\run

Source: "..\yate.url"; DestDir: "{app}"
Source: "null_team.ico"; DestDir: "{app}"
Source: "..\conf.d\*.conf.sample"; DestDir: "{app}\conf.d"

Source: "..\modules\skin\default\gtk2client.??"; DestDir: "{app}\modules\skin\default"; Components: client
Source: "..\modules\skin\default\*.png"; DestDir: "{app}\modules\skin\default"; Components: client
Source: "..\modules\skin\tabbed\gtk2client.??"; DestDir: "{app}\modules\skin\tabbed"; Components: client\skin\tabbed
Source: "..\modules\skin\tabbed\*.png"; DestDir: "{app}\modules\skin\tabbed"; Components: client\skin\tabbed
Source: "..\modules\help\*.yhlp"; DestDir: "{app}\modules\help"; Components: client

Source: "..\scripts\*.php"; DestDir: "{app}\scripts"; Components: external\php

Source: "Release\libyate.lib"; DestDir: "{app}\devel"; Components: devel
Source: "..\yate*.h"; DestDir: "{app}\devel"; Components: devel
Source: "yateversn.h"; DestDir: "{app}\devel"; Components: devel
Source: "version.rc"; DestDir: "{app}\devel"; Components: devel

Source: "Runtimes\gtk+-2.6.9-setup.exe"; DestDir: "{app}"; Components: client; Flags: skipifsourcedoesntexist dontcopy nocompression

[Icons]
Name: "{group}\Yate Client"; Filename: "{app}\yate-gtk2.exe"; Parameters: "-n yate-gtk2 -w ""{app}"""; Components: client
Name: "{group}\Yate Console"; Filename: "{app}\yate-console.exe"; Parameters: "-n yate-console -w ""{app}"""; Components: debug
Name: "{group}\Register Service"; Filename: "{app}\yate-service.exe"; Parameters: "--install -w ""{app}"""; Components: server
Name: "{group}\Unregister Service"; Filename: "{app}\yate-service.exe"; Parameters: "--remove"; Components: server
Name: "{group}\Uninstall"; Filename: "{uninstallexe}"
Name: "{group}\Yate Web Site"; Filename: "{app}\yate.url"
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\Yate Client"; Filename: "{app}\yate-gtk2.exe"; Parameters: "-n yate-gtk2 -w ""{app}"""; Components: client; Tasks: qlaunch
Name: "{userdesktop}\Yate Client"; Filename: "{app}\yate-gtk2.exe"; Parameters: "-n yate-gtk2 -w ""{app}"""; Components: client; Tasks: desktop

[Run]
Filename: "{app}\yate-service.exe"; Description: "Register service"; Parameters: "--install -w ""{app}"""; Components: server
Filename: "net.exe"; Description: "Start service"; Components: server; Parameters: "start yate"; Flags: postinstall skipifsilent unchecked
Filename: "{app}\yate-gtk2.exe"; Description: "Launch client"; Components: client; Flags: postinstall nowait skipifsilent unchecked

[UninstallRun]
Filename: "net.exe"; Parameters: "stop yate"; Components: server
Filename: "{app}\yate-service.exe"; Parameters: "--remove"; Components: server

[Code]
var
    GtkRegistry : Boolean;
    GtkLoadable : Boolean;

function GtkTrue() : Integer;
external 'gtk_true@LIBGTK-WIN32-2.0-0.DLL stdcall delayload setuponly';

function InitializeSetup() : Boolean;
begin
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
    if (CurStep = ssInstall) and IsComponentSelected('client') then begin
        if not (GtkRegistry and GtkLoadable) then begin
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

