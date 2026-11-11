# Tolkeyen Patagonia Turismo - Audioguia

## Primera prueba

1. Cierre cualquier copia anterior de la aplicacion.
2. Coloque sus MP3 en las carpetas de `audio` indicadas abajo.
3. Abra `app/TolkeyenAudioguia.exe` haciendo doble clic. No mueva el ejecutable fuera de `app`.
4. Baje el volumen de la app a 10-20% para la primera prueba. Verifique tambien la salida y el volumen de Windows.
5. Seleccione UP, Simple y Espanol. Pulse Bienvenida. Debe sonar `audio/0001-ESP/0001.mp3`.
6. Pruebe Ingles y Portugues en modo Simple. Despues pruebe Secuencial y Personalizado.

No hay reproduccion automatica al abrir. No se necesitan CMake, un compilador ni permisos de administrador para usar el programa.

## Donde colocar los archivos

```text
Audioguia/
  app/
    TolkeyenAudioguia.exe
    config/audioguia.json
  audio/
    0001-ESP/       Espanol:   0001.mp3 ... 0016.mp3
    0002-ENG/       Ingles:    0001.mp3 ... 0016.mp3
    0003-PORT/      Portugues: 0001.mp3 ... 0016.mp3
```

La carpeta de portugues se llama `0003-PORT`. Conserve los cuatro digitos y la extension `.mp3`. Active la visualizacion de extensiones en el Explorador para evitar nombres como `0001.mp3.mp3`.

La app no determina el punto por el orden de los archivos: utiliza el mapa explicito de `app/config/audioguia.json`. Si sus nombres son distintos, use **Configurar audios y salida**, seleccione un punto e idioma y pulse **Buscar archivo**. Los archivos deben estar dentro de `audio`.

| Direccion | Boton | Archivo En Cada Idioma | Punto |
| --- | --- | --- | --- |
| UP | 1 | 0001.mp3 | Bienvenida |
| UP | 2 | 0002.mp3 | Carcel y ciudad |
| UP | 3 | 0003.mp3 | Isla de los Lobos |
| UP | 4 | 0004.mp3 | Isla de los Pajaros |
| UP | 5 | 0005.mp3 | Canal Beagle |
| UP | 6 | 0006.mp3 | Yamanas |
| UP | 7 | 0007.mp3 | Isla Bridge |
| UP | 8 | 0008.mp3 | Archipielago Les Eclaireurs |
| DOWN | 1 | 0009.mp3 | Monte Cervantes |
| DOWN | 2 | 0010.mp3 | Pinguinera |
| DOWN | 3 | 0011.mp3 | Estancia Harberton |
| DOWN | 4 | 0012.mp3 | Puerto Williams |
| DOWN | 5 | 0013.mp3 | Estancia Tunel |
| DOWN | 6 | 0014.mp3 | Saint Christopher |

`0015.mp3` y `0016.mp3` pueden estar en cada carpeta, pero quedan reservados: el recorrido tiene 14 puntos y no se inventaron dos puntos adicionales.

## Como Operar

- **Simple:** un punto en el idioma seleccionado.
- **Secuencial:** el mismo punto en espanol, ingles y portugues. Si falta el archivo de un idioma, lo salta y reproduce el siguiente disponible. No avanza por los demas puntos del recorrido.
- **Personalizado:** pulse Editar idiomas..., marque los idiomas y arrastre sus filas activas para ordenar, o seleccione una fila y use Subir/Bajar. Los incluidos muestran posiciones 1, 2, 3; los desmarcados quedan visibles al final, atenuados y sin numero. Al activar un idioma se agrega al final de la secuencia. Debe quedar al menos uno marcado. Los cambios se guardan al hacerlos; pulse Listo para operar. El orden completo aparece en PREPARAR SIGUIENTE.
- **Botonera:** seleccione UP o DOWN para mostrar solo sus puntos en dos columnas. Cada boton completo reproduce el punto; el numero grande indica la tecla del numpad asignada. El punto activo se resalta y su estado se muestra arriba, junto a Pausar/Reanudar y Detener.
- **Avance:** el punto activo muestra una barra verde, idioma y tiempo transcurrido / duracion. Pausar congela el avance; cada idioma comienza desde cero y Detener lo limpia. Durante el intervalo aparece Esperando proximo idioma. Una duracion desconocida se indica con --:--. La barra es informativa y no permite buscar una posicion en el audio.
- **Pausa entre idiomas:** se configura en Configurar audios y salida; valor inicial 1 segundo, rango 0-60 segundos.
- **Otro punto:** cancela lo anterior y valida la secuencia nueva antes de empezar. No hay mezcla de voces.
- **Pausar/Reanudar:** conserva la posicion. Durante el intervalo entre idiomas conserva el tiempo restante.
- **Detener:** cancela tambien idiomas pendientes y pausas.
- **Cambiar modo o idioma mientras suena:** prepara el proximo disparo; el audio activo mantiene su secuencia original.
- **Ventana unica:** toda la operacion se realiza desde la botonera principal. Cerrar la ventana detiene el audio y sale de la aplicacion.

## Numpad

1. Active Bloq Num usted mismo; la app nunca cambia su estado global.
2. Haga clic en la ventana principal para activarla. Los atajos se suspenden si el foco esta en un selector o en volumen. Pulse Usar numpad para volver a operar sin interrumpir el audio. La franja inferior indica si el numpad esta listo, suspendido por edicion, sin foco o con Bloq Num apagado.
3. Numpad 1-8 dispara el punto de la direccion activa. En DOWN, 7 y 8 no hacen nada.
4. Numpad 0 detiene y vacia la secuencia.
5. Enter del numpad pausa/reanuda. El Enter principal no es ese atajo.
6. `/` del numpad alterna UP/DOWN para el siguiente punto, sin interrumpir la reproduccion actual.
7. `*` del numpad alterna Simple, Secuencial y Personalizado para el siguiente punto.
8. `+` y `-` del numpad ajustan el volumen en pasos del 5 %, entre 0 y 100 %, incluso durante la reproduccion.

9. La tecla decimal del numpad alterna ESP, ENG y PORT solo en modo Simple, con Bloq Num activado. Prepara el siguiente punto sin modificar el audio actual; en Secuencial y Personalizado no cambia el idioma.

Los operadores `/`, `*`, `+` y `-` funcionan tambien con Bloq Num apagado; los digitos y la tecla decimal requieren Bloq Num activado.

Las repeticiones al mantener una tecla se ignoran. Los atajos se suspenden en el dialogo de configuracion y cuando se edita un selector, secuencia o volumen. La asignacion numerica se puede cambiar en `preferences.keyboard` del JSON con la app cerrada; no se admiten colisiones.

Los atajos funcionan SOLO con foco en esta app. Un numpad comun no se aisla globalmente ni puede controlar esta app en segundo plano sin interferir con otra aplicacion. No se instalaron drivers ni hooks globales.

## Errores Y Salida

- Si falta el archivo de un idioma, se omite tanto en Secuencial como en Personalizado. Por ejemplo, sin ingles se reproduce espanol y luego portugues, con una sola pausa entre ellos. Si no hay ningun archivo disponible (incluido Simple con su unico archivo ausente), se muestra un error. Los indices corresponden al orden seleccionado originalmente.
- Un archivo presente pero invalido, un error de acceso o un fallo del decodificador detiene la reproduccion con punto, idioma y ruta.
- El volumen de la app no modifica el volumen global de Windows.
- En Configurar audios y salida puede elegir un dispositivo por su ID estable. Predeterminado se resuelve al comenzar cada secuencia y queda fijo durante esa secuencia.
- Si desaparece la salida elegida, la reproduccion se detiene con error. Seleccione una salida disponible y dispare nuevamente; no hay cambio ni reanudacion automatica.
- Si no puede guardar, los cambios siguen activos en memoria y aparece un aviso. Use una carpeta con permisos de escritura, por ejemplo Documentos; no Program Files.
- Se conserva la configuracion anterior como `audioguia.json.bak`. Un JSON invalido no se sobrescribe automaticamente. Con la app cerrada, corrijalo o restaure una copia valida; el ejemplo original esta en `audioguia.example.json`.
- El ejecutable no tiene firma comercial. Si Windows o una politica corporativa lo bloquean, conserve el mensaje y solicite autorizacion o una compilacion firmada al responsable del equipo. No desactive las protecciones del sistema para probarlo.

## Lista De Prueba Con Sus MP3

1. Verifique Bienvenida en los tres idiomas por separado.
2. En Secuencial compruebe ESP > ENG > PORT, con la pausa elegida y sin cambiar de punto.
3. En Personalizado deje PORT > ESP y compruebe que no suene ingles.
4. Durante una pista pulse otra: debe terminar la anterior y comenzar el punto nuevo.
5. Pruebe Pausar/Reanudar y Detener durante pista y entre idiomas.
6. Mantenga Numpad 1 presionado: no debe reiniciar repetidamente el audio.
7. Compruebe que cerrar la ventana durante la reproduccion detiene el audio y termina la aplicacion.
8. Active otra aplicacion: el numpad no debe disparar esta audioguia; recuerde que escribira normalmente en esa otra aplicacion.
9. Compruebe DOWN 1-6 y que DOWN 7/8 no interrumpan lo que esta sonando.
10. Para ensayar un faltante, cierre la app y cambie temporalmente el nombre de un MP3. En Secuencial o Personalizado compruebe que se salta ese idioma; en Simple, que se informa que no hay audio disponible. Luego restaure su nombre.

El detalle de compilacion y validacion esta en `app/README.md` y `app/docs/testing.md`. Windows 7 SP1 y Windows 10 son objetivos previstos, no sistemas en los que se haya comprobado esta version.
