# 개발 문서

원본 대비 바뀐 점·뺀 기능: [FEATURES.md](FEATURES.md) / 다른 툴 비교: [REFERENCE.md](REFERENCE.md)

## 빌드 (Windows)

필요: Qt 6.5 이상 (MSVC 2022 64bit 또는 MinGW 64bit), CMake 3.21+, Ninja 권장

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build
C:\Qt\6.8.3\msvc2022_64\bin\windeployqt.exe build\PoeTradeSearch.exe
```

데이터 폴더: `PoeTradeSearch.exe` 옆 `data\` (쓰기 불가 위치면 `%APPDATA%\PoeTradeSearch`, 환경 변수 `PTS_DATA_DIR`로 지정 가능).

## 리눅스에서 개발 / 테스트

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
src/net       레이트 리밋 큐(TradeClient), 데이터 캐시(DataManager), 엔드포인트, 업데이터
src/platform  Windows: 포그라운드 감지 / SendInput / RegisterHotKey / WS_EX_NOACTIVATE (그 외 OS 는 스텁)
src/ui        팝업, 옵션 행, 설정, 단축키 입력
resources     parser.json (한/영 키워드, 아이템 종류 매핑, pseudo 매핑 등), 아이콘
packaging     Inno Setup 설치 스크립트
tools         pts_cli (디버그용), make_icon
```

`resources/parser.json` 을 데이터 폴더에 복사해서 고치면 그게 우선 적용됨. 한국어 클라이언트 문구가 안 잡히는 게 있으면 여기만 고치면 됨.

원본 `Config.txt` / `Checked.txt` 도 그대로 읽음. 예전 설정은 처음 실행할 때 자동으로 고쳐짐 (`{Wiki}`/poe.ninja 단축키 삭제, `/hideout`·`/exit`·`{Last}` 추가).

## 외부 데이터

- 거래소 데이터: GGG / 카카오 공식 trade API (`/api/trade(2)/data/*`), 자동 다운로드·캐시
- 옵션 매처: APT (PoE1) / Exiled Exchange 2 (PoE2) 의 `stats.ndjson` (MIT). 저장소에 안 넣고 데이터 갱신 때 받아서 `data/cache/<game>/<lang>/matchers.ndjson` 에 둠. 못 받아도 기본 매칭은 동작함
- 환율: 거래소 일괄 교환 시세로 직접 계산 (30분 캐시, poe.ninja 안 씀)
- 아이콘: `web.poecdn.com`

## 아이콘 수정

`resources/app.svg` 고친 다음:

```sh
cmake -S . -B build -DPTS_BUILD_TOOLS=ON && cmake --build build --target make_icon
QT_QPA_PLATFORM=offscreen ./build/make_icon resources/app.svg resources/app.ico resources/app.png
```

## 릴리즈

master 푸시/PR 은 GitHub Actions 가 Windows 빌드 + 테스트만 함 (결과물은 Actions 아티팩트). 릴리즈는 `v*` 태그를 붙일 때만 만들어짐:

```sh
# CMakeLists.txt 의 project(... VERSION x.y.z) 도 같이 올리기
git tag v0.0.3
git push origin v0.0.3
```

Release 에 `PoeTradeSearch-Setup.exe`, `PoeTradeSearch-win64.zip` 과 각각의 `.sha256` 이 올라감.

### 앱 업데이트 동작

- GitHub `releases/latest` 확인: 시작 15초 뒤 + 6시간마다
- SHA256 검증 후 앱 종료 → 설치판(설치 폴더에 `unins000.exe`)은 새 Setup.exe 를 `/VERYSILENT` 로 실행, 포터블은 PowerShell 로 zip 덮어쓰기 → 다시 켬
- 자동 설치는 배포 빌드(`PTS_RELEASE_BUILD`)에서만. 직접 빌드한 건 알림만
- 실패하면 기존 버전으로 다시 켜지고 `data\update\update.log` 에 사유 남음

## 아직 검증 못 한 것

- 한국어 클라이언트 신규 문구 일부(역병에 유린당한, 삿된, 경로석 등급, PoE2 한글 아이템 종류)는 거래소 필터 텍스트 기준 추정값
- PoE2 클립보드 포맷은 `Requires: Level ...` 한 줄 요구사항 기준으로 판별
- `server_redirect` 모드(`/trade/search/{league}?q=`)
- 다른 언어 서버로 검색(한글 아이템 → 글로벌): 거래소 items 목록 순번으로 매칭해서, 한/영 목록 길이가 다른 그룹은 실패함
