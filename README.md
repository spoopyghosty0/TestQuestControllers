<p align="center"><img src="docs/icon.png" alt="" width="128" height="128"></p>

# Test Quest Controllers

[![Install with FramePort](https://cdn.jsdelivr.net/gh/spoopyghosty0/frameport@main/docs/badges/install-with-frameport-animated.svg)](https://frameport.app/install?manifest=https%3A%2F%2Fraw.githubusercontent.com%2Fspoopyghosty0%2FTestQuestControllers%2Fmain%2Fframeport-install.json)

Pick a Quest controller and see every input your headset delivers for it, live on a 3D model.

Steam Frame: use the button (needs [FramePort](https://frameport.app)). Quest: sideload the APK from
[Releases](https://github.com/spoopyghosty0/TestQuestControllers/releases/latest).

![Quest 2 Touch controllers in your hands, enlarged in front of you, and the panel](docs/screenshots/touch.png)

| | |
|---|---|
| ![Touch Plus](docs/screenshots/touch-plus.png) Touch Plus | ![Touch Pro](docs/screenshots/touch-pro.png) Touch Pro |
| ![Quest 1 / Rift S](docs/screenshots/quest1-rift-s.png) Quest 1 / Rift S | ![Rift CV1](docs/screenshots/rift-cv1.png) Rift CV1 |
| ![Oculus Go](docs/screenshots/go.png) Oculus Go | |

## Use

- **Pick a controller:** point at the panel, pull the trigger (or hold the stick click for 2 s).
- **Model:** blue = touched, orange = pressed or pulled.
- **Panel:** the runtime's answer, the controller it reports, and every input's live value.
- **Toggles:** vibration, passthrough, recenter.

## Build

```
./gradlew assembleRelease
```

Releases: tag `vX.Y.Z` matching `versionName` in `app/build.gradle.kts`.

## Licence

MIT. Controller models: [WebXR Input Profiles](https://github.com/immersive-web/webxr-input-profiles) (MIT). Not
affiliated with Meta.
