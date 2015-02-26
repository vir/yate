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
Name: "server"; Description: "Server files"; Types: full server
Name: "server\pstn"; Description: "Server PSTN support"; Types: full
Name: "server\cluster"; Description: "Server clustering modules"; Types: full
Name: "server\monitor"; Description: "Server monitoring support"; Types: full
Name: "driver"; Description: "Protocol drivers"; Types: full client server
Name: "driver\base"; Description: "Files, tones, mixers"; Types: full client server custom
Name: "driver\openssl"; Description: "SSL/TLS encryption support"; Types: full client server
Name: "driver\openssl\run"; Description: "OpenSSL runtime libraries"; Types: full client server
Name: "driver\sip"; Description: "SIP Protocol driver"; Types: full client server
Name: "driver\h323"; Description: "H.323 Protocol driver"; Types: full client server
Name: "driver\iax"; Description: "IAX Protocol driver"; Types: full client server
Name: "driver\jabber"; Description: "Jabber Protocol driver"; Types: full client server
Name: "driver\jabber\server"; Description: "Jabber server"; Types: full server
Name: "driver\jabber\client"; Description: "Jabber client"; Types: full client
Name: "driver\jabber\jingle"; Description: "Jingle voice protocol"; Types: full client server
;Name: "driver\wp"; Description: "Wanpipe card driver"; Types: full server
Name: "database"; Description: "Database drivers"; Types: full server
Name: "database\my"; Description: "MySQL database driver"; Types: full server
Name: "database\my\run"; Description: "MySQL client libraries"; Types: full server
Name: "database\pg"; Description: "PostgreSQL database driver"; Types: full server
Name: "database\pg\run"; Description: "PostgreSQL client libraries"; Types: full server
Name: "database\sqlite"; Description: "SQLite database driver"; Types: full server
Name: "codecs"; Description: "Audio codecs"; Types: full client server
Name: "codecs\gsm"; Description: "GSM codec"; Types: full client server
Name: "codecs\ilbc"; Description: "iLBC codec"; Types: full client server
Name: "codecs\speex"; Description: "SPEEX codec"; Types: full client server
Name: "codecs\speex\run"; Description: "SPEEX runtime libraries"; Types: full client server
Name: "codecs\isac"; Description: "ISAC codec"; Types: full client server
Name: "compress"; Description: "Data compression support"; Types: full client server
Name: "compress\zlib"; Description: "Zlib compression"; Types: full client server
Name: "external"; Description: "External interfaces"; Types: full server
Name: "external\php"; Description: "PHP5 scripting"; Types: full server
Name: "debug"; Description: "Extra debugging support"; Types: full engine
Name: "devel"; Description: "Module development files"; Types: full engine
Name: "devel\doc"; Description: "Development documentation"; Types: full

[Tasks]
Name: "qlaunch"; Description: "Create a &Quick Launch icon"; GroupDescription: "Additional icons:"; Components: client; Flags: unchecked
Name: "desktop"; Description: "Create a &Desktop icon"; GroupDescription: "Additional icons:"; Components: client; Flags: unchecked

[Files]
Source: "Release\libyate.dll"; DestDir: "{app}"; Flags: replacesameversion; Components: engine
Source: "Release\libyjabber.dll"; DestDir: "{app}"; Flags: replacesameversion; Components: driver\jabber
Source: "Release\libymgcp.dll"; DestDir: "{app}"; Flags: replacesameversion; Components: server\pstn server\cluster
Source: "Release\libysig.dll"; DestDir: "{app}"; Flags: replacesameversion; Components: server\pstn server\cluster
Source: "Release\libyscript.dll"; DestDir: "{app}"; Flags: replacesameversion; Components: client server
Source: "Release\libyqt4.dll"; DestDir: "{app}"; Flags: replacesameversion; Components: client\qt
Source: "Release\yate-qt4.exe"; DestDir: "{app}"; Flags: replacesameversion; Components: client\qt
Source: "Runtimes\qtcore4.dll"; DestDir: "{app}"; Components: client\qt\run
Source: "Runtimes\qtgui4.dll"; DestDir: "{app}"; Components: client\qt\run
Source: "Runtimes\qtxml4.dll"; DestDir: "{app}"; Components: client\qt\run
Source: "Runtimes\imageformats\qgif4.dll"; DestDir: "{app}\imageformats"; Components: client\qt\run; Flags: skipifsourcedoesntexist
Source: "Release\yate-service.exe"; DestDir: "{app}"; Flags: replacesameversion; Components: server
Source: "Release\yate-console.exe"; DestDir: "{app}"; Flags: replacesameversion; Components: debug

Source: "Release\server\accfile.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\server\analog.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server\pstn
Source: "Release\server\ysigchan.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server\pstn
Source: "Release\server\ciscosm.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server\pstn
Source: "Release\server\sigtransport.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server\pstn
Source: "Release\sig\isupmangler.yate"; DestDir: "{app}\modules\sig"; Flags: replacesameversion; Components: server\pstn
Source: "Release\sig\ss7_lnp_ansi.yate"; DestDir: "{app}\modules\sig"; Flags: replacesameversion; Components: server\pstn
Source: "Release\sig\camel_map.yate"; DestDir: "{app}\modules\sig"; Flags: replacesameversion; Components: server\pstn
Source: "Release\analyzer.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: server debug
Source: "Release\server\callcounters.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\callfork.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: server
Source: "Release\callgen.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: debug
Source: "Release\cdrbuild.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: client server
Source: "Release\cdrfile.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: server
Source: "Release\cdrcombine.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: server
Source: "Release\conference.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\base
Source: "Release\client\dsoundchan.yate"; DestDir: "{app}\modules\client"; Flags: replacesameversion; Components: client
Source: "Release\dumbchan.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: client server
Source: "Release\extmodule.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: server
Source: "Release\msgsniff.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: debug
Source: "Release\regexroute.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: client server debug
Source: "Release\enumroute.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: client server debug
Source: "Release\server\regfile.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\rmanager.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: server debug
Source: "Release\tonegen.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\base
Source: "Release\tonedetect.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\base
Source: "Release\wavefile.yate"; DestDir: "{app}\modules"; Components: driver\base
Source: "Release\server\yradius.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\server\register.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\server\dbpbx.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\server\dbwave.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\pbx.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: server
Source: "Release\server\pbxassist.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\server\park.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\server\queues.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\server\queuesnotify.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\server\lateroute.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\server\sipfeatures.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\server\cache.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\server\subscription.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\server\presence.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\server\users.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server
Source: "Release\server\cpuload.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server\monitor
Source: "Release\server\ccongestion.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server\monitor
Source: "Release\server\monitoring.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server\monitor
Source: "Release\server\ysnmpagent.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server\monitor
Source: "Release\sip\sip_cnam_lnp.yate"; DestDir: "{app}\modules\sip"; Flags: replacesameversion; Components: server
Source: "Release\qt4\clientarchive.yate"; DestDir: "{app}\modules\qt4"; Flags: replacesameversion; Components: client\qt
Source: "Release\qt4\customtable.yate"; DestDir: "{app}\modules\qt4"; Flags: replacesameversion; Components: client\qt
Source: "Release\qt4\customtree.yate"; DestDir: "{app}\modules\qt4"; Flags: replacesameversion; Components: client\qt
Source: "Release\qt4\customtext.yate"; DestDir: "{app}\modules\qt4"; Flags: replacesameversion; Components: client\qt
Source: "Release\qt4\widgetlist.yate"; DestDir: "{app}\modules\qt4"; Flags: replacesameversion; Components: client\qt
Source: "Release\javascript.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: client server
Source: "Release\server\eventlogs.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server

Source: "Release\server\heartbeat.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server\cluster
Source: "Release\server\clustering.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server\cluster
Source: "Release\server\mgcpca.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server\pstn server\cluster
Source: "Release\server\mgcpgw.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: server\pstn server\cluster

;Source: "Release\wpchan.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\wp
Source: "Release\yrtpchan.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\sip driver\h323 driver\jabber\jingle
Source: "Release\ysipchan.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\sip
Source: "Release\h323chan.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\h323
Source: "Release\yiaxchan.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\iax
Source: "Release\yjinglechan.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\jabber\jingle
Source: "Release\ystunchan.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\jabber\jingle
Source: "Release\ysockschan.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\jabber\jingle
Source: "Release\filetransfer.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\jabber\jingle
Source: "Release\fileinfo.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\jabber\jingle
Source: "Release\gvoice.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\jabber\jingle
Source: "Release\client\jabberclient.yate"; DestDir: "{app}\modules\client"; Flags: replacesameversion; Components: driver\jabber\client
Source: "Release\jabber\jbfeatures.yate"; DestDir: "{app}\modules\jabber"; Flags: replacesameversion; Components: driver\jabber\server
Source: "Release\jabber\jabberserver.yate"; DestDir: "{app}\modules\jabber"; Flags: replacesameversion; Components: driver\jabber\server
Source: "Release\openssl.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: driver\openssl

Source: "Release\gsmcodec.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: codecs\gsm
Source: "Release\ilbccodec.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: codecs\ilbc
Source: "Release\ilbcwebrtc.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: codecs\ilbc
Source: "Release\speexcodec.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: codecs\speex
Source: "Release\isaccodec.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: codecs\isac

Source: "Release\zlibcompress.yate"; DestDir: "{app}\modules"; Flags: replacesameversion; Components: compress\zlib

Source: "Release\server\mysqldb.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: database\my
Source: "Release\server\pgsqldb.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: database\pg
Source: "Release\server\sqlitedb.yate"; DestDir: "{app}\modules\server"; Flags: replacesameversion; Components: database\sqlite

Source: "Runtimes\libmysql.dll"; DestDir: "{app}"; Components: database\my\run
Source: "Runtimes\libpq.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\comerr32.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\krb5_32.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\libintl-2.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\libiconv-2.dll"; DestDir: "{app}"; Components: database\pg\run
Source: "Runtimes\libeay32.dll"; DestDir: "{app}"; Components: database\pg\run driver\openssl\run
Source: "Runtimes\ssleay32.dll"; DestDir: "{app}"; Components: database\pg\run driver\openssl\run
Source: "Runtimes\msvcr71.dll"; DestDir: "{app}"; Components: database\pg\run driver\openssl\run
Source: "Runtimes\libspeex.dll"; DestDir: "{app}"; Components: codecs\speex\run
Source: "Runtimes\libspeexdsp.dll"; DestDir: "{app}"; Components: codecs\speex\run

Source: "..\COPYING"; DestName: "COPYING.txt"; DestDir: "{app}"
Source: "..\packing\yate.url"; DestDir: "{app}"
Source: "null_team.ico"; DestDir: "{app}"
Source: "..\conf.d\*.conf.sample"; DestDir: "{app}\conf.d"

Source: "..\share\help\*.yhlp"; DestDir: "{app}\share\help"; Components: client
Source: "..\share\sounds\*.wav"; DestDir: "{app}\share\sounds"; Components: client
Source: "..\share\sounds\*.au"; DestDir: "{app}\share\sounds"; Components: client
Source: "..\conf.d\providers.conf.default"; DestName: "providers.conf"; DestDir: "{app}\conf.d"; Components: client
Source: "..\share\skins\default\qt4client.rc"; DestDir: "{app}\share\skins\default"; Components: client\qt
Source: "..\share\skins\default\*.ui"; DestDir: "{app}\share\skins\default"; Components: client\qt
Source: "..\share\skins\default\*.png"; DestDir: "{app}\share\skins\default"; Components: client
Source: "..\share\skins\default\*.gif"; DestDir: "{app}\share\skins\default"; Components: client
Source: "..\share\skins\default\*.css"; DestDir: "{app}\share\skins\default"; Components: client

Source: "..\conf.d\yate-qt4.conf.default"; DestName: "yate-qt4.conf"; DestDir: "{app}\conf.d"; Components: client\qt; Flags: skipifsourcedoesntexist

Source: "..\share\data\*.txt"; DestDir: "{app}\share\data"; Components: server
Source: "..\share\data\*.conf"; DestDir: "{app}\share\data"; Components: server

Source: "..\share\scripts\*.js"; DestDir: "{app}\share\scripts"; Components: external
Source: "..\share\scripts\*.php"; DestDir: "{app}\share\scripts"; Components: external\php

Source: "Release\libyate.lib"; DestDir: "{app}\devel"; Components: devel
Source: "..\yate*.h"; DestDir: "{app}\devel"; Components: devel
Source: "yateversn.h"; DestDir: "{app}\devel"; Components: devel
Source: "version.rc"; DestDir: "{app}\devel"; Components: devel
Source: "..\clients\qt4\qt4client.h"; DestDir: "{app}\devel"; Components: devel
Source: "Release\libyqt4.lib"; DestDir: "{app}\devel"; Components: devel
Source: "..\README"; DestName: "README.txt"; DestDir: "{app}\devel"; Components: devel
Source: "..\ChangeLog"; DestName: "ChangeLog.txt"; DestDir: "{app}\devel"; Components: devel
Source: "..\docs\*.html"; DestDir: "{app}\devel\docs"; Components: devel\doc
Source: "..\docs\api\*.html"; DestDir: "{app}\devel\docs\api"; Components: devel\doc; Flags: skipifsourcedoesntexist
Source: "..\docs\api\*.png"; DestDir: "{app}\devel\docs\api"; Components: devel\doc; Flags: skipifsourcedoesntexist
Source: "..\docs\api\*.css"; DestDir: "{app}\devel\docs\api"; Components: devel\doc; Flags: skipifsourcedoesntexist

Source: "Release\msvcrtest.dll"; Flags: dontcopy
; Global CRT DLLs installer, should not be used together with local installs
Source: "Runtimes\vcredist_x86.exe"; DestDir: "{app}"; Flags: skipifsourcedoesntexist dontcopy nocompression
; Local CRT DLLs install, either all or none of the files must be present
Source: "Runtimes\Microsoft.VC80.CRT.manifest"; DestDir: "{app}"; Flags: skipifsourcedoesntexist; Check: CrtLocalInstall
Source: "Runtimes\msvcr80.dll"; DestDir: "{app}"; Flags: skipifsourcedoesntexist; Check: CrtLocalInstall
Source: "Runtimes\msvcp80.dll"; DestDir: "{app}"; Flags: skipifsourcedoesntexist; Check: CrtLocalInstall

[Icons]
Name: "{group}\Yate Client (Qt)"; Filename: "{app}\yate-qt4.exe"; Parameters: "-n yate-qt4 -w ""{app}"""; Components: client\qt
Name: "{group}\Yate Console"; Filename: "{app}\yate-console.exe"; Parameters: "-n yate-console -w ""{app}"""; Components: debug
Name: "{group}\Register Service"; Filename: "{app}\yate-service.exe"; Parameters: "--install -w ""{app}"""; Components: server
Name: "{group}\Unregister Service"; Filename: "{app}\yate-service.exe"; Parameters: "--remove"; Components: server
Name: "{group}\Uninstall"; Filename: "{uninstallexe}"
Name: "{group}\Yate Web Site"; Filename: "{app}\yate.url"
Name: "{group}\Developer docs"; Filename: "{app}\devel\docs\index.html"; Components: devel\doc
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\Yate Client (Qt)"; Filename: "{app}\yate-qt4.exe"; Parameters: "-n yate-qt4 -w ""{app}"""; Components: client\qt; Tasks: qlaunch
Name: "{userdesktop}\Yate Client (Qt)"; Filename: "{app}\yate-qt4.exe"; Parameters: "-n yate-qt4 -w ""{app}"""; Components: client\qt; Tasks: desktop

[Run]
Filename: "{app}\yate-service.exe"; Description: "Register service"; Parameters: "--install -w ""{app}"""; Components: server
Filename: "net.exe"; Description: "Start service"; Components: server; Parameters: "start yate"; Flags: postinstall skipifsilent unchecked
Filename: "{app}\yate-qt4.exe"; Parameters: "-n yate-qt4 -w ""{app}"""; Description: "Launch Qt client"; Components: client\qt; Flags: postinstall nowait skipifsilent unchecked

[UninstallRun]
Filename: "net.exe"; Parameters: "stop yate"; Components: server
Filename: "{app}\yate-service.exe"; Parameters: "--remove"; Components: server

[Code]
var
    CrtLoadable : Boolean;
    CrtLocal    : Boolean;

function CrtTrue() : Integer;
external 'crt_true@files:msvcrtest.dll cdecl delayload setuponly';

function InitializeSetup() : Boolean;
begin
    try
        CrtLoadable := CrtTrue() <> 0;
        UnloadDLL('MSVCRTEST.DLL');
    except
        CrtLoadable := False;
    end;
    CrtLocal := False;
    Result := True;
end;

function CrtLocalInstall() : Boolean;
begin
    if not CrtLoadable then CrtLocal := True;
    Result := CrtLocal;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
    msg : String;
    url : String;
    err : Integer;
begin
    if (CurStep = ssInstall) then begin
        if not (CrtLoadable or CrtLocal) then begin
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
    end;
end;

procedure CurPageChanged(CurPageID: Integer);
var
    crt : String;
begin
    if (CurPageID = wpReady) then begin
        if CrtLocal then crt := 'Install locally'
        else if CrtLoadable then crt := 'Already loadable (will not install)'
        else crt := 'Install globally';
        if crt <> '' then begin
            Wizardform.ReadyMemo.Lines.Add('');
            Wizardform.ReadyMemo.Lines.Add('C Runtimes:');
            Wizardform.ReadyMemo.Lines.Add('      ' + crt);
        end;
    end;
end;

