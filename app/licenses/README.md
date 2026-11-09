# Licencias De Dependencias

Las dependencias completas y sus avisos se obtienen con `app/scripts/bootstrap.ps1`, usando las versiones fijadas en `app/vendor-versions.json`.

| Dependencia | Aviso Original |
| --- | --- |
| wxWidgets | `app/vendor/docs/licence.txt`, LGPL y avisos de bibliotecas incluidas |
| miniaudio | `app/vendor/miniaudio/LICENSE`, alternativa MIT-0 o Unlicense |
| nlohmann/json | `app/vendor/nlohmann/LICENSE.MIT` |
| MinGW-w64/winpthreads | `app/.tools/w64devkit/COPYING.MinGW-w64-runtime.txt` |

`package.ps1` copia esos avisos y los de JPEG, PNG, TIFF, zlib, PCRE, NanoSVG, regex y Expat al portable. Las copias generadas bajo esta carpeta no se versionan; cada clon las regenera desde los originales. Este documento no cambia la licencia de los fuentes propios de la aplicacion ni otorga derechos sobre los audios de la empresa.
