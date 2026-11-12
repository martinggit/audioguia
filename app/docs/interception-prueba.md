# Botonera Genius integrada con AutoHotInterception

## Herramientas preparadas

Descargadas de los repositorios oficiales bajo `app/.tools/numpad`, fuera de Git:

- Interception v1.0.1: https://github.com/oblitum/Interception/releases/tag/v1.0.1
- AutoHotInterception v0.9.2: https://github.com/evilC/AutoHotInterception/releases/tag/v0.9.2
- AutoHotkey v2.0.31 portable: https://github.com/AutoHotkey/AutoHotkey/releases/tag/v2.0.31

Se prepararon las DLL de Interception x86/x64 y AutoHotInterception.dll en
`AutoHotInterception/AHK v2/Lib`, segun las instrucciones oficiales.
El runtime se prepara con `app/scripts/prepare-bridge.ps1`, que descarga versiones
fijas desde GitHub, verifica SHA256 y genera `app/bridge`. Se incluye en el ZIP
solo al pasar `-IncludeBridge` al empaquetado.

## Instalacion unica

En PowerShell **como administrador**, desde la raiz del proyecto:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\interception-driver.ps1
```

Leer el resultado, guardar el trabajo y reiniciar Windows. El script no reinicia
automaticamente. Si Windows bloquea el instalador o el controlador, guardar el
mensaje y detener la prueba; no cambiar protecciones como parte de estos scripts.

## Identificacion despues de reiniciar

Cerrar Audioguia durante el diagnostico para no reproducir por accidente.
En PowerShell normal, desde la raiz:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\interception-monitor.ps1
```

1. En el monitor, seleccionar **un teclado por vez**, no todos los dispositivos.
2. Pulsar 1 y Enter en el Genius para identificar que entrada registra actividad.
3. Confirmar que el teclado principal no produce eventos en esa misma entrada.
4. Copiar VID/PID y Handle del Genius y anotar codigos de 0-9, Enter, /, *, +, -,
   decimal y Bloq Num. Anotar tambien Calculadora, Tab, Escape y Backspace.
5. Compartir esos datos antes de configurar el bloqueo por dispositivo.

El monitor es solo de diagnostico. Cerrarlo antes de usar la botonera integrada.

## Uso diario integrado

### Control de audio en segundo plano

El operador confirmo aislamiento, reposo prolongado, reinicio de Windows y
reconexion manual con el puente de prueba. La nueva version automatiza su ciclo:

1. Cerrar el monitor y la ventana anterior de prueba de aislamiento.
2. Abrir Audioguia, marcar **Botonera dedicada** y pulsar **Configurar botonera**.
3. Seleccionar el receptor Genius validado y pulsar **Guardar y capturar**.
   Solo se listan interfaces de teclado compatibles 248A:8367/MI_01; la interfaz
   de mouse nunca se captura. Si el identificador aparece duplicado, se rechaza
   la seleccion en lugar de escoger arbitrariamente.
4. Esperar **GENIUS ACTIVO**. Minimizar Audioguia y operar desde otra aplicacion.
5. Las aperturas posteriores recuerdan la opcion y arrancan el puente oculto.

La preferencia y el identificador se guardan fuera del paquete, por usuario y
computadora: `%LOCALAPPDATA%/TolkeyenAudioguia/dedicated.ini`. No viajan con los
audios ni afectan al esquema del JSON existente. **Diagnostico** abre `bridge.log`
en esa misma carpeta (rota a `.old` al superar 512 KiB).

Desmarcar la casilla libera el Genius y cierra el proceso propio. Cerrar Audioguia
detiene el audio y cierra tambien el puente. Minimizar no interrumpe el control.
El puente tiene un mutex para impedir capturas simultaneas; cada app supervisa
solo el proceso que inicio. Si falla o no responde, muestra error y requiere
reactivar la casilla o usar Configurar botonera para reintentar.

Al desconectar aparece **ESPERANDO AL GENIUS**. Cuando vuelve un unico teclado con
el identificador guardado, se captura automaticamente, sin reproducir ni reanudar
audios. Durante la ventana de deteccion puede haber teclas que lleguen a otras apps.
Identificadores ambiguos requieren configuracion; no se usa un ID numerico fijo.

El puente envia mensajes locales `TolkeyenAudioguia.DedicatedKey.v1` a la instancia
propietaria, sin simular teclas ni lanzar un proceso por pulsacion. Cada arranque
usa un identificador de sesion; las ordenes antiguas o del puente manual se ignoran.
Los numeros respetan el mapa de teclado configurado; 9 queda sin asignar por
defecto. 0 detiene, Enter pausa, / cambia recorrido, * cambia modo, +/- ajustan
volumen y decimal cambia idioma solo en Simple. Bloq Num no afecta estos codigos.

La app ignora los atajos locales de reproduccion al habilitar la botonera dedicada.
El mouse y los controles normales de Windows siguen disponibles. Durante dialogos
de configuracion se descartan las ordenes dedicadas; no se guardan para despues.
Cambiar modo, idioma o recorrido solo prepara el siguiente punto.

El estado se obtiene del puente con latidos cada 500 ms y supervision del proceso,
no solamente del valor de la casilla. No iniciar app o puente como administrador
para operar. El protocolo local no es una barrera de autenticacion entre procesos
del mismo usuario. No se configura inicio al iniciar sesion de Windows: basta
abrir el EXE. El puente integrado no muestra una ventana durante la operacion.

Pruebas automatizadas (copias y preferencias aisladas, sin audio):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\test-dedicated-input.ps1 -SimulateDevice
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\test-dedicated-input.ps1 -DeviceHandle 'HID\VID_248A&PID_8367&REV_0100&MI_01'
```

La primera simula ausencia, ambiguedad y reconexion sin filtrar hardware. La segunda
captura temporalmente el Genius real. Verifican habilitacion, sesion, comandos en
segundo plano, foco, desactivacion, persistencia y arranque automatico al reabrir.
La reconexion fisica automatica y la operacion con los sistemas de a bordo deben
comprobarse tambien por el operador.

## Paquete para los catamaranes

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\prepare-bridge.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\package.ps1 -IncludeAudio -IncludeBridge
```

El ZIP incluye el runtime AutoHotkey/AHI, las DLL, sus licencias, el script integrado
y el instalador oficial del controlador. En cada PC se instala Interception una
vez como administrador y se reinicia. Luego se selecciona el Genius y la salida
de audio en ese equipo. No se instala AutoHotkey globalmente. El paquete sin
`-IncludeBridge` conserva operacion normal con mouse/teclado, pero no captura dedicada.

Interception incluye condiciones de uso comercial y no comercial en
`app/bridge/installer/licenses`; revisar la licencia aplicable al despliegue de la
empresa. Incluir los binarios no concede una licencia comercial adicional.

### Prueba de bloqueo del Genius identificado

Se identifico el teclado `HID\VID_248A&PID_8367&REV_0100&MI_01`.
Su ID temporal era 5, pero el script lo vuelve a buscar por hardware cada vez.
La interfaz de mouse del mismo receptor no se captura.

Cerrar el monitor y Audioguia. Ejecutar desde PowerShell normal:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\interception-aislamiento.ps1
```

La ventana inicia INACTIVA. Pulsar **Activar captura del Genius**, cambiar a Bloc
de notas y probar numeros, Enter y operadores. El registro debe mostrar las acciones,
sin escribir ni mover el cursor en Bloc de notas. El teclado principal debe seguir
funcionando incluso cuando se usan ambos a la vez. Mantener una tecla no debe sumar
varios DOWN. Probar tambien las teclas auxiliares y el despertar del numpad.

Los numeros se leen por posicion fisica, sin depender de Bloq Num. Mientras esta
activa la captura se bloquean tambien las teclas sin asignar de esta interfaz.
Las funciones que el receptor exponga por otra interfaz (por ejemplo multimedia)
no se consideran verificadas: anotar cualquier filtracion.

**Liberar Genius**, cerrar la ventana o salir del script libera el teclado. No se
reproduce audio en esta fase. Si el dispositivo cambia o desaparece, se detiene la
captura y se requiere activarla manualmente de nuevo. No hay inicio automatico.

Interception documenta problemas de IDs tras reconexiones/hibernacion que pueden
requerir reiniciar para recuperar el teclado. Debe probarse el receptor concreto
y los sistemas reales antes de desplegar a bordo. VID/PID identifican el modelo,
no necesariamente una unidad unica; no bloquear un dispositivo ambiguo.

## Desinstalar

En PowerShell como administrador:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\interception-driver.ps1 -Uninstall
```

Reiniciar despues. Cerrar el monitor no desinstala el controlador.
