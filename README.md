# HDR White Balance for OBS

색온도와 색조를 조절하는 OBS 필터입니다. OBS 기본 **색 보정** 필터는 HDR 영상에서는 적용되지 않지만, 이 필터는 **SDR과 HDR 영상 모두에** 적용됩니다.

A color temperature / tint filter for OBS Studio that works on **both SDR and HDR** sources. The built-in Color Correction filter skips HDR content; this one does not.

## 원리 / How it works

OBS는 필터에 선형(linear) Rec.709 픽셀을 넘겨줍니다. SDR은 0~1, HDR은 1.0이 SDR 흰색이고 그보다 밝은 값이 하이라이트입니다. 화이트 밸런스는 선형 값에 R·G·B를 각각 곱하는 연산이라 두 경우 모두 똑같이 정확하게 동작하고, HDR 하이라이트도 잘리지 않습니다.

OBS passes filters linear-light Rec.709 pixels (`GS_CS_SRGB`, `GS_CS_SRGB_16F`, or `GS_CS_709_EXTENDED` for HDR). White balance is a per-channel multiply in linear light, which is valid in all of these spaces, so the filter declares and processes `GS_CS_709_EXTENDED` instead of bypassing it.

## 설정 / Settings

| 항목 | 설명 |
|---|---|
| 색온도 | 6500 K = 변화 없음. 높이면 차갑게(푸르게), 낮추면 따뜻하게(노랗게). |
| 색조 | 0 = 변화 없음. 양수는 마젠타, 음수는 초록. |
| 밝기 처리 | **밝기 유지**(기본, HDR 권장) / **채널 증폭 없음**(SDR 흰색 클리핑 방지) / **없음** |
| 빨강·초록·파랑 미세 조정 | 채널별 추가 배율 (기본 1.0) |

색온도를 살짝 차갑게 하려면 **7000~7500 K**부터 시작해 보세요.

## 설치 / Install (Windows)

1. GitHub **Actions** 탭에서 가장 최근 성공한 빌드를 열고, 아래 **Artifacts**에서 `obs-hdr-white-balance-1.0.0-windows-x64-…` 항목을 내려받습니다.
2. 받은 ZIP 안에 ZIP이 하나 더 들어 있습니다. 두 번 압축을 풀면 나오는 `obs-hdr-white-balance` 폴더를 `C:\ProgramData\obs-studio\plugins\` 안에 넣습니다(`plugins` 폴더가 없으면 만듭니다). 최종 구조:
   ```
   C:\ProgramData\obs-studio\plugins\obs-hdr-white-balance\bin\64bit\obs-hdr-white-balance.dll
   C:\ProgramData\obs-studio\plugins\obs-hdr-white-balance\data\...
   ```
3. OBS를 다시 실행하고, 소스 우클릭 → **필터** → 효과 필터 **＋** → **HDR 화이트 밸런스**를 추가합니다.

## 빌드 / Build

Based on [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate). Pushing to `main` builds Windows, macOS and Ubuntu packages with GitHub Actions.

## License

GPL-2.0-or-later
