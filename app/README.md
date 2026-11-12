# Tolkeyen Audioguia 0.1.0

Aplicacion desktop portable C++17, wxWidgets 3.2.12, miniaudio 0.11.26 y nlohmann/json 3.12.0. El manual operativo es `../LEEME.md`.

## Estructura

Las dos carpetas principales son `app` y `audio`. El ejecutable permanece en `app`; las rutas del JSON son relativas a la carpeta que contiene ambas. El directorio de trabajo no interviene. Los 48 MP3 posibles los aporta el operador; solo se usan 42 asociaciones de los 14 puntos definidos.

```text
app/
  TolkeyenAudioguia.exe
  CMakeLists.txt
  CMakePresets.json
  config/
  src/app/
  src/domain/
  src/audio/
  src/config/
  src/input/
  src/ui/
  tests/
  scripts/
  docs/
  licenses/
  vendor/
  .tools/                 herramientas de desarrollo, no del portable
  build/                  compilacion y pruebas, no del portable
  dist/                   ZIP de distribucion
```

## Compilar En Windows

Desde Windows PowerShell, sin instalar herramientas globalmente:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\bootstrap.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\build.ps1
```

`-ExecutionPolicy Bypass` se limita a ese proceso de PowerShell; no modifica la politica global ni evita Windows Application Control. El bootstrap descarga y verifica versiones fijas, extrae w64devkit bajo `app/.tools` y las bibliotecas en `app/vendor`. No modifica PATH persistente ni solicita elevacion. La primera compilacion de wxWidgets tarda varios minutos; las siguientes son incrementales.

El preset es `windows-x86-release`. El resultado es `app/TolkeyenAudioguia.exe`. Si una politica del equipo impide ejecutar un test, puede compilar sin ejecutarlos con:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\build.ps1 -SkipTests
```

Esto NO significa que los tests hayan pasado: revise el resultado de CTest y `docs/testing.md`.

## Pruebas

```powershell
app\.tools\w64devkit\bin\ctest.exe --test-dir app\build\windows-x86-release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\smoke-test.ps1
```

El smoke test crea una copia aislada bajo `app/build/ui-smoke`, genera WAV identificados `TEST_ONLY`, usa volumen de app 1%, guarda capturas y verifica la app real. Activa ventanas para acciones simuladas; no debe ejecutarse durante la operacion del servicio. Nunca modifica los MP3 de `audio` ni el JSON de produccion. `-NoAudio` ensaya faltantes sin reproduccion. `-UnicodeRoot` prueba una raiz con espacios, ene y tilde. `-Mp3Fixture ruta.mp3` utiliza un MP3 sintetico de prueba ya preparado, no material de pasajeros.

Las pruebas C++ no necesitan framework. `core_tests`, `controller_tests` y `keyboard_tests` son deterministas; `audio_tests` utiliza explicitamente el backend null de miniaudio en tiempo real, no la salida del operador. Ese backend nunca se usa como fallback de produccion.

`scripts/test-linux.sh` permite verificar el nucleo y el backend null en WSL con un GCC 15 local y sysroot completo. Puede indicar `AUDIOGUIA_LINUX_TOOLCHAIN`; no es un build de la interfaz ni valida WASAPI. El entorno usado se detalla en `docs/testing.md`.

## Distribuir

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\package.ps1
```

Genera `app/dist/TolkeyenAudioguia-x86-0.1.0.zip` con `Audioguia/app` y `Audioguia/audio`, sin compiladores, fuentes de dependencias ni archivos de prueba. El paquete vacio usa la configuracion de ejemplo y crea las tres carpetas de idiomas.

Para incluir los audios propios y el mapa actual, despues de verificarlos:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\package.ps1 -IncludeAudio
```

Para incluir también la botonera dedicada, ejecutar primero
`app/scripts/prepare-bridge.ps1` y empaquetar con `-IncludeAudio -IncludeBridge`.
El runtime se copia a `app/bridge`, junto con sus licencias e instalador oficial.
El controlador se instala una vez por PC con permisos de administrador y reinicio;
la selección del Genius y la preferencia se guardan en el perfil local, fuera del ZIP.
Consultar `docs/interception-prueba.md` para el procedimiento completo.

Se conservan los originales. No distribuya audios sin los derechos correspondientes. No se aplica compresion ejecutable ni se empaquetan DLL del sistema. El ZIP incluye licencias, manual e identificacion SHA-256 del ejecutable.

## Windows 7

El objetivo previsto es Windows 7 SP1 x86 o posterior, CPU con SSE2. Se usa MSVCRT del sistema, bibliotecas estaticas, `WINVER` y `_WIN32_WINNT=0x0601`, subsistema PE 6.1 y APIs de endpoint disponibles desde Vista. WebView, WinRT y wxMediaCtrl estan deshabilitados. El manifiesto solicita `asInvoker`, no administrador, e incluye estilos visuales y compatibilidad DPI con fallback.

Esto reduce dependencias y evita imponer UCRT o Visual C++ Redistributable como instaladores, pero NO demuestra compatibilidad. Deben probarse Windows 7 SP1 de 32 bits y Windows 10 reales o en VM, sus controladores de audio y la cuenta estandar. XP esta fuera del alcance. Las API opcionales de versiones posteriores usadas por las bibliotecas se resuelven dinamicamente; se auditaron tambien las importaciones estaticas del PE.

Consulte `docs/architecture.md`, `docs/testing.md` y `vendor-versions.json`.
