# Validacion De La Version 0.1.0

Fecha: 2026-11-06. Host real: Windows 11 Home Single Language 25H2 x64, build 26200.9711. Compilacion nativa Windows x86 con w64devkit 2.11.0, GCC 16.2.0, CMake 4.4.4 y Ninja 1.13.2.

## Resultados Reales

| Prueba | Entorno | Resultado |
| --- | --- | --- |
| Modelos, los tres modos y ambas direcciones; JSON, rutas, Unicode, respaldo e I/O | Windows 11 x86 y WSL Linux | 157/157 casos |
| Controlador, snapshots, cancelacion, eventos atrasados, preferencias y errores | Windows 11 x86 y WSL Linux | 45/45 casos |
| Traduccion del numpad, foco, edicion, repeticion y remapeo | Windows 11 x86 y WSL Linux | 55 comprobaciones |
| Motor miniaudio, null explicito en tiempo real | WSL2 Ubuntu 26.04 x64, GCC 15.2.0 local | 15/15 pruebas |
| Motor miniaudio recuperado, null explicito y rutas Unicode completas | Windows 11 x86, 2026-11-09 | 15/15 pruebas |
| App wxWidgets real, WAV sintetico y WASAPI | Windows 11 x64 ejecutando el binario x86 | 38/38 comprobaciones |
| App real, MP3 sintetico y raiz con espacios, ene y tilde | Windows 11 x64 ejecutando el mismo binario x86 | 38/38 comprobaciones |

Las pruebas null cubren orden e indices, stop durante pista y pausa entre idiomas, pausa/reanudacion de PCM e intervalo, reemplazo de punto, descarte de eventos, ultimo archivo faltante antes de iniciar, MP3/WAV invalidos, Unicode, volumen, dispositivo inexistente, concurrencia de consultas y cierre en distintos estados.

El 2026-11-09 se recuperaron 51 archivos desde los objetos conservados por Git tras descartar archivos nuevos sin commit. Antes de modificar nada se verificaron los 24/24 hashes de los fuentes contra el informe original y se guardo un respaldo fuera del repositorio. Se recompilo el proyecto completo y pasaron las cuatro suites CTest en Windows. La prueba nativa de rutas con chino y emoji detecto una limitacion de apertura por stdio; se corrigio usando el VFS Unicode de miniaudio, respaldado por CreateFileW, sin modificar la biblioteca ni desactivar protecciones. Los fuentes restantes se conservaron sin cambios funcionales.

Las pruebas de la app real cubren inicio detenido, botoneras, alternancia de vistas, preferencias, reproduccion WASAPI, pausa/reanudacion, los tres modos, error de faltantes, numpad en ambas vistas, repeticion de Enter y digitos, suspension sobre un selector, rechazo sin foco y finalizacion sin robar el foco a una ventana externa. La interaccion de teclado se simula con mensajes Win32 locales; no reemplaza una prueba manual del teclado fisico del operador.

Se ejecuto una copia con solo el ejecutable y configuracion, desde un directorio de trabajo diferente (`Windows`) y un PATH limitado al sistema. La prueba con raiz Unicode uso un MP3 realmente codificado con LAME a partir de un WAV sintetico, no un WAV renombrado a .mp3. Los archivos TEST_ONLY quedan exclusivamente bajo build; no se cargaron audios en las carpetas de produccion ni se incluyen en el ZIP.

## Revision Visual

### Botonera de operacion (2026-11-09)

Se recompilo la interfaz Windows x86 y pasaron las cuatro suites CTest. La nueva ventana muestra un solo grupo UP/DOWN en dos columnas, teclas grandes segun la asignacion configurada, estado del punto activo y transporte fijo. Se revisaron capturas de la ventana completa, a 1024 x 740, de la compacta pausada y del editor de idiomas. Los ocho puntos UP y los seis DOWN caben en las dimensiones ensayadas; ventanas mas pequenas pueden requerir desplazamiento en la botonera.

El smoke test actualizado termino con 57 comprobaciones aprobadas en una copia aislada con raiz Unicode y WAV sinteticos sobre WASAPI. Incluye cambio de grupo, limites y ausencia de solapamiento de botones, los tres modos, dialogos, pausa/reanudacion, Enter del numpad y su repeticion, aviso de edicion, recuperacion de foco con Usar numpad y finalizacion sin activar la app sobre una ventana externa. Se ajusto la invocacion automatizada de botones a BM_CLICK asincrono para no bloquear los dialogos modales; se conservan comprobaciones especificas de mensajes de mouse.

Bloq Num estaba apagado: el bloque de digitos del numpad se omitio y no se modifico su estado global. La traduccion de teclas paso en la suite keyboard; las teclas fisicas, otros monitores y escalas DPI siguen requiriendo validacion en el equipo del operador. Evidencia local: `app/build/ui-smoke/evidence-unicode/` (desde la raiz del repositorio).

### Editor de secuencia con arrastre (2026-11-09)

Se reemplazo el selector desplegable por una lista nativa de tres filas con casillas, posiciones y estado de inclusion. Las cuatro suites CTest pasaron. El smoke test sobre una copia con raiz Unicode termino con 76 comprobaciones aprobadas: incluye Subir/Bajar, arrastre con mouse en ambas direcciones (incluido ultimo a primero), activacion y exclusion, persistencia del orden, permanencia de las tres filas y rechazo de desmarcar el ultimo idioma. Se revisaron las capturas `sequence-editor.png` y `sequence-three-languages.png` en `app/build/ui-smoke/evidence-unicode/`. El bloque de digitos del numpad se omitio por Bloq Num apagado, como en la verificacion anterior.

### Capturas iniciales

Se abrieron la referencia temporal original y capturas de la aplicacion real. Se corrigio la distribucion inicial para mostrar los ocho puntos UP y los seis DOWN en la vista principal de prueba, manteniendo numeracion celeste, superficies claras, nombres legibles e identidad textual. El editor personalizado ocupa el espacio libre bajo DOWN. No se invento un logo ni se usa la imagen como interfaz.

La aplicacion usa controles Windows reales, no una replica pixel por pixel del dibujo. Las vistas completa, compacta pausada, preferencias y error se compararon visualmente. Las capturas, dimensiones y hashes se conservan localmente en `screenshots/` y `visual-verification.json`; los resultados del smoke test estan en `test-results/`. Son evidencia generada, excluida de Git para no publicar rutas y preferencias del equipo. Se pueden volver a generar con los scripts de prueba y record-verification. En pantallas mas pequenas se dispone de desplazamiento; deben verificarse los DPI y resoluciones concretos de las computadoras de a bordo.

## Limitaciones Pendientes

### Cambio posterior: omision de idiomas sin archivo (2026-11-09)

Se recompilo el ejecutable Windows x86 y pasaron las cuatro suites CTest (core, controller, keyboard y audio). La suite de audio incorpora una regresion para archivos ausentes al principio, en medio y al final de una secuencia, y para todos los idiomas ausentes. Comprueba orden e indices originales y el siguiente idioma anunciado durante el intervalo. La prueba de fallo previo al inicio ahora utiliza un MP3 presente pero invalido. El controlador comprueba que un intervalo puede anunciar portugues directamente despues de espanol.

Esta validacion usa el backend null en las pruebas del motor; no es una nueva prueba de escucha con los MP3 operativos ni una nueva ejecucion del smoke test. Los resultados anteriores describen la version previa, que rechazaba cualquier faltante.

### Validaciones de plataforma y hardware

- Windows 7 SP1 de 32 bits: compatibilidad prevista, NO ejecutada ni comprobada.
- Windows 10: compatibilidad prevista, NO ejecutada ni comprobada.
- Cuenta estandar y maquina totalmente limpia: pendientes. El manifiesto es asInvoker y no solicita elevacion, pero las pruebas actuales heredaron el contexto del entorno de desarrollo.
- Desconexion fisica y reconexion de USB/HDMI/jack durante reproduccion, pausa e intervalo: pendientes en hardware real. El monitor de endpoint esta implementado, sin fallback silencioso.
- Audios reales de Tolkeyen, niveles de salida y escucha en el sistema del barco: pendientes de que el operador los cargue.
- Teclado integrado y numpad externo fisicos, otros dispositivos de salida, DPI adicionales y cambios de monitores: requieren prueba manual en los equipos objetivo.
- Se comprueba apertura y primer marco de todos los archivos antes de iniciar, no la integridad total de un MP3. Algunos danos pueden ser interpretados como EOF por el decodificador.
- En la validacion inicial del 2026-11-06, `audio_tests.exe` fue bloqueado por Code Integrity (eventos 3033/3077). No se desactivo ni se evadio esa politica. En la recompilacion de recuperacion del 2026-11-09, la suite pudo ejecutarse y las cuatro suites pasaron. La autorizacion de binarios sin firma puede variar entre equipos; no se garantiza que otras politicas permitan ejecutarlos.
- El ejecutable distribuido no tiene firma Authenticode comercial. Las politicas de otros equipos pueden exigir autorizacion o firma.

Las importaciones estaticas se revisaron: DLL del sistema y MSVCRT, sin wx DLL, libstdc++ DLL ni dependencia UCRT externa. Esta inspeccion y las macros de destino NO sustituyen las pruebas reales de Windows 7 y 10.

## Repetir

Desde la raiz Audioguia:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File app\scripts\build.ps1 -SkipTests
app\.tools\w64devkit\bin\ctest.exe --test-dir app\build\windows-x86-release -R "^(core|controller|keyboard)$" --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File app\scripts\smoke-test.ps1
```

El smoke test activa y mueve ventanas de prueba, incluyendo una ventana externa para verificar foco. Ejecucion solo de desarrollo, no durante un recorrido ni mientras el operador utiliza el equipo. No cambia Bloq Num, el volumen del sistema ni politicas de seguridad.

Para aceptacion con los MP3 propios, seguir la lista de `../../LEEME.md`. Conservar el mensaje de error completo, punto, idioma y dispositivo elegido al informar un problema.
