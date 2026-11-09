# Arquitectura

## Responsabilidades

- `domain`: catalogo, enums estables, preferencias, snapshot de reproduccion y PlaybackPlanner. Sin wxWidgets ni miniaudio.
- `config`: JSON estricto UTF-8, validacion de claves, claves repetidas, numeros, IDs, secuencias y rutas. Raiz portable explicita. Guardado en temporal hermano, flush y reemplazo con respaldo y rollback.
- `audio`: un worker posee contexto, dispositivo, decoder, ring buffer y secuencia. El callback consume PCM o silencio, sin I/O de archivos, bloqueos ni acceso a ventanas. Las lecturas y el avance se realizan en el worker.
- `AppController`: una seleccion preparada, un snapshot activo y un identificador de sesion. Coordina configuracion, transporte y presentacion; se puede probar con IAudioPlayer falso.
- `input`: convierte solo numpad fisico en comandos cuando esta app tiene foco. El event loop local intercepta antes de la navegacion nativa, distingue Enter extendido y repeticion. No registra Raw Input, hotkeys globales ni drivers.
- `ui`: dos wxFrame sobre el mismo controlador. Un timer de 50 ms drena eventos y actualiza solo lo que cambio. La completa muestra una botonera de dos columnas para el grupo seleccionado, controles de transporte fijos arriba y preparacion abajo. Los botones nativos incluyen una insignia con la tecla configurada. El dialogo de preferencias modifica un borrador; el editor separado de idiomas personalizados guarda los cambios inmediatamente. Ambos suspenden los atajos durante la edicion.
- La deteccion de foco de edicion se comparte entre KeyboardController y el aviso visual. Usar numpad recupera el foco de transporte sin enviar comandos de reproduccion. Cambiar grupo y cerrar dialogos tambien devuelve el foco; las actualizaciones de reproduccion no activan ventanas externas.
- El progreso cuenta marcos PCM consumidos del ring buffer, mediante un contador atomico lock-free en el callback. La duracion se consulta al decoder en el worker; si no esta disponible se conserva como desconocida hasta EOF. El worker publica muestras como maximo cada 100 ms, coalesciendo las pendientes, con sesion e indice de pista. No se calcula avance por reloj ni por lectura anticipada del decoder. La posicion representa PCM entregado al dispositivo, con la latencia habitual de su buffer, no una medicion de presentacion fisica en el parlante.
- La barra se dibuja sobre el boton Windows nativo sin cambiar activacion, foco ni accesibilidad. Los margenes de la insignia numerica los dibuja el boton, no un rectangulo opaco en el bitmap. El area de mensajes de Configuracion solo ocupa espacio cuando hay texto.
- El editor personalizado usa wxListCtrl con casillas: idiomas activos numerados primero e inactivos visibles al final. El arrastre solo acepta filas activas del mismo editor y comparte la operacion de reordenar con Subir/Bajar. La reconstruccion de filas tras cambiar una casilla se difiere hasta terminar la notificacion nativa; la seleccion se conserva por idioma.

## Reproduccion

El plan contiene punto, direccion, modo, idiomas ordenados, rutas resueltas y pausa. Cambiar las preferencias no lo modifica. Play y Stop invalidan comandos y eventos antiguos por generacion. El controlador tambien descarta eventos de otras sesiones.

Antes de iniciar se comprueban los archivos seleccionados: los inexistentes se omiten; los presentes deben ser archivos regulares y superar apertura del decoder, primer marco y seek. Si ninguno existe, se informa error. Se conservan los indices del plan original y los eventos de intervalo incluyen el indice real del siguiente idioma disponible. No se promete una auditoria completa de corrupcion de cada MP3: fallos tardios reportados por el decoder detienen la reproduccion, y ciertos danos pueden parecer EOF al propio decoder.

Una pulsacion nueva cancela lo anterior ANTES de validar lo nuevo. Si no hay archivos disponibles o uno presente es invalido, queda en error y no retoma lo anterior. DOWN 7/8 no cambian ni cancelan nada. Stop cancela tambien el intervalo entre idiomas. Pausar conserva PCM y tiempo restante del intervalo. El vaciado final deja un margen al buffer del dispositivo para no cortar la cola del sonido.

## Salida Y Concurrencia

Produccion utiliza exclusivamente WASAPI, en modo compartido, con un ID de endpoint explicito. El predeterminado se resuelve al inicio de cada secuencia y despues queda fijado. `noAutoStreamRouting` impide redireccionamiento transparente. No hay fallback a DirectSound, otro dispositivo ni backend null.

No se depende solo de las notificaciones stopped de miniaudio: IMMNotificationClient y GetState comprueban tambien desconexion durante pausa o intervalo. El callback nativo solo senala; la cancelacion y los releases pertenecen al worker. Las notificaciones de audio se transfieren mediante una cola protegida, leida por el hilo de interfaz. Al cerrar se hace join, sin detach del worker de reproduccion.

La comprobacion de existencia del dialogo usa un worker wx autocontenido con un mailbox compartido; al cerrar se cancela y no conserva referencias a ventanas. Esta comprobacion informa existencia y ubicacion, no sustituye el preflight del decoder.

## Datos Portables

La raiz se obtiene del ejecutable: `root/app/TolkeyenAudioguia.exe`. JSON: `root/app/config/audioguia.json`. Audio: `root/audio/...`. Se admiten espacios y Unicode mediante filesystem::path y APIs wide. No se deduce identidad por nombres de directorios, enumeracion ni orden fisico.

Las rutas de configuracion deben ser relativas bajo audio, sin traversal. El selector comprueba ademas enlaces que resuelvan fuera de audio. No se usa el registro para preferencias ni se copia un archivo externo sin accion explicita del operador. El selector actual solo asocia archivos ya presentes dentro de audio; no importa ni sobrescribe originales.

Un JSON invalido produce aviso y valores por defecto SOLO en memoria, con guardado deshabilitado hasta corregirlo y reiniciar. Se preserva el archivo dañado y el .bak. Los fallos de escritura mantienen las preferencias en memoria y se muestran en ambas vistas.

## Decisiones Ajustables

- 14 puntos fijos; nombres y asociaciones editables, IDs y numeros estables.
- UP 0001-0008 y DOWN 0009-0014 como mapa inicial; 0015/0016 reservados.
- Tres idiomas: es, en y pt; nombre fisico portugues 0003-PORT.
- Un intervalo comun de 0-60000 ms, inicialmente 1000 ms.
- Configuracion personalizada persistida antes de disparar, nunca un dialogo por punto.
- Al abrir se muestra siempre la vista completa, sin autoplay; la compacta recuerda posicion y siempre visible.
- Cerrar la compacta vuelve a la completa; cerrar la completa termina la app y el audio.
- Bloq Num se consulta, nunca se cambia. Enter del numpad funciona tambien con Bloq Num apagado; los digitos no.

La primera version no tiene reproduccion de puntos por GPS, integracion WT2008, acceso global al teclado, actualizador ni instalador.
