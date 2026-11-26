# POE 거래소 검색 (C++/Qt)

인게임 아이템 위에서 Ctrl+C 하면 팝업이 떠서 거래소 검색 / 시세 확인을 해주는 Windows 도구.
[phiDelPark/PoeTradeSearch](https://github.com/phiDelPark/PoeTradeSearch) (C#/WPF, 3.16 기준)를 C++17 / Qt 6 로 다시 짠 것이고, 현행 PoE1 + PoE2 거래소 API 기준으로 맞춤.

- 라이선스: **GPL-3.0** (원본이 GPL-3.0 이라 파생 저작물도 GPL-3.0). `LICENSE` 참고.
- 아이콘은 이 프로젝트에서 새로 그림 (`resources/app.svg` → `make_icon` 으로 `app.ico`/`app.png` 생성). 원본 아이콘은 안 씀.
- 원본 대비 바뀐 점 / 뺀 기능 전체 목록: [`docs/FEATURES.md`](docs/FEATURES.md)

## 1차 버전 기능

- PoE1 / PoE2 둘 다. 게임 창 제목(`Path of Exile 2`)으로 자동 구분 (설정에서 고정 가능)
- 한국어 / 영어 클라이언트 둘 다. 한글 아이템은 카카오(`poe.kakaogames.com`), 영문은 글로벌(`www.pathofexile.com`)
- Ctrl+C 클립보드 감시 (PoE 창이 앞에 있을 때만), 또는 `{Run}` 단축키로 Ctrl+Alt+C 주입
- 창은 마우스 커서 근처에 뜨고 바로 시세 조회 시작. 창 바깥을 누르면 닫힘
  - 실수로 닫았으면 트레이 아이콘 클릭 / `{Last}` 단축키로 다시 열기
  - 직전 아이템 이름·추정가를 위에 한 줄로 보여주고, "직전 보기" 로 시세 표까지 바꿔 볼 수 있음
- 옵션 → 거래소 stat 자동 매칭 (로컬 옵션, 다중 줄, 성단 주얼 인챈트, 변형 젬, 룬/훼손 옵션 포함)
  - 게임 문구가 거래소 문구랑 다른 경우(숫자 없는 100% 옵션, "감소" 표기 등)는 APT/EE2 매처 데이터로 보정
- 옵션 값 완화: 최소값을 값의 N%(기본 10%) 낮춰서 채움, 낮을수록 좋은 옵션은 최대값 쪽. 고유는 롤 범위 폭 기준
- 롤 % 표시 (고급 설명 `52(40-59)` 범위 기준)
- 프리셋: 기본(pseudo, 자동 체크) / 정확(T1~T2, 정확값) / 베이스(옵션 끄고 ilvl·영향)
- pseudo 자동 선택, 저항 합산, 우클릭 자동 체크(`Checked.txt`)
- 무기 DPS (Q20 보정), 홈/링크, 퀄리티/레벨/지도 등급, 타락, 영향(PoE1), 결합/역병, 강탈, 결전
- 시세 조회: 검색 → 10개씩 fetch → `≈ 28.5 디바인 (28~29 · 신뢰도 높음 · 매물 161)` 식 추정가
  - 싼 쪽 10개 중앙값 + 20~80% 범위, 덤핑 매물 제외. 환율(카오스↔디바인, PoE2 는 엑잘 기준)은 거래소 일괄 교환 시세로 직접 계산 (30분 캐시, poe.ninja 안 씀)
  - "자세히" 탭 표: 가격 / 환산 / ilvl / 퀄 / DPS / 등록 / 판매자(접속 상태 점), 3일 넘은 매물 흐리게
  - 판매 방식(즉시구매+직접 / 즉시구매만 / ...) 과 계정별 묶기 토글
- 교환 가능한 템(화폐·조각·에센스 등)은 자동으로 일괄 교환 시세 (기본 카오스, PoE2 엑잘)
- "거래소에서 찾기"로 브라우저 열기
- 아이템 아이콘: 화폐류는 바로, 장비/고유는 시세 조회 한 번 하면 배워서 `data/cache/icons` 에 캐시
- 채팅 매크로: F2 `/hideout`, F4 `/exit`(로그아웃 = 캐릭터 선택), `^{Enter}...` 귓말 답장. 키 한 번에 명령 하나
- 거래소 옵션을 못 찾은 줄도 회색으로 표시 (검색엔 안 들어감)
- 거래소 레이트 리밋(`X-Rate-Limit-*` 헤더) 준수 — 원본처럼 연타하다 IP 밴 먹는 일 없게
- 거래소 데이터(stats/items/static/filters/leagues) 자동 다운로드·캐시, 지난 리그면 현재 리그로 자동 교체
- 원본 `Config.txt` / `Checked.txt` 그대로 읽음 (데이터 폴더에 넣으면 됨)

뺀 것: 위키/닌자 링크, 이미지 팝업, 창고 좌표 그리드, Ctrl+휠 창고 탭 이동, POESESSID 특수 창고 시세, 자동 업데이트.
다른 툴(APT, PoE Overlay 등) 비교와 다음 채택 후보: [`docs/REFERENCE.md`](docs/REFERENCE.md)

예전 설정 파일(`Config.txt`)은 처음 실행할 때 자동으로 고쳐짐: `{Wiki}` / poe.ninja 링크 단축키 삭제, `/hideout`·`/exit`·`{Last}` 추가 (키가 겹치면 키 없이 추가 → 설정에서 지정), 뜨자마자 시세 조회 켜짐.

아이콘을 고치려면 `resources/app.svg` 수정 후:

```sh
cmake -S . -B build -DPTS_BUILD_TOOLS=ON && cmake --build build --target make_icon
QT_QPA_PLATFORM=offscreen ./build/make_icon resources/app.svg resources/app.ico resources/app.png
```

## 설치 / 업데이트

[Releases](https://github.com/j0n9m1n1/poe-trade-search/releases) 에서 `PoeTradeSearch-win64.zip` 받아서 아무 폴더에 풀고 `PoeTradeSearch.exe` 실행.

- 프로그램 업데이트는 GitHub Release 기준. 시작 15초 뒤 + 6시간마다 확인
- 설정 > 프로그램 업데이트: **새 버전 알림만**(기본) / 자동으로 받아서 설치 / 확인 안 함
- 설치는 트레이 메뉴 "업데이트 설치". SHA256 검증 후 PowerShell 로 압축 풀어 덮어쓰고 다시 켬. `data\` (설정/캐시) 는 그대로
- 자동 설치는 Release 로 받은 배포 빌드에서만 됨 (직접 빌드한 건 알림만). Program Files 처럼 쓰기 막힌 폴더면 릴리즈 페이지만 열어줌
- 실패하면 기존 버전으로 다시 켜지고 `data\update\update.log` 에 사유 남음

### 릴리즈 만들기 (관리자용)

master 푸시/PR 은 GitHub Actions 가 Windows 빌드 + 테스트만 함 (결과물은 Actions 아티팩트). 릴리즈는 태그 붙일 때만:

```sh
# CMakeLists.txt 의 project(... VERSION x.y.z) 도 같이 올려두면 개발 빌드 버전 표시가 맞음
git tag v0.0.2
git push origin v0.0.2
```

## 빌드 (Windows)

필요: Qt 6.5 이상 (MSVC 2022 64bit 또는 MinGW 64bit), CMake 3.21+, Ninja 권장

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe build\PoeTradeSearch.exe
```

실행하면 `PoeTradeSearch.exe` 옆에 `data\` 폴더가 생기고 거기에 설정/캐시/로그가 들어감 (쓰기 불가 위치면 `%APPDATA%\PoeTradeSearch`).
PoE 를 관리자 권한으로 돌리면 이 프로그램도 관리자로 실행해야 단축키/키 주입이 먹음 (Windows UIPI).

## 개발 / 테스트 (리눅스에서도 코어는 빌드됨)

리눅스에선 플랫폼 계층이 스텁이라 게임 감지/단축키는 안 되고, 파서·쿼리·네트워크·UI 는 그대로 돌아감.

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
src/net       레이트 리밋 큐(TradeClient), 데이터 캐시(DataManager), 엔드포인트
src/platform  Windows: 포그라운드 감지 / SendInput / RegisterHotKey / WS_EX_NOACTIVATE (그 외 OS 는 스텁)
src/ui        팝업, 옵션 행, 설정, 단축키 입력
resources     parser.json (한/영 키워드, 아이템 종류 매핑, pseudo 매핑 등)
tools         pts_cli (디버그용)
```

`resources/parser.json` 을 데이터 폴더에 복사해서 고치면 그게 우선 적용됨. 한국어 클라이언트 문구가 틀려서 안 잡히는 게 있으면 여기만 고치면 됨.

## 외부 데이터

- 거래소 데이터: GGG / 카카오 공식 trade API (`/api/trade(2)/data/*`)
- 옵션 매처: [Awakened PoE Trade](https://github.com/SnosMe/awakened-poe-trade) (PoE1), [Exiled Exchange 2](https://github.com/Kvan7/Exiled-Exchange-2) (PoE2) 의 `stats.ndjson` — 둘 다 **MIT** 라이선스. 저장소에 포함하지 않고 데이터 갱신 때 GitHub 에서 받아 `data/cache/<game>/<lang>/matchers.ndjson` 에 둠. 못 받아도 기본 매칭은 동작함
- 아이콘: `web.poecdn.com`

## 아직 검증 못 한 것

- 실제 Windows 빌드/실행 (이 저장소는 리눅스에서 개발, Win32 코드는 mingw 로 타입 검사까지만 함)
- 한국어 클라이언트 신규 문구 일부(역병에 유린당한, 삿된, 경로석 등급, PoE2 한글 아이템 종류)는 거래소 필터 텍스트 기준 추정값
- PoE2 클립보드 포맷은 `Requires: Level ...` 한 줄 요구사항 기준으로 판별
- `server_redirect` 모드(`/trade/search/{league}?q=`)
- 다른 언어 서버로 검색(한글 아이템 → 글로벌): 거래소 items 목록 순번으로 매칭해서, 한/영 목록 길이가 다른 그룹은 실패함
