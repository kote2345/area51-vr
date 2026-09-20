# Area 51 — автономный порт Quest 3 и план VR

Статус: M1/M2 реализованы — общий OpenXR/Vulkan session path и validation XR frame
для PCVR и Quest 3; полноценный игровой stereo renderer и input остаются следующими этапами.

Цель первого этапа — автономная Android-версия для Meta Quest 3. Тот же общий VR-слой
используется для PCVR, а платформенные различия изолированы в loader/backend glue.

## Краткий вывод

В репозитории уже есть основа автономного Android-порта:

- `Apps/GameApp/android` — Android Application/Gradle-проект;
- `arm64-v8a`, NDK 29 и Vulkan-only настройка SDL3;
- SDL3 `SDLActivity` и отдельный `GameAppPlatform_Android.cpp`;
- текущий рендер построен на `SDL_GPU` с Vulkan-драйвером;
- игровой код уже отделён от платформенного запуска через `main_mobile.inl`.

Однако это пока обычный Android-рендер, а не VR:

- `eng_BeginFrame()` захватывает SDL window swapchain;
- `eng_EndFrame()` отправляет этот swapchain на Android surface;
- OpenXR runtime, session, actions и XR swapchains отсутствуют;
- `RenderGame()` формирует один `view` на локального игрока;
- `view` и рендер-менеджеры получают симметричную проекцию из `GetV2C()`;
- до этого в дереве отсутствовали OpenXR SDK/loader и зависимость Gradle на
  loader; теперь добавлен общий `Support/VR` bootstrap, Android prefab-зависимость
  Khronos и desktop/Android loader glue;
- README ссылается на SDL3 Gradle wrapper, но wrapper в checkout отсутствует;
- `Apps/GameApp/android/local.properties` содержит машинный Linux-путь к SDK.

Главный вывод: добавление VR нельзя свести к manifest-флагу или к отрисовке
двух половин Android-окна. Нужен XR-aware frame lifecycle и вывод в OpenXR
projection swapchain.

## Выбранное направление

### Runtime и Android

Использовать совместимый с OpenXR 1.0 API через Khronos Android Loader, а Meta
Quest-specific возможности подключать только расширениями OpenXR. Базовая сборка должна
оставаться `TARGET_ANDROID`; VR включается отдельной опцией, например
`A51_ENABLE_OPENXR`, только для `arm64-v8a`.

В manifest для VR-сборки нужен отдельный OpenXR launcher intent с
`org.khronos.openxr.intent.category.IMMERSIVE_HMD`. Обычный flat Android
variant должен сохранить текущий launcher и поведение.

### Graphics

На Quest 3 использовать Vulkan. Для VR необходимо создать Vulkan instance,
physical device, logical device и graphics queue в порядке, совместимом с
OpenXR graphics binding, после чего создать `XrSession` и использовать images
из `XrSwapchain`.

Текущий публичный SDL3 GPU API не предоставляет безопасный способ сделать
`SDL_GPUTexture` из внешнего OpenXR `VkImage`, а текущий engine создаёт Vulkan
device внутри `SDL_CreateGPUDevice()`. Поэтому не следует связывать OpenXR
swapchain с SDL window swapchain через неофициальные структуры SDL.

Предлагаемый вариант — сохранить существующий high-level render API, но
добавить изолированный native Vulkan/XR backend для Android VR. Flat Android,
Windows и Linux продолжают использовать текущий SDL GPU backend. Если в ходе
прототипа выяснится, что перенос backend слишком велик, запасной вариант —
расширить SDL Vulkan backend официальным native-device/import интерфейсом,
но это должно быть отдельным осознанным изменением SDL3.

### Игровой frame lifecycle

Для VR-фрейма порядок должен быть таким:

1. `xrPollEvent` и обработка pause/resume session;
2. `xrWaitFrame`, `xrBeginFrame`;
3. `xrLocateViews` на predicted display time;
4. один шаг игровой симуляции;
5. рендер левого и правого глаза в два layer-а/две области XR swapchain;
6. world-space/UI-pass с явным правилом, какие HUD-элементы разрешены;
7. `xrEndFrame` с `XrCompositionLayerProjection`.

`FramePacer` и ожидание SDL swapchain не должны выполняться параллельно с
`xrWaitFrame`: источником pacing для VR является runtime.

## План вертикального среза

### M0 — воспроизводимый обычный Android build

Цель: установить текущий APK на Quest 3 в flat-режиме и доказать, что данные,
звук, ввод, сохранения и Vulkan работают до изменений VR.

Работы:

- добавить нормальный Gradle wrapper или явно документировать внешний Gradle;
- убрать machine-specific `local.properties` из обязательного checkout;
- добавить `A51_ASSET_DIR`/packaging проверку и диагностический startup log;
- проверить `arm64-v8a`, Release и загрузку `BOOT`, `PRELOAD`, DFS;
- проверить lifecycle SDL activity после pause/resume;
- сохранить этот вариант как отдельный non-XR variant.

Критерий готовности: APK запускает текущую игру на Quest 3 без OpenXR и без
регрессий desktop/Linux конфигураций.

### M1 — OpenXR hello frame без игры (реализован)

Цель: минимальный Quest APK, который создаёт instance/system/session, проходит
lifecycle и выводит тестовый цветной quad/cube в левый и правый глаз.

Добавить изолированную подсистему, например:

```text
Support/VR/
  XRRuntime.hpp
  XRRuntime.cpp
  XRRuntime_Android_OpenXR.cpp
  XRInput_OpenXR.cpp
```

На этой стадии не трогать `Support/Objects`, `StateMgr` и desktop backend.
Проверить loader initialization через `SDL_GetAndroidActivity()` и JNI/Android
context, а также runtime events и потерю focus.

### M2 — XR graphics backend (validation slice реализован)

Цель: заменить только frame target/present часть для `A51_ENABLE_OPENXR`.

Вынести из SDL-specific реализации следующие понятия:

- GPU device/context;
- command buffer и render pass;
- color/depth target;
- acquire/submit/present;
- swapchain size и viewport;
- synchronization и device idle.

Сохранить общие интерфейсы ресурсов, материалов, shader bindings и object
render submission. Для первой версии допустим отдельный Vulkan implementation
для target `main`; существующие `sdleng_*` файлы для остальных targets не
менять условной компиляцией по месту.

Критерий: тестовый XR frame и один существующий `rtarget`-pass выводятся в
OpenXR swapchain без обращения к Android window swapchain.

### M3 — stereo camera и игровой рендер

Цель: показать реальный уровень в двух глазах.

Изменения:

- добавить XR frame/view data между `xrLocateViews` и `RenderGame()`;
- отделить player gameplay pose от head/eye render pose;
- рендерить world один раз на глаз, не выполняя simulation дважды;
- расширить `view`/projection path для asymmetric per-eye projection;
- обеспечить, чтобы culling, decals, fog, particles, shadows и post-effects
  использовали текущий eye view;
- запретить или адаптировать экранные эффекты, не имеющие смысла в stereo;
- отдельно определить weapon/HUD policy: world-space оружие и HUD-панель
  должны иметь корректную глубину и не быть приклеены к одному глазу.

Особое внимание: множество подсистем напрямую вызывает `eng_GetView()` и
`view::GetV2C()`. Поэтому нельзя заменить только `g_View` в `main.cpp` и
считать stereo готовым — projection override должен проходить через общий
engine/render context.

### M4 — OpenXR input

Добавить action set `gameplay` и минимум действий:

- left/right thumbstick;
- trigger и grip;
- A/B и X/Y;
- menu/pause;
- haptic output для левой/правой руки.

Сопоставление с существующим `input_gadget` должно быть отдельным адаптером.
Игровой код не должен знать о `XrPath`, interaction profile или модели
контроллера. SDL gamepad backend остаётся для flat Android и fallback-режима.

Для первого playable варианта рекомендуется locomotion через левый stick,
snap-turn через правый stick и отдельная кнопка pause. Свободное движение
головы не должно менять authoritative player simulation.

### M5 — Quest 3 performance/release

- baseline 72 Hz, затем проверка 80/90 Hz;
- GPU/CPU frame timing и dropped-frame log;
- ASTC texture packaging и проверка памяти;
- уменьшение overdraw/post-processing;
- фиксированный dynamic resolution policy после замеров;
- release signing, ABI/manifest validation и чистая установка APK;
- regression matrix: Windows, Linux, flat Android, Quest XR.

## Предлагаемая структура конфигурации

```text
A51_BUILD_SELECTED_APPS=ON
A51_ENABLE_OPENXR=OFF       # default для всех существующих targets
A51_OPENXR_LOADER=ANDROID  # используется только при A51_ENABLE_OPENXR=ON
A51_ANDROID_ABI=arm64-v8a
```

Для desktop `A51_ENABLE_OPENXR=ON` должен завершать CMake configure с понятной
ошибкой, если отсутствуют OpenXR headers/loader. Для Android loader и headers
берутся из официального Khronos AAR через Gradle Prefab; CMake должен проверить
наличие целей `OpenXR::headers` и `OpenXR::openxr_loader`. Это лучше, чем
молча включать частично собранный backend.

## Уже реализовано

### M1/M2 status

- `Support/VR/XRSession.*` adds the common OpenXR + Vulkan session path for PCVR and Quest 3;
- OpenXR events, session state transitions, `xrWaitFrame`/`xrBeginFrame`/`xrLocateViews`/`xrEndFrame`, stereo views and runtime swapchains are implemented;
- the current frame is a validation frame that clears both XR swapchains and submits a projection layer; game stereo rendering and input remain the next stages;
- the XR path is isolated behind `A51_ENABLE_OPENXR`; the default SDL frame path remains unchanged when the option is `OFF`;
- if XR initialization or frame submission fails, the application logs the error, disables the XR session and continues in flat mode.

- `A51_ENABLE_OPENXR=OFF` остаётся значением по умолчанию;
- добавлена отдельная статическая библиотека `A51OpenXR` в `Support/VR`;
- Android использует Khronos `openxr_loader_for_android:1.1.63`, JNI loader
  initialization и `XR_KHR_android_create_instance`;
- desktop и Android создают OpenXR instance и получают HMD system properties;
- при включённом флаге игра логирует состояние OpenXR, но пока продолжает
  использовать обычный SDL frame path — это намеренная промежуточная стадия,
  а не готовый VR режим;
- текущая проверка компилирует common/desktop bootstrap MSVC и common/Android
  bootstrap Clang под `aarch64-linux-android35`.

Для Android XR-варианта включение выполняется свойством Gradle из каталога
`Apps/GameApp/android`:

```text
gradle -PA51_ENABLE_OPENXR=true -PA51_ASSET_DIR=/path/to/area51-assets assembleRelease
```

Пути `A51_OPENXR_HEADERS_DIR` и `A51_OPENXR_LOADER_LIBRARY` для Android не
нужны: их предоставляет Khronos AAR через Prefab. Для desktop эти пути
задаются CMake-параметрами и зависят от установленного OpenXR runtime/SDK.

## Файлы, которые являются естественными точками изменений

- Android packaging: `Apps/GameApp/android/app/build.gradle`,
  `AndroidManifest.xml`, `A51Activity.java`;
- platform selection: `Apps/GameApp/CMakeLists.txt`,
  `cmake/A51ThirdParty.cmake`, `cmake/A51Project.cmake`;
- frame lifecycle: `Apps/GameApp/main.cpp`,
  `xCore/Entropy/SDLEngine/sdleng_core.cpp`;
- current target abstraction:
  `xCore/Entropy/SDLEngine/sdleng_rtarget.cpp`;
- camera: `xCore/Entropy/e_View.hpp/.cpp`,
  `Support/GameLib/RenderContext.*`, `Apps/GameApp/main.cpp`;
- input boundary: `xCore/Entropy/Input/backend/input_backend.hpp`,
  `xCore/Entropy/Input/backend/sdl/input_sdl.cpp`;
- existing Android filesystem boundary:
  `Apps/GameApp/GameAppPlatform_Android.cpp`.

## Что не делать на первом этапе

- не добавлять OpenXR calls непосредственно в gameplay objects;
- не подменять desktop/Linux macros на `TARGET_MOBILE` глобально;
- не использовать Android sensor API как замену OpenXR head pose;
- не выводить stereo через side-by-side texture в обычный Android surface;
- не копировать каждый глаз через CPU readback;
- не включать Quest-specific extensions до рабочего core OpenXR frame;
- не менять asset serialization flags без доказанной необходимости.

## Критерии «Quest 3 port playable»

Порт считается первым playable-срезом, когда на чистом Quest 3:

1. приложение запускается как immersive OpenXR app;
2. уровень загружается из packaged DFS без внешнего доступа к PC;
3. head tracking стабильно обновляет stereo views;
4. оба controller action set-а работают и pause не ломает session;
5. игра проходит стартовый участок с корректным world scale и без чёрного
   одного глаза;
6. выход из приложения и resume не оставляют Vulkan/OpenXR ресурсы в плохом
   состоянии;
7. flat Android, Windows и Linux продолжают собираться теми же targets.

## Внешние технические основания

- [Meta: OpenXR Support for Meta Quest Headsets](https://developers.meta.com/horizon/documentation/native/android/mobile-openxr/)
- [Meta: Creating Instances and Sessions](https://developers.meta.com/horizon/documentation/native/android/mobile-openxr-instance-session/)
- [Meta: Build and Run hello_xr](https://developers.meta.com/horizon/documentation/native/android/mobile-build-run-hello-xr-app/)
- [Khronos: OpenXR loader design and Android initialization](https://registry.khronos.org/OpenXR/specs/1.1/loader.html)
- [Khronos: OpenXR SDK changelog and Android Prefab targets](https://github.com/KhronosGroup/OpenXR-SDK/blob/main/CHANGELOG.SDK.md)
- [SDL3: SDL_CreateGPUDevice](https://wiki.libsdl.org/SDL3/SDL_CreateGPUDevice)
- [SDL3: SDL_CreateGPUTexture](https://wiki.libsdl.org/SDL3/SDL_CreateGPUTexture)
