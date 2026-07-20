#!/usr/bin/env bash
set -euo pipefail

desktop="${1:?desktop entry path required}"
grep -Fxq 'Exec=shaula launch' "${desktop}"
grep -Fxq 'Actions=QuickCapture;CaptureArea;CaptureFullscreen;CaptureAllScreens;Settings;' "${desktop}"
grep -Fxq '[Desktop Action QuickCapture]' "${desktop}"
grep -Fxq 'Exec=shaula capture quick --json' "${desktop}"
grep -Fxq '[Desktop Action CaptureArea]' "${desktop}"
grep -Fxq 'Exec=shaula capture area --json' "${desktop}"
grep -Fxq '[Desktop Action CaptureFullscreen]' "${desktop}"
grep -Fxq 'Exec=shaula capture fullscreen --json --save' "${desktop}"
grep -Fxq '[Desktop Action CaptureAllScreens]' "${desktop}"
grep -Fxq 'Exec=shaula capture all-screens --json --save' "${desktop}"
grep -Fxq '[Desktop Action Settings]' "${desktop}"
grep -Fxq 'Exec=shaula settings' "${desktop}"

printf 'ok desktop entry actions\n'
