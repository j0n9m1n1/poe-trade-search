# PoeTradeSearch 기능 인벤토리 (C# 원본 → C++/Qt 포팅용)

- 원본: https://github.com/phiDelPark/PoeTradeSearch (마지막 커밋 `40f3ad6`, v3.16.0.5, 2021-11-03, WPF/.NET 4.8)
- 원본 라이선스: **GPL-3.0** → 포팅본도 GPL-3.0 유지해야 함 (파생 저작물)
- 원본 규모: C# 약 4,700줄 + XAML 약 500줄
- 현행 기준 확인일: 2026-11-26 (현재 리그 `Covens`), 공식 trade API를 직접 호출해서 확인한 결과임

## 1차 구현 현황 (2026-11-26)

결정 사항: **Windows 전용**, **PoE1 + PoE2 둘 다**, '보류' 항목은 **전부 1차 제외**.

| 영역 | 상태 |
|---|---|
| A 앱 기반 | A1 A2 A3 A4 A6 A7 A8 구현. A5(원작자 자동 업데이트)는 이 저장소 GitHub Release 기반 업데이터로 대체 (알림/자동 설치 선택) |
| B 아이템 감지 | B1 B2 B3 구현 (`{Run}` 은 Ctrl+Alt+C 로 복사해서 등급까지 읽음) |
| C 파서 | 삭제 항목(C12 C18 C19 C20) 빼고 전부. PoE2 룬/훼손/룬 홈, 변형 젬, 삿된(Foulborn), 역병에 유린당한 지도 추가 |
| D 쿼리 | 전부. 영향은 pseudo stat, 지도 영향 삭제, 변형 젬 discriminator, PoE2 필터 그룹(type_filters/equipment_filters) 대응 |
| E 시세 | 전부. 교환 응답 신포맷, 레이트 리밋 큐, 진행 중 조회 취소 |
| F 거래소 열기 | 전부 |
| G 팝업 UI | G1~G8, G11. G9(위키) 사용자 결정으로 삭제, G10(창고 버튼) 제외. 아이콘 표시 추가 |
| H 설정 | 전부. 저장 즉시 적용(재시작 없음), 단축키 개수 제한 없음 |
| I 단축키 | I1, I2, I4, I6, I5(채팅 매크로: 키 한 번에 명령 하나만). I3 의 `{Wiki}` 삭제. I7 I8 I9 I10 제외 |
| J 특수 창고 | 제외 |

테스트: `tests/test_core.cpp` 17개 (실데이터 파서 테스트 포함) 통과. 실제 거래소 API 에 PoE1(글로벌/카카오)·PoE2 쿼리 보내서 400 없이 결과 받는 것까지 확인.

판정 기호
- **유지**: 그대로 포팅
- **수정**: 기능은 살리되 현행 게임/API에 맞게 고침
- **삭제**: 게임에서 사라졌거나 의미 없음
- **보류**: 포팅할지 결정 필요

---

## 0. 현행 API 실측 결과 (원본 대비 바뀐 점)

| 항목 | 원본(3.16) | 현재 (실측) | 영향 |
|---|---|---|---|
| 한국 서버 도메인 | `poe.game.daum.net` | **DNS 자체가 없음**. `poe.kakaogames.com`로 이전됨 (`/api/trade/data/*` 200, 검색 POST 동작, `/trade` 웹페이지는 카카오 로그인 필요) | URL 전부 교체 |
| 글로벌 서버 | `www.pathofexile.com` | 동일 | - |
| stat 그룹 | pseudo/explicit/implicit/fractured/enchant/crafted/veiled/monster/delve/ultimatum/scourge | pseudo/explicit/implicit/**imbued**/fractured/enchant/scourge/crafted/**mercenary**/veiled/delve/ultimatum/**sanctum**/**crucible** (monster 없어짐) | 하드코딩된 `lFilterType` 대신 API의 `id`/`label` 그대로 사용 |
| 영향(쉐이퍼/엘더/정복자) | `misc_filters.shaper_item` 등 | misc_filters에서 **사라짐**. `pseudo.pseudo_has_shaper_influence` 등 pseudo stat으로 검색 | 쿼리 빌더 수정 |
| 젬 대체 퀄리티 | `gem_alternate_quality` (기묘한/상이한/몽환적인) | **삭제됨**(3.23). 변형 젬은 items 데이터에 `disc: "alt_x/y/z"`로 들어있고 `gem_transfigured` 필터 있음 | 변형 젬 지원으로 교체 |
| 지도 영향 | `map_shaped`/`map_elder`, `stat_1792283443` (Map is occupied by…) | 필터도 stat도 **없음** | 삭제 |
| 주얼 반경 | `stat_3642528642` | **없음** | 삭제 |
| 지도 필터 | tier/shaped/elder/blighted | tier/packsize/iiq/iir/area_level/**blighted**/**uberblighted**/completion_reward/series | blighted만 유지 + uberblighted 추가 |
| 아이템 카테고리 | prophecy, watchstone, monster.sample 등 | prophecy/watchstone/monster.sample **없음**. tincture/corpse/idol/graft/wombgift/enshrouded/chart/viscera/sanctum/currency.omen/currency.tattoo 등 **추가** | 카테고리 매핑표 재작성 |
| 일괄 교환 응답 | 검색 후 `fetch`로 다시 조회 | `result`가 **dict**, 각 listing에 `offers[{exchange, item{amount,stock}}]` 인라인으로 옴 (fetch 불필요) | 교환 시세 로직 재작성 |
| 등록 시간 포맷 | `yyyy-MM-ddTHH:mm:ssZ` | `2026-11-25T21:49:02+00:00` | 원본 `ParseExact` 그대로면 터짐 → ISO 파서 사용 |
| 계정명 | `Name` | `Name#1234` (디스크리미네이터) | 표시만 처리 |
| 레이트 리밋 | 원본은 무시 | `X-Rate-Limit-Ip: 5:10:60,15:60:300,...` 헤더 옴 | 헤더 파싱해서 스로틀링 **필수** (안 지키면 IP 밴) |
| 화폐 기준 | Exalted | Divine 중심 | 시세 요약/창고 시세 표기 변경 |
| Parser.txt 화폐 목록 | 60/52개 | 화폐 13개, 교환 8개 id가 static에 **없음** (육분의, 하빈저 조각, 재분류 렌즈, 정예 화석 일부, 시간의 파편 등) | 하드코딩 말고 static API로 KO/EN 자동 매칭 |
| 위키 | `pathofexile.gamepedia.com` | 커뮤니티 위키 `poewiki.net`로 이전 | URL 교체 |

---

## A. 앱 기반 / 시스템

| # | 기능 | 원본 위치 | 판정 | 비고 / Qt 대응 |
|---|---|---|---|---|
| A1 | 중복 실행 방지 (Mutex), `/wait_shutdown` 으로 재시작 시 이전 프로세스 종료 대기 | App.xaml.cs | 유지 | `QLockFile` 또는 `QLocalServer` |
| A2 | 트레이 아이콘 + 메뉴(설정/재시작/업데이트/종료), 시작 풍선 알림 | App.xaml.cs, WinMain.xaml.cs | 유지 | `QSystemTrayIcon` |
| A3 | 관리자 권한 확인 (단축키/마우스 훅은 관리자일 때만) | App.xaml.cs | 유지(Win) | PoE가 관리자로 돌면 일반 권한 앱은 키 주입이 막힘(UIPI) |
| A4 | 처리 안 된 예외를 `.log`로 기록 후 종료 | App.xaml.cs | 유지 | `qInstallMessageHandler` + 파일 로그 |
| A5 | 자동 업데이트: 원작자 repo `VERSIONS` 확인 → `_POE_EXE.zip` 받아서 `update.cmd` 실행 | App.xaml.cs, Updates.cs | **삭제** | 원작자 바이너리 받아서 덮어쓰는 구조라 의미 없음. 나중에 자체 릴리즈 체크로 대체 가능 |
| A6 | 기본 데이터(Config/Parser/Checked)가 없으면 원작자 GitHub raw에서 받아옴 | Updates.cs `BasicDataUpdate` | 수정 | 기본값은 `qrc` 리소스에 내장 |
| A7 | 거래소 DB 캐시: stats/items/static을 KO/EN 둘 다 받아 로컬 저장 (`FiltersKO/EN`, `ItemsKO/EN`, `StaticKO/EN`) | Updates.cs | 유지 | 로컬 옵션(Local) 표시·`part` 태깅, monster 텍스트 `(×#)` 제거 같은 전처리 포함 |
| A8 | DB 오래되면 알림, 트레이/설정에서 수동 갱신 | WinMain.xaml.cs, WinSetting | 수정 | 원작자 VERSIONS 대신 리그 목록 바뀌었거나 N일 지나면 갱신 |

## B. 아이템 감지 (트리거)

| # | 기능 | 원본 위치 | 판정 | 비고 |
|---|---|---|---|---|
| B1 | 클립보드 감시 (`SetClipboardViewer` 체인), PoE 창이 포그라운드일 때만 파싱 | WinMain.xaml.cs `WndProc` | 유지 | `QClipboard::dataChanged` + 포그라운드 창 확인(Win32 `GetForegroundWindow`, 클래스 `POEWindowClass`) |
| B2 | `{Run}` 커스텀 작동키: 클립보드 비우고 Ctrl+C 주입 → 텍스트 들어올 때까지 대기 → 파싱 | WinMain.xaml.cs | 유지 | 기본은 그냥 Ctrl+C 감시, `{Run}` 지정 시에는 클립보드 감시 끔 |
| B3 | Ctrl+Alt+C(고급 설명) 텍스트 지원: `{ 접두 속성 "이름" (등급: 1) }` 줄에서 tier 추출 | Methods.cs `ItemOptionParser` | 유지 | 현행 포맷 샘플로 재검증 필요 |

## C. 아이템 텍스트 파서 (핵심, `ItemTextParser` 약 750줄)

| # | 기능 | 판정 | 비고 |
|---|---|---|---|
| C1 | 언어 판별: 첫 줄 `아이템 종류:` / `Item Class:` 로 KO/EN 판단 | 유지 | |
| C2 | 헤더 파싱: 아이템 종류 → 거래소 카테고리 id, 희귀도, 이름, 베이스. `<<set:MS>>` 태그 제거 | 수정 | 카테고리 매핑표(Parser.txt `category`)를 현행 아이템 종류로 재작성해야 함 |
| C3 | 속성 키 추출: 퀄리티, 레벨, 아이템 레벨, 부적 등급, 지도 등급, 홈, 강탈 대상, 결전 도전/보상, 물리/원소/카오스 피해, 초당 공격, 영향 6종, 결합, 타락, 미확인, 바알 젬 | 수정 | 예언·환영 보상·주얼 반경·장기 관련 키 제거 / 현행 키 검증 |
| C4 | 옵션 줄 → stat id 매칭: 숫자를 `#`로 바꾼 정규식으로 DB 검색, 숫자 리터럴까지 일치하는 항목 선택 | 유지 | 성능 위해 정규식 미리 컴파일/인덱싱 (원본은 매 줄마다 수천 개 정규식 생성) |
| C5 | 동일 문구 여러 개면 장비 로컬 옵션 우선 (화살통 제외), 아니면 글로벌 | 유지 | Parser.txt `local` 24개 id 전부 현행 API에 존재 확인함 |
| C6 | 옵션 타입 판별(`(implicit)` `(crafted)` 등 접미) → 타입 콤보 후보 구성, pseudo 가능하면 pseudo 자동 선택 | 수정 | 타입 목록은 API 그룹에서 동적으로 |
| C7 | 범위 옵션(`# to #`/`#~#`)은 평균값을 min으로 | 유지 | |
| C8 | 역방향 옵션(감소형) min/max 뒤집기 (`position` 27개) | 유지 | 전부 현행 API에 존재 확인함 |
| C9 | 여러 줄 옵션 처리 (한 stat이 2줄) | 유지 | |
| C10 | 특수 옵션: 성단 주얼 인챈트(패시브 종류 → option 값) | 유지 | `cluster` 표 현행 검증 필요 |
| C11 | 특수 옵션: 탐험 일지 보스(`logbook`) | 유지 | |
| C12 | 특수 옵션: 주얼 반경 / 지도 점령 영향 | **삭제** | 해당 stat 없어짐 |
| C13 | 검색 불가 옵션 체크 금지 (`disable`) | 유지 | |
| C14 | 저항 옵션 합산 검색(`pseudo_total_resistance`) 체크박스 | 유지 | |
| C15 | 이름 정규화: 상급 접두 제거, 매직 아이템 접두/접미 떼고 베이스 찾기, 역병/형성 지도 접두 제거, 결합 접두 제거 | 수정 | 형성(Shaped) 지도 없어짐. 역병 걸린/역병 침식(uberblight) 처리 |
| C16 | 젬: 대체 퀄리티 접두 제거 + alt_quality 선택 | **수정** | 변형 젬(`disc`)으로 교체. 바알 젬은 `gem_vaal` |
| C17 | 야수: 영어 이름으로만 검색 | 유지 | items의 monster 카테고리 기준 재검증 |
| C18 | 예언 처리 | **삭제** | 3.17에서 삭제 |
| C19 | 장기(metamorph sample) 중복 옵션 제거 | **삭제** | `monster.sample` 카테고리 없음 |
| C20 | 환영 지도(Delirium Reward Type) 첫 옵션 자동 체크 | **삭제** | 해당 줄 현행 존재 여부 불확실, 일단 제외 |
| C21 | 상세 모드: 화폐/카드/젬/조각은 옵션 대신 설명 텍스트 표시 | 유지 | |
| C22 | 무기 DPS 계산 (pDPS/eDPS/합계, Q20 보정, 공속 0.05 반올림) | 유지 | |
| C23 | 홈/연결 파싱 (`R-G-B B` → 홈 수, 최대 링크) | 유지 | |
| C24 | KO/EN 이름 쌍 생성 → 이름 콤보로 검색 서버 선택 (한글이면 카카오, 영문이면 글로벌) | 유지 | |
| C25 | 베이스 자동 체크(`Checked.txt bases`): 지정 베이스·영향·ilvl 이상이면 ilvl 자동 체크 + 강조 | 유지 | |
| C26 | 퀄리티 초과 강조 (>20, 장신구 >4), 타락 강조, 영향 강조 | 유지 | |

## D. 검색 쿼리 빌더 (`CreateJson`)

| # | 기능 | 판정 | 비고 |
|---|---|---|---|
| D1 | `status: online`, 이름/베이스, 카테고리, 희귀도 | 수정 | status 옵션 설정화 (online / any 등) |
| D2 | 홈/링크, 퀄리티, 아이템 레벨, 젬 레벨 | 유지 | |
| D3 | 영향 6종, 결합 | **수정** | 영향은 `pseudo_has_*_influence` stat으로 |
| D4 | 타락 무관/예/아니오/한 번만/두 번 (`corrupted` + `twice_corrupted`), 삿된(`mutated`, PoE2 는 함양된 바알 고유). 기본값은 아이템 상태 | 확장 | 원본은 타락 예/아니오만, 기본 무관 |
| D5 | 강탈 대상 가치(`heist_objective_value`) | 유지 | |
| D6 | 지도 등급, 역병 | 수정 | uberblighted 추가, shaped/elder 삭제 |
| D7 | 결전 보상/결과물(`ultimatum_reward`/`ultimatum_output`) | 유지 | 필드 현행 검증 |
| D8 | 젬 대체 퀄리티 | **삭제→수정** | 변형 젬 `type: {option, discriminator}` |
| D9 | stats `and` 그룹 (min/max/option/disabled) | 유지 | |
| D10 | 등록 기간(`indexed` 1시간~2달, 팝업 콤보), 가격 화폐(`price.option`), 판매 방식(기본 즉시 구입만), 최소 가격 | 확장 | 원본은 등록일 설정값만 |
| D11 | "아이템 유형으로 검색" (이름·베이스 빼고 카테고리로만), `auto_select_bytype` | 유지 | |
| D12 | 유니크 아니면 name 제거 등 조건부 필드 | 유지 | 원본은 99999 센티넬 넣고 문자열 regex로 지우는 야매 → QJsonObject로 정상 빌드 |
| D13 | 매칭 실패 stat은 빨간 줄 + error 표시 | 유지 | |

## E. 시세 조회 (`UpdatePrice`)

| # | 기능 | 판정 | 비고 |
|---|---|---|---|
| E1 | 검색 → 결과 id 10개씩 fetch → 가격/계정/경과 시간 목록 | 유지 | 레이트 리밋 준수 추가 |
| E2 | 가격 요약: 최저~최고, 최빈 가격 2개 | 수정 | 카오스/디바인 환산 표기 |
| E3 | 목록 수 20/40/60/80, 최소 가격 필터 | 유지 | |
| E4 | 창 뜰 때 자동 검색 + 재검색 딜레이(30/60초 카운트다운) | 유지 | 레이트 리밋 때문에 기본값은 수동 권장 |
| E5 | 일괄 교환(오브/조각 선택 → exchange API, 비율 표시) | **수정** | 응답 포맷 바뀜 (offers 인라인, stock 포함). 교환 목록은 static API에서 자동 생성 |
| E6 | 창 닫거나 새 아이템 오면 진행 중 조회 중단 | 유지 | `QNetworkReply::abort` |

## F. 거래소 열기

| # | 기능 | 판정 | 비고 |
|---|---|---|---|
| F1 | "거래소에서 찾기": 검색 POST → 브라우저로 `trade/search/{league}/{id}` 열기 | 유지 | `QDesktopServices::openUrl` |
| F2 | `server_redirect`: `?redirect&source=` 로 브라우저가 직접 검색 | 유지 | |
| F3 | 교환은 `trade/exchange/{league}?q=` 로 열기 | 유지 | |

## G. 메인 팝업 UI (`WinMain`)

| # | 기능 | 판정 | 비고 |
|---|---|---|---|
| G1 | 항상 위, **포커스 안 뺏고** 뜸(`WS_EX_NOACTIVATE`), 입력칸 클릭 시에만 활성화, ESC로 닫기, 닫기 = 숨김 | 유지 | `Qt::WindowDoesNotAcceptFocus` + Win32 스타일 토글 |
| G2 | 탭: 간략히(옵션 필터) / 자세히(시세 목록), 시세 문구 우클릭으로 탭 전환 | 유지 | |
| G3 | 옵션 10줄: 타입 콤보, 옵션 텍스트, 최소, 최대, 사용 체크, 저항합산 체크 | 수정 | 고정 10줄 대신 동적 행 (원본은 10줄 넘으면 버림) |
| G4 | 색상: 고정옵 진빨강, 제작/인챈트 파랑, 스컬지 주황, 타입 애매하면 핑크, 오류 빨강 | 유지 | |
| G5 | 전체 체크 | 유지 | |
| G6 | 체크박스 우클릭 → 자동 체크 목록에 등록/해제 (카테고리별, `Checked.txt` 즉시 저장) | 유지 | |
| G7 | 레벨/퀄리티/홈·링크/결합(역병)/타락/영향1·2/대체 콤보 (젬·주얼·강탈·지도일 때 용도 바뀜) | 수정 | 콤보 용도 재정의 (변형 젬, 강탈 가치, 결전 보상, 역병 종류) |
| G8 | DPS 표시 라벨 | 유지 | |
| G9 | 위키 버튼 | 수정 | `poewiki.net` |
| G10 | 창고 버튼 → 특수 창고 창 | 보류 | J 참조 |
| G11 | 타이틀바 `?` 버튼 → 설정 | 수정 | Qt에선 그냥 설정 버튼/트레이 메뉴 |

## H. 설정 창 (`WinSetting`)

| # | 기능 | 판정 | 비고 |
|---|---|---|---|
| H1 | 리그 선택 (leagues API), 검색 서버(자동/한국/영국) | 유지 | |
| H2 | 자동 검색, 등록일 제한, 목록 수 | 유지 | |
| H3 | 유니크 전체 체크 / pseudo 우선 / 저항 합산 / 업데이트 체크 | 유지 | |
| H4 | DB 업데이트 버튼, 버전 표시 | 유지 | |
| H5 | 단축키 12개 (키 캡처 박스 + 작업값) | 수정 | 개수 제한 없앰 |
| H6 | 자동 체크 목록 보기 | 유지 | 삭제 버튼 추가 |
| H7 | 저장하면 앱 재시작 | 수정 | 재시작 없이 바로 적용 |
| H8 | `Config.txt` JSON 직접 수정도 지원 | 유지 | 기존 Config.txt 포맷 호환 (이관 쉽게) |

## I. 전역 단축키 / 매크로 (원본: Windows, 관리자 권한 필요)

| # | 기능 | 판정 | 비고 |
|---|---|---|---|
| I1 | PoE 창 활성일 때만 단축키 등록, 비활성 시 해제 (1초 폴링) | 유지 | Win32 `RegisterHotKey` |
| I2 | ESC: 팝업 열려 있으면 닫기, 아니면 PoE로 ESC 전달 | 유지 | |
| I3 | `{Run}` / `{Wiki}` (아이템 위키 바로 열기) | 유지 | |
| I4 | `{Pause}` 일시정지 / `{Restart}` 재시작 | 유지 | |
| I5 | 채팅 매크로 `{Enter}/hideout{Enter}`, `^{Enter}` 귓말 답장, UTF-16 SendInput | 보류 | 1키 1명령은 GGG 정책상 허용 범위. 원본처럼 **한 키에 여러 명령 연속 전송은 막는 게 안전** |
| I6 | `{Link}URL{Link}` 링크 열기 | 유지 | |
| I7 | `*.jpg` 이미지 팝업 (신디케이트/사원 치트시트) | 보류 | 기능은 단순. 동봉 이미지는 3.16 기준이라 낡음 → 이미지 미동봉, 사용자 파일만 |
| I8 | `{Grid:Stash}` 창고 칸 좌표 오버레이 (1080p 비율 하드코딩) | 보류 | 해상도/UI 스케일 바뀌면 어긋남 |
| I9 | `{Find:Stash}` 특수 창고 검색 창 | 보류 | J 참조 |
| I10 | Ctrl+휠로 창고 탭 이동 (저수준 마우스 훅 → 좌/우 키 주입) | 보류 | 전역 마우스 훅 + 키 주입이라 게임 정책 회색지대 |

## J. 특수 창고 시세 (`WinStash`)

| # | 기능 | 판정 | 비고 |
|---|---|---|---|
| J1 | POESESSID 쿠키로 `character-window/get-characters` → 계정명 → `get-stash-items`로 카드/화석/에센스 탭 읽고 poe.ninja 시세 붙여 목록화 | **보류(삭제 권장)** | 세션 쿠키를 앱에 넣는 구조라 보안상 별로고, 비공식 legacy API라 언제 막힐지 모름. 계정명 `#태그`도 바뀜. 살린다면 OAuth 공식 API로 새로 짜야 함 |

---

## 포팅 시 구조 개선 (원본 문제점)

- `WinMain` partial class 하나에 파서·쿼리·네트워크·UI 전부 섞임 → `ItemParser` / `QueryBuilder` / `TradeClient` / `DataStore` / UI 로 분리해서 파서·쿼리는 UI 없이 단위 테스트
- 네트워크를 UI 스레드에서 `Thread.Join()`으로 막고 기다림 → `QNetworkAccessManager` 비동기
- 레이트 리밋 무시 → `X-Rate-Limit-*` 헤더 기반 스로틀러
- 정규식 매 줄 수천 번 생성 → 옵션 텍스트 인덱스(`#` 정규화 문자열 → 후보 목록) 미리 구축
- JSON 문자열 replace로 필드 제거 → `QJsonObject` 구조적 빌드
- 하드코딩된 화폐/교환 목록 → static API의 KO/EN을 id로 매칭해 자동 생성
- 기존 `Config.txt` / `Checked.txt` 포맷은 그대로 읽게 해서 기존 사용자 이관 가능하게

## 플랫폼 의존 부분 (Windows 전용 Win32 API)

`SetClipboardViewer`, `RegisterHotKey`, `GetForegroundWindow`/`FindWindow("POEWindowClass")`, `SendInput`(UTF-16), `SetWindowsHookEx(WH_MOUSE_LL)`, `WS_EX_NOACTIVATE`, `GetWindowRect`/`GetClientRect`

→ 플랫폼 계층(`platform/win/*`)으로 격리. 리눅스(X11) 지원하려면 XGrabKey / XTest / `_NET_ACTIVE_WINDOW` 로 별도 구현 필요. Wayland는 전역 단축키·키 주입·포커스 감지가 사실상 막혀 있어서 비현실적.
