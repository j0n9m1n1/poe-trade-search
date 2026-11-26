; Poe Trade Search 설치 스크립트 (Inno Setup 6)
;   iscc /DAppVersion=0.0.2 /DNumericVersion=0.0.2 /DSourceDir=<windeployqt 결과 폴더> /O<출력폴더> packaging\installer.iss
; 기본: 사용자별 설치 (관리자 권한 없이 %LOCALAPPDATA%\Programs\PoeTradeSearch). 설치 화면에서 모든 사용자 설치도 고를 수 있음
; 앱 자동 업데이트는 새 PoeTradeSearch-Setup.exe 를 /VERYSILENT 로 실행 -> 끝나면 아래 [Run] 이 앱을 다시 켬

#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0"
#endif
#ifndef SourceDir
  #define SourceDir "..\dist\PoeTradeSearch"
#endif
#define HasKorean FileExists(CompilerPath + "Languages\Korean.isl")

[Setup]
AppId={{8C2F6D1E-5B7A-4E3C-9F21-6A0D3B5C7E94}
AppName=Poe Trade Search
AppVersion={#AppVersion}
AppVerName=Poe Trade Search {#AppVersion}
AppPublisher=poe-trade-search
AppPublisherURL=https://github.com/j0n9m1n1/poe-trade-search
AppSupportURL=https://github.com/j0n9m1n1/poe-trade-search/issues
AppUpdatesURL=https://github.com/j0n9m1n1/poe-trade-search/releases
DefaultDirName={autopf}\PoeTradeSearch
DefaultGroupName=Poe Trade Search
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputBaseFilename=PoeTradeSearch-Setup
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\PoeTradeSearch.exe
UninstallDisplayName=Poe Trade Search
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; 실행 중이면 닫으라고 알려줌 (앱이 같은 이름의 뮤텍스를 만듦)
AppMutex=PoeTradeSearchQt_Running
CloseApplications=yes
RestartApplications=no
VersionInfoVersion={#NumericVersion}
VersionInfoProductName=Poe Trade Search
VersionInfoDescription=Poe Trade Search Setup

[Languages]
#if HasKorean
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
#endif
Name: "english"; MessagesFile: "compiler:Default.isl"

[CustomMessages]
#if HasKorean
korean.StartupTask=Windows 시작할 때 자동 실행
korean.LaunchApp=Poe Trade Search 실행
korean.ExtraGroup=추가 작업:
#endif
english.StartupTask=Start with Windows
english.LaunchApp=Launch Poe Trade Search
english.ExtraGroup=Additional tasks:

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:ExtraGroup}"; Flags: unchecked
Name: "startup"; Description: "{cm:StartupTask}"; GroupDescription: "{cm:ExtraGroup}"; Flags: unchecked

[Files]
; data\ (설정/캐시) 는 실행 중에 생기는 거라 여기 없음 -> 업데이트/삭제해도 남음
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[InstallDelete]
; v0.0.2 까지 쓰던 한글 이름 바로가기 정리 (안 지우면 업데이트 후 바로가기 2개 생김)
Type: files; Name: "{autoprograms}\POE 거래소 검색.lnk"
Type: files; Name: "{autodesktop}\POE 거래소 검색.lnk"
Type: files; Name: "{userstartup}\POE 거래소 검색.lnk"

[Icons]
Name: "{autoprograms}\Poe Trade Search"; Filename: "{app}\PoeTradeSearch.exe"
Name: "{autodesktop}\Poe Trade Search"; Filename: "{app}\PoeTradeSearch.exe"; Tasks: desktopicon
Name: "{userstartup}\Poe Trade Search"; Filename: "{app}\PoeTradeSearch.exe"; Tasks: startup

[Run]
; 일반 설치: 마지막 화면 "실행" 체크박스
Filename: "{app}\PoeTradeSearch.exe"; Description: "{cm:LaunchApp}"; Flags: nowait postinstall skipifsilent
; 자동 업데이트(조용한 설치): 끝나면 바로 다시 켬
Filename: "{app}\PoeTradeSearch.exe"; Parameters: "--updated --wait-shutdown"; Flags: nowait skipifnotsilent
