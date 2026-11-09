# Guía de desarrollo — Tolkeyen Audioguia

## Objetivo y estado del proyecto

Desarrollar y mantener una aplicación desktop portable para Windows que permita al operador reproducir audios de un recorrido turístico. La base ya está implementada: extenderla respetando su arquitectura y comportamiento existente.

La aplicación dispone de botoneras UP/DOWN, tres modos de reproducción, tres idiomas, selección de salida, volumen propio, preferencias persistentes, atajos del numpad y vistas completa y compacta. El uso es local, con archivos aportados por el operador.

## Stack y plataforma

- C++17, namespace `audioguia`.
- wxWidgets 3.2.12 para controles y ventanas nativas.
- miniaudio 0.11.26 para reproducción; WASAPI compartido en producción.
- nlohmann/json 3.12.0 para configuración.
- CMake y Ninja; toolchain portable w64devkit descargada por PowerShell.
- Compilación Windows x86 obligatoria en el CMake actual; enlace estático de bibliotecas.
- Windows 7 SP1 con SSE2 es un objetivo previsto. Windows 11 tiene validaciones documentadas; Windows 7 y Windows 10 siguen pendientes de validación real.
- Linux/WSL permite ejecutar las pruebas específicas del núcleo y backend null, no compilar la interfaz mediante el CMake actual ni validar WASAPI.

Consultar `app/vendor-versions.json` para versiones, URLs y checksums de herramientas y dependencias. Mantener el stack actual salvo un cambio de arquitectura solicitado explícitamente.

## Documentación de referencia

- `README.md`: preparación del repositorio y flujo de desarrollo.
- `LEEME.md`: comportamiento esperado, mapa de puntos y operación.
- `app/README.md`: compilación, pruebas y distribución.
- `app/docs/architecture.md`: responsabilidades, concurrencia y decisiones.
- `app/docs/testing.md`: resultados históricos y verificaciones pendientes. No presentar esos resultados como pruebas ejecutadas en la sesión actual.

## Mapa del código

| Ruta | Responsabilidad |
| --- | --- |
| `app/src/app/App.cpp` | Inicio wxWidgets, raíz portable, creación del controlador y ventanas, bucle de teclado y timer de 50 ms. |
| `app/src/app/AppController.*` | Coordina preferencias, transporte, plan activo, sesiones y mensajes de presentación. Recibe un `IAudioPlayer` inyectable. |
| `app/src/domain/Models.*` | Catálogo, IDs, enums, preferencias y validación de modelos. |
| `app/src/domain/PlaybackPlanner.*` | Construye por valor el plan del punto y sus idiomas; no reproduce ni consulta el sistema de archivos. |
| `app/src/audio/IAudioPlayer.h` | Contrato de transporte, dispositivos y eventos. |
| `app/src/audio/MiniaudioPlayer.*` | Worker de reproducción, decodificación, buffers, pausas y supervisión de salida. |
| `app/src/config/ConfigRepository.*` | Lectura y validación JSON; persistencia con temporal, respaldo y recuperación. |
| `app/src/input/KeyboardController.*` | Traducción del numpad y filtrado de mensajes locales según foco y edición. |
| `app/src/ui/MainFrame.*` | Botonera de dos columnas del grupo seleccionado, transporte fijo, preparación y aviso de disponibilidad del numpad. |
| `app/src/ui/CompactFrame.*` | Vista compacta del mismo reproductor. |
| `app/src/ui/PreferencesDialog.*` | Edición de un borrador, asociaciones de archivos y selección de salida. |
| `app/src/ui/SequenceEditor.*` | Lista nativa con casillas, posiciones, arrastre local y Subir/Bajar; se abre desde Editar idiomas... y guarda los cambios inmediatamente. Los idiomas desmarcados permanecen visibles al final, sin posición. |
| `app/tests/` | Suites C++ de dominio/configuración, controlador, teclado y audio. |
| `app/scripts/` | Bootstrap, build, empaquetado y verificaciones. |
| `app/config/audioguia.example.json` | Configuración de ejemplo versionada. |
| `audio/` | Archivos operativos locales; solo se versionan los `.gitkeep`. |

Flujo principal: acción de UI o numpad → `AppController` → `PlaybackPlanner` → `IAudioPlayer` → eventos de audio → `AppController::poll()` → actualización de ambas vistas.

## Reglas funcionales que deben preservarse

1. Hay 14 puntos: UP 1–8 usa inicialmente archivos `0001.mp3`–`0008.mp3`; DOWN 1–6 usa `0009.mp3`–`0014.mp3`. `0015` y `0016` están reservados. Los IDs y números son estables; nombres y asociaciones son editables.
2. Idiomas: `es`, `en`, `pt`. Carpetas iniciales: `audio/0001-ESP`, `audio/0002-ENG`, `audio/0003-PORT`. Conservar la escritura exacta `PORT`.
3. Simple reproduce un idioma. Secuencial reproduce el mismo punto en español → inglés → portugués; no avanza a otro punto. Personalizado reproduce los idiomas seleccionados en el orden guardado.
4. El intervalo entre idiomas es común, de 0 a 60000 ms; por defecto, 1000 ms.
5. Solo hay un reproductor compartido por las dos vistas. No superponer narraciones ni crear otro motor al alternar ventanas.
6. Un nuevo punto válido cancela lo anterior antes de validar sus archivos. Si falla, queda en error sin retomar lo anterior. DOWN 7/8 no cambia ni interrumpe nada.
7. Antes de iniciar, omitir los archivos inexistentes y validar todos los presentes. Reproducir los idiomas disponibles en el orden seleccionado, conservando sus índices originales. Informar error si ninguno está disponible o un archivo presente es inválido/inaccesible. Los eventos de intervalo indican el índice real del próximo idioma disponible.
8. El plan activo es un snapshot. Cambiar modo, idioma o asociaciones prepara la próxima reproducción. El volumen sí se aplica en vivo; cambiar el dispositivo de salida detiene la reproducción actual.
9. Pausar conserva la posición y, durante el intervalo, el tiempo restante. Detener cancela también los idiomas pendientes y el intervalo.
10. Al iniciar se muestra la vista completa sin autoplay. Cerrar la compacta vuelve a la completa; cerrar la completa termina el audio y la aplicación.
11. Numpad 1–8 reproduce, 0 detiene y Enter del numpad pausa/reanuda con la configuración inicial. Respetar remapeos, foco, suspensión durante edición y filtrado de repetición. El Enter principal no equivale al del numpad. La app consulta Bloq Num sin modificarlo.
12. La salida predeterminada se resuelve al empezar cada secuencia y queda fijada. Una desconexión detiene con error, sin redirigir ni reanudar automáticamente. El volumen de la app no cambia el volumen global.

## Arquitectura y concurrencia

- Mantener `domain` independiente de wxWidgets y miniaudio.
- Centralizar coordinación y estado compartido en `AppController`; las ventanas presentan estado y envían acciones.
- Mostrar solo UP o DOWN en la botonera principal. Conservar botones nativos, números según el remapeo real y transporte visible al reducir la ventana. Compartir la detección de edición entre el teclado y su aviso visual; Usar numpad devuelve el foco sin iniciar ni detener audio.
- Usar `IAudioPlayer` para desacoplar y probar el controlador con un reproductor falso.
- El worker de audio es dueño del contexto, dispositivo, decoder, ring buffer y secuencia. El callback consume PCM o silencio, sin I/O de archivos, bloqueos ni acceso a ventanas.
- Transferir eventos a la UI mediante la cola existente. Preservar generaciones del motor e IDs de sesión del controlador para descartar trabajo y eventos obsoletos.
- Al cerrar, detener y hacer join del worker de reproducción. Evitar referencias a ventanas destruidas desde tareas asíncronas.
- El backend null se habilita explícitamente para pruebas; nunca es fallback de producción.
- Mantener soporte Unicode en rutas, incluido el VFS Windows del motor basado en APIs wide.

## Configuración y datos portables

- Estructura de ejecución: `<raíz>/app/TolkeyenAudioguia.exe`, `<raíz>/app/config/audioguia.json` y `<raíz>/audio/`.
- Obtener la raíz desde la ubicación del ejecutable, no desde el directorio de trabajo.
- Las asociaciones se guardan como rutas relativas a la raíz, dentro de `audio`. Mantener validación de traversal y enlaces que resuelvan fuera de esa carpeta.
- El selector asocia archivos ya presentes en `audio`; actualmente no importa archivos externos.
- Mantener JSON estricto UTF-8, validación de claves/IDs/secuencias y escritura mediante temporal hermano, flush, respaldo `.bak` y rollback.
- Un JSON inválido se conserva: usar valores por defecto solo en memoria y deshabilitar el guardado hasta corregirlo y reiniciar.
- Un fallo de escritura conserva los cambios en memoria y muestra el aviso en ambas vistas.
- Si cambia el esquema, revisar `Models`, `ConfigRepository`, configuración de ejemplo, compatibilidad con datos existentes y pruebas relacionadas.

## Compilación y ejecución

Desde la raíz del repositorio, en PowerShell de Windows:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\bootstrap.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\build.ps1
.\app\TolkeyenAudioguia.exe
```

Bootstrap descarga y verifica herramientas y bibliotecas en `app/.tools` y `app/vendor`. Build usa el preset `windows-x86-release` y ejecuta CTest por defecto. `-SkipTests` solo omite las pruebas: no equivale a una validación exitosa.

Para generar el ZIP portable:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\package.ps1
```

El resultado queda en `app/dist`. La opción `-IncludeAudio` incorpora los audios propios y el mapa actual; el paquete normal usa el ejemplo y carpetas de idiomas vacías.

## Verificación de cambios

Ejecutar las comprobaciones pertinentes al área modificada:

- `core_tests.cpp`: modelos, planes, JSON, rutas y persistencia.
- `controller_tests.cpp`: coordinación, snapshots, cancelación, preferencias y eventos atrasados.
- `keyboard_tests.cpp`: traducción, foco, repetición y remapeos.
- `audio_tests.cpp`: motor con backend null, secuencias, pausas, errores y concurrencia.

```powershell
app\.tools\w64devkit\bin\ctest.exe --test-dir app\build\windows-x86-release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\smoke-test.ps1
```

El smoke test usa una copia aislada y fixtures `TEST_ONLY` bajo `app/build/ui-smoke`; activa ventanas y puede reproducir audio. Ejecutarlo en un entorno de desarrollo. Sus opciones incluyen `-NoAudio`, `-UnicodeRoot` y `-Mp3Fixture ruta.mp3`.

En Linux/WSL, con dependencias y sysroot GCC 15 preparados:

```bash
bash app/scripts/test-linux.sh
```

El script espera la toolchain en `app/.tools/linux`, o en la ruta indicada por `AUDIOGUIA_LINUX_TOOLCHAIN`. No la instala. Informar comandos ejecutados, resultados y cualquier bloqueo del entorno. Para cambios de interfaz revisar ambas vistas; para cambios de audio comprobar cancelación, pausa y transición entre idiomas.

## Convenciones de trabajo

- Comunicación y documentación orientadas al usuario en español; mantener nombres de código en el estilo inglés existente.
- Seguir C++17, indentación de cuatro espacios, clases en PascalCase, métodos en snake_case y miembros privados con sufijo `_`.
- Usar RAII, ownership explícito y `std::filesystem::path`; no introducir rutas absolutas de un equipo.
- Revisar el estado de Git antes de editar y preservar trabajo existente.
- Incorporar pruebas de regresión relevantes cuando cambie el comportamiento y actualizar la documentación afectada.
- Mantener fuera de Git audios reales, configuración local, herramientas, dependencias descargadas, binarios, ZIP y evidencia generada, según `.gitignore`.
- Separar lo implementado de lo comprobado: siguen pendientes pruebas con audios reales, equipos de a bordo, desconexión física de salidas, teclados físicos y versiones de Windows indicadas en `app/docs/testing.md`.
