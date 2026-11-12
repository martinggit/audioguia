# Audioguía Tolkeyen Patagonia Turismo

Una aplicación para Windows que permite **reproducir narraciones de un recorrido turístico con un clic**, en español, inglés y portugués.

Está pensada para que un guía u operador elija qué explicación escuchar en cada momento, sin tener que buscar archivos uno por uno. Por ejemplo: al llegar a Isla de los Lobos, presiona su botón y la app reproduce la narración en los idiomas elegidos.

Los audios se guardan en la computadora: no hace falta internet para reproducirlos. **El operador decide cuándo iniciar cada punto; la app no se activa por GPS ni avanza sola por el recorrido.**

## 1. Qué necesitás para probarla

- Una computadora con Windows y una salida de sonido, como parlantes o auriculares.
- La aplicación compilada: el archivo `TolkeyenAudioguia.exe` dentro de la carpeta `app`.
- Al menos un audio MP3 para escuchar una narración. Los audios reales no se incluyen en este repositorio.

La aplicación fue probada en Windows 11. Windows 7 SP1 y Windows 10 son objetivos previstos, pero todavía no tienen validación real. Windows XP queda fuera del alcance.

**Si recibiste un ZIP con la aplicación:** extraelo completo en una carpeta, por ejemplo dentro de Documentos. No ejecutes la app desde adentro del ZIP. Conservá juntas las carpetas `app` y `audio`.

**Si descargaste el código desde GitHub y no encontrás el `.exe`:** primero seguí la sección [Compilar desde el código fuente](#compilar-desde-el-código-fuente), al final de esta guía. Descargar el código no incluye el programa ya compilado.

## 2. Colocar los audios

Abrí la carpeta del proyecto en el Explorador de Windows. La estructura debe ser así:

```text
Audioguia/
├── app/
│   ├── TolkeyenAudioguia.exe
│   └── config/
└── audio/
    ├── 0001-ESP/       Audios en español
    ├── 0002-ENG/       Audios en inglés
    └── 0003-PORT/      Audios en portugués
```

La carpeta se llama **`audio`**, en singular. Conservá los nombres completos de las carpetas de idiomas, incluido `0003-PORT`.

Para una primera prueba, copiá el audio de Bienvenida en español con este nombre:

```text
audio/0001-ESP/0001.mp3
```

Para el mismo punto en portugués, usá:

```text
audio/0003-PORT/0001.mp3
```

Los archivos deben tener cuatro dígitos: `0001.mp3`, `0002.mp3`, etc. El nombre inicial esperado **no lleva el idioma al final**: `0001ESP.mp3` no coincide con `0001.mp3`. En el Explorador, activá la visualización de extensiones para evitar nombres como `0001.mp3.mp3`.

### ¿Qué archivo corresponde a cada botón?

El nombre es el mismo en las tres carpetas; cambia el idioma de la grabación.

| Grupo | Botón | Archivo | Narración |
| --- | --- | --- | --- |
| UP | 1 | `0001.mp3` | Bienvenida |
| UP | 2 | `0002.mp3` | Cárcel y ciudad |
| UP | 3 | `0003.mp3` | Isla de los Lobos |
| UP | 4 | `0004.mp3` | Isla de los Pájaros |
| UP | 5 | `0005.mp3` | Canal Beagle |
| UP | 6 | `0006.mp3` | Yámanas |
| UP | 7 | `0007.mp3` | Isla Bridge |
| UP | 8 | `0008.mp3` | Archipiélago Les Éclaireurs |
| DOWN | 1 | `0009.mp3` | Monte Cervantes |
| DOWN | 2 | `0010.mp3` | Pingüinera |
| DOWN | 3 | `0011.mp3` | Estancia Harberton |
| DOWN | 4 | `0012.mp3` | Puerto Williams |
| DOWN | 5 | `0013.mp3` | Estancia Túnel |
| DOWN | 6 | `0014.mp3` | Saint Christopher |

UP y DOWN son los dos grupos de puntos del recorrido; no son controles de volumen. Elegí el grupo con los botones grandes **UP / DOWN**: la botonera muestra solo sus puntos, en dos columnas. El número grande de cada botón indica qué tecla del numpad lo reproduce. Los archivos `0015.mp3` y `0016.mp3` están reservados y no tienen un botón asignado.

**Si querés conservar otros nombres:** copiá los archivos dentro de `audio`, abrí **Configurar audios y salida**, elegí el punto y el idioma y usá **Buscar archivo** para asociar el MP3 correspondiente. Aplicá los cambios. La app usa esas asociaciones, no el orden en que aparecen los archivos en la carpeta.

## 3. Escuchar tu primer audio

1. Abrí la carpeta `app` y hacé doble clic en **`TolkeyenAudioguia.exe`**. Dejá el ejecutable dentro de esa carpeta.
2. La ventana se abre detenida: no empieza a sonar nada automáticamente.
3. Ajustá el volumen de la app a un nivel bajo, por ejemplo 10–20%, y verificá que Windows no esté silenciado.
4. Seleccioná **UP**, modo **Simple** e idioma **Español**.
5. Presioná **Bienvenida**. Debe reproducirse `audio/0001-ESP/0001.mp3`.
6. Usá **Pausar/Reanudar** para interrumpir momentáneamente y continuar desde la misma posición.
7. Usá **Detener** para terminar la reproducción.

Arriba siempre tenés el punto actual, su idioma y los botones **Enter · Pausar/Reanudar** y **0 · Detener**. El punto activo también se resalta en la botonera. Abajo, **PREPARAR SIGUIENTE** muestra la selección que se usará en el próximo disparo.

El botón del punto activo muestra una **barra verde de avance**, el idioma y el tiempo transcurrido / duración (por ejemplo, `ESP | 00:35 / 02:10`). Se congela al pausar, empieza de nuevo con cada idioma y se limpia al detener. Durante el intervalo indica **Esperando próximo idioma**. Si no se puede obtener la duración, aparece `--:--` sin inventar un porcentaje. La barra es informativa: hacer clic sigue disparando el punto, no adelanta ni retrocede el audio.

Si querés probar portugués, seleccioná ese idioma y volvé a presionar Bienvenida. No necesitás cargar todos los puntos para hacer esta primera prueba.

## 4. Elegir cómo se reproducen los idiomas

| Modo | Qué hace | Ejemplo |
| --- | --- | --- |
| **Simple** | Reproduce el punto en un solo idioma. | Bienvenida en español. |
| **Secuencial** | Reproduce el mismo punto en español, inglés y portugués, en ese orden. | Bienvenida en español → inglés → portugués. |
| **Personalizado** | Usá **Editar idiomas...**: marcá los idiomas y arrastrá las filas activas a la posición deseada, o seleccioná una fila y usá Subir/Bajar. Terminá con **Listo**. | Bienvenida en portugués → español. |

**Si falta el archivo de un idioma, la app lo salta y sigue con el próximo disponible.** Por ejemplo, si tenés español y portugués pero todavía no inglés, Secuencial reproduce español → portugués. Si no encuentra ningún archivo para los idiomas seleccionados, muestra un error. Un archivo presente pero dañado o inaccesible también puede producir un error.

Entre idiomas hay una pausa de un segundo por defecto. Podés cambiarla en **Configurar audios y salida**, entre 0 y 60 segundos.

El editor personalizado muestra los tres idiomas juntos. Los incluidos tienen posiciones **1, 2, 3**; los desmarcados quedan al final, atenuados y sin número. Al marcar uno, se agrega al final de la secuencia activa y podés moverlo. Siempre debe quedar al menos uno seleccionado. Los cambios se guardan al hacerlos y se usan en el siguiente punto.

Al terminar los idiomas de un punto, la app se detiene. Para escuchar otro punto, presioná su botón.

### Preparar lo siguiente mientras suena un audio

- Cambiar el modo o los idiomas prepara el próximo punto que dispares; no cambia la secuencia que ya está sonando.
- Presionar otro punto cancela el anterior y comienza la nueva selección. Las narraciones no se mezclan.
- Pausar durante el intervalo entre idiomas conserva el tiempo que faltaba para continuar.
- Detener cancela tanto la pista actual como los idiomas pendientes.

## 5. Controles útiles durante el recorrido

### Volumen y dispositivo de salida

El volumen de la app controla sus narraciones, sin cambiar el volumen general de Windows. En **Configurar audios y salida** podés elegir dónde escuchar: por ejemplo, parlantes o una salida USB.

Cambiar la salida desde la app detiene la reproducción actual. Si se desconecta el dispositivo, la app informa el error; elegí una salida disponible y presioná nuevamente el punto para reproducirlo.

El área de avisos al pie de Configuración aparece únicamente cuando hay un mensaje, por ejemplo una asociación pendiente o un error al guardar. No es un campo para completar.

### Ventana de operacion

La aplicación utiliza una única ventana con la botonera, los controles y la información de reproducción. El numpad configurado se dedica automáticamente a Audioguía mediante Interception, en primer y segundo plano. El mouse sigue disponible.

### Genius dedicado en segundo plano

Con Interception instalado y el runtime preparado, la captura dedicada se activa al abrir la app;
usá **Configurar botonera** para elegir el receptor Genius la primera vez. La app
recuerda la selección en el perfil local del equipo e inicia el puente oculto en
cada apertura. **NUMPAD ACTIVO** confirma la captura. Cerrar Audioguía libera
el teclado y detiene el audio y su puente. Minimizar permite seguir
operando desde otras aplicaciones. La reconexión del mismo receptor es automática;
durante la detección puede escribir normalmente hasta recuperar la captura.

Para preparar un paquete completo con audios y puente:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\prepare-bridge.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\package.ps1 -IncludeAudio -IncludeBridge
```

En cada equipo de destino hay que instalar el controlador una vez como administrador
con `app/scripts/interception-driver.ps1` y reiniciar. El resto del runtime viaja en
el ZIP; no requiere instalar AutoHotkey globalmente. Ver
[configuración, diagnóstico y pruebas](app/docs/interception-prueba.md), incluidas
las condiciones de licencia del controlador para su despliegue comercial.

**Para salir de la aplicación, cerrá la ventana principal.** El audio se detiene al cerrar.

### Teclado numérico (opcional)

También podés operar con el mouse; no hace falta un teclado numérico para usar la app.

Con el estado **NUMPAD ACTIVO**, sin depender del foco ni de Bloq Num:

| Tecla del numpad | Acción |
| --- | --- |
| **1–8** | Reproduce el punto correspondiente del grupo seleccionado. En DOWN, 7 y 8 no hacen nada. |
| **0** | Detiene la reproducción. |
| **Enter del numpad** | Pausa o reanuda. No es el Enter principal del teclado. |

Los atajos se suspenden en los diálogos de configuración. Funcionan mientras usás otra aplicación. Mantener una tecla presionada no dispara el audio repetidamente.

La franja inferior muestra el estado real del numpad y los botones **Configurar botonera** y **Diagnóstico**. No hay casilla de activación ni botón Usar numpad.

La ventana adapta su tamaño inicial al monitor. Podés maximizarla para tener botones más amplios; en ventanas pequeñas, la zona de puntos permite desplazamiento y los controles de pausa y detener permanecen visibles.

## 6. Si algo no funciona

| Problema | Qué revisar |
| --- | --- |
| No encuentro `TolkeyenAudioguia.exe`. | El repositorio contiene código fuente. Seguí la sección de compilación o conseguí el paquete portable ya compilado. |
| Aparece “No hay audios disponibles”. | Revisá el punto, los idiomas y las rutas. Para Bienvenida en español, la asociación inicial busca `audio/0001-ESP/0001.mp3`. |
| Los archivos tienen nombres como `0001ESP.mp3`. | Renombrá a `0001.mp3` o asocialos desde Configurar audios y salida → Buscar archivo. |
| No tengo audios en inglés. | Podés usar Secuencial: saltará los archivos ausentes. También podés seleccionar solo español y portugués en Personalizado. |
| La app indica que reproduce, pero no escucho. | Revisá el volumen de la app, el de Windows, la salida seleccionada y la conexión de parlantes o auriculares. |
| Un MP3 está presente, pero aparece un error. | Probalo en otro reproductor y verificá que sea un audio válido. Renombrar otro tipo de archivo a `.mp3` no lo convierte. |
| El numpad no responde. | Revisá el estado inferior, el receptor y sus pilas. Usá **Configurar botonera** para seleccionar o reintentar el dispositivo, y **Diagnóstico** para revisar el registro. |
| Aparece un aviso al guardar preferencias. | Usá una carpeta con permisos de escritura, como Documentos. Los cambios pueden seguir activos solo durante esa sesión. |
| Aparece un aviso de configuración inválida. | Cerrá la app y revisá `app/config/audioguia.json`, o restaurá una copia válida de `audioguia.json.bak`. El archivo inválido no se sobrescribe automáticamente. |

Si el problema continúa, copiá el mensaje completo e indicá qué punto, idioma y salida estabas usando.

## Información para desarrollo

La aplicación está implementada en C++17 con wxWidgets 3.2, miniaudio y configuración JSON. Esta sección es para generar el ejecutable o modificar el código; si ya tenés la aplicación compilada, podés usarla sin instalar herramientas de desarrollo.

### Compilar desde el código fuente

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

### Qué se versiona

Se incluyen fuentes, pruebas, CMake, scripts, documentacion, versiones de dependencias y `app/config/audioguia.example.json`. Los directorios de audio se conservan mediante archivos `.gitkeep`.

No se incluyen herramientas, dependencias descargadas, compilaciones, ejecutables, ZIP, audios, preferencias locales ni capturas/logs generados. `.gitignore` los excluye sin borrarlos del disco. Cada computadora ejecuta el bootstrap para reconstruir su entorno y mantiene su propia configuracion. Los audios reales se copian por separado en las tres carpetas documentadas en [LEEME.md](LEEME.md).

### Pruebas y distribución

```powershell
app\.tools\w64devkit\bin\ctest.exe --test-dir app\build\windows-x86-release --output-on-failure
powershell -NoProfile -ExecutionPolicy Bypass -File .\app\scripts\package.ps1
```

El ZIP portable queda en `app/dist`. No se agrega al historial de Git: se puede compartir por separado o adjuntar a una GitHub Release. El paquete vacio no contiene audios ni preferencias personales.

Si una politica de Windows bloquea algun test, `build.ps1 -SkipTests` permite compilar, pero no significa que las pruebas hayan pasado. No desactivar protecciones para ejecutarlo. Consultar [los resultados y limitaciones](app/docs/testing.md).

## Más documentación

- [Uso, carga de MP3 y mapa de los 14 puntos](LEEME.md).
- [Compilacion y empaquetado detallados](app/README.md).
- [Arquitectura y decisiones](app/docs/architecture.md).
- [Validacion real y pendientes](app/docs/testing.md).
- [Versiones y checksums](app/vendor-versions.json).
- [Avisos de dependencias](app/licenses/README.md).

La aplicacion se compila x86 con Windows 7 SP1 como objetivo previsto. Windows 11 fue probado en la primera version; Windows 7 y Windows 10 no tienen validacion real. XP queda fuera del alcance. No se incluyen audios reales ni un logo independiente.
