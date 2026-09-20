# Area 51 Android build

This project uses the root CMake build and the SDL3 Android Java activity.
The SDL3 Java sources are referenced from `xCore/3rdParty/SDL3` so they are
not copied into the game sources.

Build with the Gradle wrapper shipped with SDL3:

```sh
xCore/3rdParty/SDL3/android-project/gradlew \
    -p Apps/GameApp/android \
    -PA51_ASSET_DIR=/absolute/path/to/android-assets \
    assembleRelease
```

The standalone build reads runtime data from `/sdcard/Area51` on the Quest
filesystem (`/sdcard` maps to the shared `/storage/emulated/0` volume). Copy
the complete game data there, including the DFS files
expected by `level_loader`; the APK only contains the executable and native
libraries. `A51_ASSET_DIR` remains available for development assets, but the
runtime data directory is deliberately external to the APK.
