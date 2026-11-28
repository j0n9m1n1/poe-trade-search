# 오픈소스 고지 (Third-party notices)

Poe Trade Search는 [GPL-3.0](LICENSE) 자유 소프트웨어입니다. 이 문서는 배포 파일에 함께 들어 있거나 프로그램이 내려받아 쓰는 다른 저작물의 라이선스를 알립니다.

## 원본 프로그램

- **PoeTradeSearch** (phiDelPark) - GPL-3.0
  - <https://github.com/phiDelPark/PoeTradeSearch>
  - 이 프로그램은 원본을 C++ / Qt 6로 다시 만든 것입니다. 원본이 GPL-3.0이므로 같은 라이선스를 따릅니다.

## Qt 6

- **Qt 6** (Qt Core, Qt Gui, Qt Widgets, Qt Network와 windeployqt가 넣는 플러그인) - GNU LGPL-3.0
  - 배포 파일의 `Qt6*.dll`과 `plugins` 폴더가 Qt입니다. 라이선스 전문은 [packaging/licenses/LGPL-3.0.txt](packaging/licenses/LGPL-3.0.txt)에 있고, 배포 파일에는 `LGPL-3.0.txt`로 들어 있습니다. LGPL-3.0은 GPL-3.0을 바탕으로 하므로 `LICENSE`(GPL-3.0)도 함께 봐 주세요.
  - Qt는 동적 링크로 씁니다. 같은 버전의 Qt DLL을 직접 빌드하거나 수정한 것으로 바꿔 넣어 실행할 수 있습니다.
  - Qt 소스: <https://download.qt.io/official_releases/qt/>
  - 배포 빌드는 GitHub Actions에서 공식 Qt 바이너리(6.8.3)로 만듭니다. 정확한 버전은 [빌드 설정](.github/workflows/build.yml)에 있습니다.
  - Qt 안에 포함된 다른 라이브러리(zlib, PCRE2, HarfBuzz, FreeType, libpng, libjpeg, md4c 등)의 라이선스는 <https://doc.qt.io/qt-6/licenses-used-in-qt.html>에 정리되어 있습니다.

## Microsoft Visual C++ 런타임

- 배포 파일의 `msvcp140*.dll`, `vcruntime140*.dll` 등은 Microsoft Visual C++ 재배포 가능 패키지입니다. Visual Studio 라이선스의 재배포 조건에 따라 함께 넣었습니다.

## 프로그램이 내려받는 데이터

설치 파일에는 들어 있지 않고, 실행 중에 내려받아 `data` 폴더에 저장합니다.

- **Awakened PoE Trade** (SnosMe) - MIT. PoE1 옵션 매칭 데이터(`stats.ndjson`). <https://github.com/SnosMe/awakened-poe-trade>
- **Exiled Exchange 2** (Kvan7) - MIT. PoE2 옵션 매칭 데이터(`stats.ndjson`). <https://github.com/Kvan7/Exiled-Exchange-2>
- **거래소 데이터와 아이콘** - Grinding Gear Games, 카카오게임즈의 거래소 API와 이미지 서버에서 받습니다. 저작권은 각 회사에 있습니다.

## 아이콘

- 설정 창의 GitHub 아이콘은 GitHub의 [Octicons](https://github.com/primer/octicons) `mark-github`(MIT)를 그린 것입니다. GitHub 로고는 GitHub, Inc.의 상표입니다.

## 상표

Path of Exile은 Grinding Gear Games의 상표입니다. 이 프로그램은 Grinding Gear Games 및 카카오게임즈와 관계가 없습니다.
