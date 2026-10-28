# CursorCat v5

Native Windows desktop cat with a real-time Direct3D 11 3D model rendered to a transparent layered window.

## Features
- Real-time 3D mesh with lighting and depth.
- Animated four-leg walk cycle with alternating front/rear gait.
- Moving paws/legs, tail swing, head/body volume, ears, eyes and muzzle.
- Jump animation between detected Windows taskbars.
- Taskbar-only movement; supports multiple monitors.
- Much faster cursor response: cursor position is sampled every 16 ms and steering is applied immediately.
- Model is intentionally compact: 96x96 render area, about half the visual footprint of v4.
- No Python and no external runtime required.

Build in GitHub Actions on Windows with Visual Studio/MSVC and Direct3D 11.
