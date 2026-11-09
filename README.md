# Audioguia Tolkeyen Patagonia Turismo

Aplicacion desktop portable C++17 con wxWidgets 3.2, miniaudio y configuracion JSON. Botonera UP/DOWN, modos Simple/Secuencial/Personalizado, numpad con foco y vista compacta sobre un unico reproductor.

## Desarrollar Desde Otra Computadora

En Windows, instalar Git for Windows y abrir PowerShell en la carpeta donde guardar los proyectos. No hace falta instalar Visual Studio, CMake ni MinGW globalmente.

```powershell
git clone https://github.com/martinggit/audioguia.git
Set-Location audioguia
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\bootstrap.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\build.ps1
```

Si el repositorio es privado, GitHub pedira autenticacion y permisos para clonarlo. El bootstrap descarga versiones fijas con SHA-256 y conserva sus licencias. La primera compilacion de wxWidgets tarda varios minutos; necesita internet y espacio para herramientas y compilaciones locales.

El resultado es `app/TolkeyenAudioguia.exe`. Para ejecutarlo:

```powershell
.\app\TolkeyenAudioguia.exe
```

Para editar sin Windows se pueden revisar fuentes y documentacion, pero la interfaz y WASAPI se compilan y prueban en Windows. No se requiere WSL para el flujo normal de desarrollo.

## Que Se Versiona

Se incluyen fuentes, pruebas, CMake, scripts, documentacion, versiones de dependencias y `app/config/audioguia.example.json`. Los directorios de audio se conservan mediante archivos `.gitkeep`.

No se incluyen herramientas, dependencias descargadas, compilaciones, ejecutables, ZIP, audios, preferencias locales ni capturas/logs generados. `.gitignore` los excluye sin borrarlos del disco. Cada computadora ejecuta el bootstrap para reconstruir su entorno y mantiene su propia configuracion. Los audios reales se copian por separado en las tres carpetas documentadas en [LEEME.md](LEEME.md).

## Trabajo Entre Dos Computadoras

Antes de empezar, con el trabajo anterior guardado:

```powershell
git status
git pull --ff-only
```

Despues de editar y probar:

```powershell
git status
git add README.md app/src app/tests app/scripts app/docs
git diff --cached --stat
git commit -m "Describe el cambio realizado"
git push
```

La lista de `git add` es un ejemplo: agregar solo las rutas realmente modificadas, incluidos CMake o configuracion de ejemplo cuando corresponda. Revisar lo preparado antes del commit. Hacer commit y push antes de cambiar de computadora; los cambios sin commit no se trasladan con GitHub.

**Descartar cambios no es lo mismo que quitar archivos del area de preparacion.** Para corregir un `git add` en un repositorio con commits, usar:

```powershell
git restore --staged .
```

Esto conserva los archivos de trabajo. No usar Descartar todos, `git clean` ni `git reset --hard` para reducir la lista de archivos. Si `git pull --ff-only` informa que no puede avanzar, detenerse y revisar las ramas; no forzar el push.

## Pruebas Y Distribucion

```powershell
app\.tools\w64devkit\bin\ctest.exe --test-dir app\build\windows-x86-release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\package.ps1
```

El ZIP portable queda en `app/dist`. No se agrega al historial de Git: se puede compartir por separado o adjuntar a una GitHub Release. El paquete vacio no contiene audios ni preferencias personales.

Si una politica de Windows bloquea algun test, `build.ps1 -SkipTests` permite compilar, pero no significa que las pruebas hayan pasado. No desactivar protecciones para ejecutarlo. Consultar [los resultados y limitaciones](app/docs/testing.md).

## Documentacion

- [Uso, carga de MP3 y mapa de los 14 puntos](LEEME.md).
- [Compilacion y empaquetado detallados](app/README.md).
- [Arquitectura y decisiones](app/docs/architecture.md).
- [Validacion real y pendientes](app/docs/testing.md).
- [Versiones y checksums](app/vendor-versions.json).
- [Avisos de dependencias](app/licenses/README.md).

La aplicacion se compila x86 con Windows 7 SP1 como objetivo previsto. Windows 11 fue probado en la primera version; Windows 7 y Windows 10 no tienen validacion real. XP queda fuera del alcance. No se incluyen audios reales ni un logo independiente.
