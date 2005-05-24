; -- yate.iss --
; Yate script for Inno Setup Compiler.
; SEE THE DOCUMENTATION FOR DETAILS ON CREATING .ISS SCRIPT FILES!

[Setup]
AppName=Yet Another Telephony Engine
AppVerName=Yate version 0.9.0
DefaultDirName={pf}\Yate
DefaultGroupName=Yate
UninstallDisplayIcon={app}\yate-client.exe
Compression=lzma
SolidCompression=yes

[Types]
Name: "full"; Description: "Full installation"
Name: "client"; Description: "VoIP client installation"
Name: "server"; Description: "Server installation"
Name: "custom"; Description: "Custom installation"; Flags: iscustom
Name: "engine"; Description: "Engine only (unlikely)"

[Components]
Name: "engine"; Description: "Engine library"; Types: full client server engine custom; Flags: fixed
Name: "client"; Description: "Client files"; Types: full client
Name: "server"; Description: "Server files"; Types: full server
Name: "driver"; Description: "Protocol drivers"; Types: full client server
Name: "driver\base"; Description: "Files, tones, mixers"; Types: full client server custom
Name: "driver\iax"; Description: "IAX Protocol driver"; Types: full client server
Name: "driver\sip"; Description: "SIP Protocol driver"; Types: full client server
Name: "driver\wp"; Description: "Wanpipe card driver"; Types: full server
Name: "debug"; Description: "Extra debugging support"; Types: full engine

[Tasks]
Name: "qlaunch"; Description: "Create a &Quick Launch icon"; GroupDescription: "Additional icons:"; Components: client; Flags: unchecked
Name: "desktop"; Description: "Create a &Desktop icon"; GroupDescription: "Additional icons:"; Components: client; Flags: unchecked

[Files]
Source: "Release\libyate.dll"; DestDir: "{app}"; Components: engine
Source: "Release\yate-client.exe"; DestDir: "{app}"; Components: client
Source: "Release\yate-service.exe"; DestDir: "{app}"; Components: server
Source: "Release\yate-console.exe"; DestDir: "{app}"; Components: debug
Source: "Release\callgen.yate"; DestDir: "{app}\modules"; Components: debug
Source: "Release\cdrbuild.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\cdrfile.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\conference.yate"; DestDir: "{app}\modules"; Components: driver\base
Source: "Release\dsoundchan.yate"; DestDir: "{app}\modules"; Components: client
Source: "Release\iaxchan.yate"; DestDir: "{app}\modules"; Components: driver\iax
Source: "Release\msgsniff.yate"; DestDir: "{app}\modules"; Components: debug
Source: "Release\regexroute.yate"; DestDir: "{app}\modules"; Components: client server debug
Source: "Release\regfile.yate"; DestDir: "{app}\modules"; Components: server
Source: "Release\rmanager.yate"; DestDir: "{app}\modules"; Components: debug
Source: "Release\tonegen.yate"; DestDir: "{app}\modules"; Components: driver\base
Source: "Release\wavefile.yate"; DestDir: "{app}\modules"; Components: driver\base
Source: "Release\wpchan.yate"; DestDir: "{app}\modules"; Components: driver\wp
Source: "Release\yrtpchan.yate"; DestDir: "{app}\modules"; Components: driver\sip
Source: "Release\ysipchan.yate"; DestDir: "{app}\modules"; Components: driver\sip
Source: "..\conf.d\*.conf.sample"; DestDir: "{app}\conf.d"

[Icons]
Name: "{group}\Yate Client"; Filename: "{app}\yate-client.exe"; Components: client
Name: "{group}\Yate Console"; Filename: "{app}\yate-console.exe"; Components: debug
Name: "{group}\Register Service"; Filename: "{app}\yate-service.exe"; Parameters: "--install"; Components: server
Name: "{group}\Unregister Service"; Filename: "{app}\yate-service.exe"; Parameters: "--remove"; Components: server
Name: "{group}\Uninstall"; Filename: "{uninstallexe}"
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\Yate Client"; Filename: "{app}\yate-client.exe"; Components: client; Tasks: qlaunch
Name: "{userdesktop}\Yate Client"; Filename: "{app}\yate-client.exe"; Components: client; Tasks: desktop

[Run]
Filename: "{app}\yate-client.exe"; Description: "Launch client"; Components: client; Flags: postinstall nowait skipifsilent unchecked

[UninstallRun]
Filename: "{app}\yate-service.exe"; Parameters: "--remove"; Components: server

