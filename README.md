## 실행 요구 사항

- Docker Desktop
- SDL2 GUI 창을 띄우기 위한 디스플레이 환경
  - macOS: XQuartz
  - Linux: X11
  - Windows: WSLg, VcXsrv 또는 다른 X server

macOS에서는 XQuartz를 먼저 실행한 뒤, 터미널에서 아래 명령어를 한 번 실행해 주세요.

```bash
xhost + localhost
```

## 실행 방법

저장소를 clone한 뒤, 해당 폴더 안에서 실행합니다.

```bash
git clone https://github.com/2026-Spring-PM/Team_24_Demo_gui.git
cd Team_24_Demo_gui
```


Docker 이미지를 먼저 pull 합니다.

```bash
docker pull --platform linux/amd64 hyorilee33/team_24_project:0.1.0
```

실행 권한을 local하게 부여합니다.
```bash
chmod +x main
```

그 다음 데모를 실행합니다.

```bash
bash scripts/run.sh
```

Docker 이미지는 정상적으로 받아졌지만 게임 창이 열리지 않는다면, 대부분 게임 문제가 아니라 GUI forwarding 설정 문제입니다.  
사용 중인 운영체제의 디스플레이 브리지(XQuartz, X11, WSLg 등)가 실행 중인지 확인해 주세요.

## 조작 방법

- `WASD` / 방향키: 플레이어 이동
- `Space` / `Enter`: 선택한 도구 사용 또는 상호작용
- `1-6`: 도구 선택
- `T` / `P`: 순무 씨앗 / 감자 씨앗 선택
- `Tab`: 씨앗 또는 건설 옵션 전환
- `F`: 물가 근처에서 낚시
- `M`: 기억력 미니게임 시작
- `C`: 음식을 먹어 스태미나 회복
- `Esc`: 메뉴 닫기 또는 나가기

## 주요 기능

- C++17과 SDL2로 구현한 2D 농장 마을 시뮬레이터입니다.
- 플레이어는 3/4 시점 느낌의 농장 맵을 이동하며 농사, 수확, 판매, 동물 관리, 건설을 진행할 수 있습니다.
- 작물 시스템에는 씨앗 심기, 물 주기, 성장 단계, 수확, 판매 흐름이 포함되어 있습니다.
- 닭과 소 같은 가축을 구매하고, 사료를 주고, 달걀과 우유를 얻을 수 있습니다.
- 마켓 NPC와 상호작용하여 씨앗, 사료, 동물을 구매하고 수확물을 판매할 수 있습니다.
- 스태미나 시스템이 있어 농사, 낚시, 수확 등의 행동에 에너지가 필요하며, 음식을 먹거나 침대에서 쉬어 회복할 수 있습니다.
- 날씨 시스템이 있으며 맑음, 흐림, 비, 폭풍 등의 변화가 농장 플레이에 영향을 줍니다.
- 낚시 미니게임과 기억력 카드 미니게임을 포함합니다.
- SNU 셔틀버스를 통해 캠퍼스 지역으로 이동할 수 있습니다.
- 캠퍼스에는 Building 301이 있으며, 내부에서 Breadboard Farm Lab, Grad Student Ranch Lab, Research Lab으로 이동할 수 있습니다.
- Breadboard Farm Lab에서는 저항, 커패시터, 다이오드, MOSFET, FinFET 같은 전자 소자 작물을 재배할 수 있습니다.
- Grad Student Ranch Lab에서는 연구원을 고용하여 연구 포인트를 생성할 수 있습니다.
- Research Lab에서는 연구 시설 확장, 업그레이드, 회로 디버깅 미니게임을 통해 연구 포인트를 활용할 수 있습니다.
- 연구 포인트는 돈과 별도로 관리되는 두 번째 자원이며, 연구실 시스템의 핵심 진행 요소입니다.
