# 개발 문서

원본 대비 바뀐 점과 뺀 기능은 [FEATURES.md](FEATURES.md), 다른 도구와의 비교는 [REFERENCE.md](REFERENCE.md)를 참고하세요.

## 빌드 (Windows)

필요: Qt 6.5 이상 (MSVC 2022 64bit 또는 MinGW 64bit), CMake 3.21 이상, Ninja 권장

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe build\PoeTradeSearch.exe
```

데이터 폴더는 `PoeTradeSearch.exe` 옆의 `data\`입니다. 쓰기가 불가능한 위치라면 `%APPDATA%\PoeTradeSearch`를 사용하며, 환경 변수 `PTS_DATA_DIR`로 직접 지정할 수도 있습니다.

## 리눅스에서 개발 / 테스트

리눅스에서는 플랫폼 계층이 스텁이라 게임 감지와 단축키는 동작하지 않지만, 파서·쿼리·네트워크·UI는 그대로 동작합니다.

```sh
cmake -S . -B build -G Ninja && cmake --build build

# 거래소 데이터 받기 (data/cache/... 에 저장)
./build/pts_cli --data ./data --update --game poe1 tests/fixtures/poe1_en_rare_ring.txt

# 단위 테스트 (파서 테스트는 실데이터 필요, 없으면 SKIP)
PTS_TEST_DATA=./data ./build/test_core

# 아이템 텍스트 하나 파싱해서 쿼리 보기 / 실제 검색 / 시세
./build/pts_cli --data ./data --league Standard tests/fixtures/poe1_en_rare_sword.txt
./build/pts_cli --data ./data --league Standard --price 20 tests/fixtures/poe1_en_unique.txt

# 화면 없이 팝업 렌더링 확인
QT_QPA_PLATFORM=offscreen PTS_DATA_DIR=./data PTS_DEV_ITEM=tests/fixtures/poe1_ko_rare_ring.txt \
  PTS_DEV_SHOT=/tmp/popup.png ./build/PoeTradeSearch
```

## 구조

```
src/core      파서(ItemParser), 거래소 DB/stat 인덱스(TradeDb), 쿼리(QueryBuilder), 설정, 자동 체크
src/net       레이트 리밋 큐(TradeClient), 데이터 캐시(DataManager), 엔드포인트, 업데이터
src/platform  Windows: 포그라운드 감지 / SendInput / RegisterHotKey / WS_EX_NOACTIVATE (그 외 OS 는 스텁)
src/ui        팝업, 옵션 행, 설정, 단축키 입력
resources     parser.json (한/영 키워드, 아이템 종류 매핑, pseudo 매핑 등), 아이콘
packaging     Inno Setup 설치 스크립트
tools         pts_cli (디버그용), make_icon
```

`resources/parser.json`을 데이터 폴더에 복사해서 수정하면 그 파일이 우선 적용됩니다. 한국어 클라이언트 문구가 인식되지 않는 경우 이 파일만 수정하면 됩니다.

원본의 `Config.txt` / `Checked.txt`도 그대로 읽습니다. 예전 설정은 처음 실행할 때 자동으로 변환됩니다 (`{Wiki}`/poe.ninja 단축키 삭제, `/hideout`·`/exit`·`{Last}` 추가).

## 외부 데이터

- 거래소 데이터: GGG / 카카오 공식 trade API (`/api/trade(2)/data/*`), 자동 다운로드·캐시
- 옵션 매처: APT (PoE1) / Exiled Exchange 2 (PoE2)의 `stats.ndjson` (MIT). 저장소에는 포함하지 않고 데이터를 갱신할 때 받아서 `data/cache/<game>/<lang>/matchers.ndjson`에 저장합니다. 받지 못해도 기본 매칭은 동작합니다.
- 환율: 거래소 일괄 교환 시세로 직접 계산합니다 (30분 캐시, poe.ninja는 사용하지 않습니다).
- 아이콘: `web.poecdn.com`

## 아이콘 수정

`resources/app.svg`를 수정한 다음 아래 명령을 실행합니다.

```sh
cmake -S . -B build -DPTS_BUILD_TOOLS=ON && cmake --build build --target make_icon
QT_QPA_PLATFORM=offscreen ./build/make_icon resources/app.svg resources/app.ico resources/app.png
```

## 릴리즈

master 푸시와 PR에서는 GitHub Actions가 Windows 빌드와 테스트만 수행합니다 (결과물은 Actions 아티팩트). 릴리즈는 `v*` 태그를 붙일 때만 만들어집니다.

```sh
# CMakeLists.txt 의 project(... VERSION x.y.z) 도 같이 올리기
git tag v0.0.26
git push origin v0.0.26
```

Release에는 `PoeTradeSearch-Setup.exe`, `PoeTradeSearch-win64.zip`과 각각의 `.sha256` 파일이 올라갑니다.

### 앱 업데이트 동작

- GitHub `releases/latest`를 시작 15초 뒤와 이후 6시간마다 확인합니다.
- SHA256을 검증한 뒤 앱을 종료합니다. 설치판(설치 폴더에 `unins000.exe`가 있는 경우)은 새 Setup.exe를 `/VERYSILENT`로 실행하고, 포터블은 PowerShell로 zip을 덮어쓴 뒤 다시 실행합니다.
- 자동 설치는 배포 빌드(`PTS_RELEASE_BUILD`)에서만 동작합니다. 직접 빌드한 버전은 알림만 표시합니다.
- 실패하면 기존 버전으로 다시 실행되고 `data\update\update.log`에 사유가 기록됩니다.

## 아직 검증하지 못한 것

- 한국어 클라이언트 신규 문구 일부(역병에 유린당한, 삿된, 경로석 등급, PoE2 한글 아이템 종류)는 거래소 필터 텍스트를 기준으로 한 추정값입니다.
- PoE2 클립보드 형식은 `Requires: Level ...` 한 줄 요구 사항을 기준으로 판별합니다.
- `server_redirect` 모드(`/trade/search/{league}?q=`)
- 다른 언어 서버로 검색(한글 아이템 → 글로벌): 거래소 items 목록 순번으로 매칭하므로, 한/영 목록 길이가 다른 그룹은 실패합니다.

## 에이전트 작업 이력

에이전트가 수정한 항목만 버전별로 기록한다. 다음 작업부터는 변경 내용, 검증 방법, 남은 확인을 여기에 이어서 적는다.

### v0.0.28

- Windows에서 관리자 권한 게임과 일반 권한 PoeTradeSearch의 조합이 감지되면 트레이에 한 번 안내하고, 메뉴에 `관리자 권한으로 다시 실행`을 표시한다. Windows에서 승인하면 기존 데이터 폴더를 넘겨 다시 실행한다. 취소하면 기존 인스턴스를 유지한다. [트레이 동작](../src/app/Application.cpp) · [Windows 권한 확인](../src/platform/win/WinPlatform.cpp)
- 기존 설치본을 관리자 권한으로 다시 실행했을 때 실제 게임 화면 클릭으로 팝업이 닫히는 것을 사용자가 확인했다.
- 팝업 시세 갱신 버튼의 표시 문구를 `새로고침`으로 줄였다.
- 일반 매물 목록의 첫 조회는 선택한 건수만큼 표시한다. 목록 맨 아래에 닿으면 같은 검색 결과의 다음 매물을 10건씩 추가한다. 검색 조건이 바뀌면 이전 결과의 추가 조회를 중단하고, 거래소가 반환한 매물 ID를 모두 소비하면 조회를 끝낸다. [매물 조회](../src/app/Application.cpp) · [거래소 요청](../src/net/TradeClient.cpp)
- 장비 등 제작 대상에 `수정 가능` 필터를 추가했다. 켜면 시세와 거래소 검색 모두에서 미타락·미복제 매물로 좁힌다. [검색 조건](../src/core/SearchDefaults.cpp)
- 군 주얼의 옵션 영역이 내용에 맞춰 펼쳐지고, 소형 패시브 효과의 반복 문구를 줄였다. 화면 높이가 부족하면 옵션 영역을 스크롤할 수 있고 하단 버튼은 화면에 남는다. [팝업 레이아웃](../src/ui/MainPopup.cpp) · [옵션 표시](../src/ui/FilterRow.cpp)
- Windows 빌드와 코어 테스트 41개가 통과했다. 실제 카카오 거래소를 사용하는 테스트 팝업에서 20건에서 30건으로 추가되고, 기존 행과 스크롤 위치가 유지되며, 검색 조건을 바꾸면 이전 목록의 추가 조회가 멈추는 것을 확인했다.
- 한글 활 대형 군 주얼 샘플로 높이 870px에서 모든 옵션 표시, 470px에서 옵션 스크롤과 하단 버튼 표시를 확인했다.

### v0.0.27

- PoE1 젬 시세와 거래소 검색에서 복사한 젬의 부여 여부를 맞춘다. `도륙의 천상의 단도`의 기존 변성 구분자는 정상으로, 최저가 1신성 매물에 내장 보조가 붙은 부여된 젬이 섞인 게 원인이었다. [검색 조건](../src/core/SearchDefaults.cpp) · [회귀 테스트](../tests/test_core.cpp)
- Windows 팝업 바깥 게임 클릭을 버튼을 누른 시점의 위치와 창으로 감지한다. 짧게 누른 클릭을 놓치던 30ms 폴링과 팝업 직후 250ms 차단을 교체했다. Windows 이벤트 등록 실패 시에만 폴링을 사용한다. [팝업 제어](../src/app/Application.cpp) · [Windows 클릭 감지](../src/platform/win/WinPlatform.cpp)

**검증.** Windows 빌드와 코어 테스트 40개가 통과했다. 카카오 Covens에서 제보한 20/20 한 번 타락 젬을 다시 검색한 결과 116건, 최저 500카오스였다. Windows 클릭 이벤트 등록·해제를 확인했고, 테스트 팝업에서 뜨자마자 누른 게임 클릭, 다른 앱 클릭, 모달 보호, 다시 띄운 팝업에 대한 이전 클릭 무시, 설정 끄기를 검증했다.

**남은 확인.** 실제 PoE 클라이언트 위에서 바깥 클릭을 재검증한다.

### v0.0.26

- PoE1 바알 젬은 복사 텍스트 뒤쪽의 바알 스킬 이름으로 검색한다. 일반 타락 젬은 기본 이름을 쓰고, 변성 바알 젬은 거래소 구분자를 유지한다. [파서](../src/core/ItemParser.cpp) · [회귀 테스트](../tests/test_core.cpp)
- 팝업을 열 때 Esc를 바로 등록한다. 트레이에서 다시 열거나 설정·선택 상자를 조작한 뒤에도 Esc를 받을 수 있도록 포커스 처리를 보완했다. 거래소 열기 요청은 대기 상태를 표시하고, 팝업을 닫거나 새 아이템을 검색하면 이전 요청을 취소한다. 바깥 클릭 자동 닫기는 게임 화면을 기준으로 동작한다. [팝업 제어](../src/app/Application.cpp) · [Windows 게임 창 판정](../src/platform/win/WinPlatform.cpp)
- 팝업의 최소·최대 헤더를 입력칸에 맞췄다. 옵션이 없는 젬 등에서는 프리셋과 전체 선택을 숨기고 속성 영역을 줄인다. 화면 높이에 맞춰 필터 목록을 스크롤할 수 있게 하고, 시세 갱신 버튼에 `시세 새로고침`을 표시한다. [팝업 UI](../src/ui/MainPopup.cpp) · [필터 행](../src/ui/FilterRow.cpp)
- Windows CRLF 작업트리에서 테스트 픽스처를 가공할 때 타락 줄과 방어 속성 줄 삭제가 누락되던 문제를 고쳤다. 테스트용 픽스처는 LF로 읽고, CRLF 클립보드 입력도 별도로 파싱해 결과를 비교한다. [코어 테스트](../tests/test_core.cpp)

**검증.** Windows에서 `PTS_TEST_DATA`로 거래소 캐시를 지정해 코어 테스트 39개를 통과했다. `poe1_ko_gem_vaal_haste.txt`로 한국·글로벌 스탠다드 거래소 검색 응답을 확인했다. Qt 팝업 검사로 닫기, 거래소 응답 후 숨김, 요청 중 닫고 다시 열기를 확인했다. 젬에서 옵션이 긴 희귀 아이템으로 바꿔 높이를 470px로 제한한 뒤 열 정렬·스크롤·하단 버튼을 확인했고, 젬으로 돌아오면 속성 영역이 다시 줄어드는 것도 확인했다.

**남은 확인.** 실제 게임 화면 위에서 팝업을 띄운 직후 Esc를 누르는 경우와 멀티 모니터에서 팝업 밖 게임 화면을 클릭하는 경우를 실기로 확인한다.
