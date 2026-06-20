# Cómo se monta un equipo NatAmbio para ejecutar en software NatAmbio

Una vez desarrollado el concepto, explicadas las ideas y el algoritmo, presentadas las herramientas y el software principal del sistema, llega la pregunta que todo futuro usuario se hará: ¿Cómo se construye un equipo NatAmbio completo?

NatAmbio tiene un cerebro, el PC DSP que alberga el software y todo el control, y unos pulmones, la interfaz de audio, encargada de llevar la señal estéreo al cerebro para su procesamiento y devolver posteriormente el resultado al previo, amplificador y altavoces. Vamos a describir ambos elementos y a explicar cómo se conectan con el resto del equipo de sonido.

## Un PC para DSP/NatAmbio

En primer lugar se necesita un PC que albergue el sistema operativo GNU/Linux (recomendación: distribución Debian). Dado que va a estar inmerso en un sistema de sonido, uno de sus requisitos es que sea silencioso. Que funcione sin ruido. Existen muchos sistemas con ventiladores de bajo ruido, pero la que seguro la mejor opción es un PC con refrigeración pasiva. Ahí el ruido es cero. Tradicionalmente, los equipos de refrigeración pasiva estaban asociados a procesadores de baja potencia. Sin embargo, la situación actual es muy diferente y existen soluciones capaces de ejecutar NatAmbio con amplio margen de recursos.

Como referencia práctica, mi sistema doméstico está montado empleando un PC silencioso con una placa base [Asrock N100DC-ITX](https://www.asrock.com/mb/Intel/N100DC-ITX/index.la.asp), trabajando sobre un equipo de sonido de doble dipolo estéreo más subwoofer gestionado por NatAmbio. La configuración incluye un NAE, dos filtros XTC, un filtro de cruce low/high-pass y cuatro filtros DRC, sumando diez procesos de convolución de 16384 muestras. La carga total de CPU ronda el 5% con jackd funcionando a 256 muestras por período. El procesador dispone de cuatro núcleos y NatAmbio aprovecha su naturaleza multithread para repartir eficientemente la carga.

![CPU load NatAmbio](figs/htop_natambio.png)

El ejemplo de carga mostrado es empleando la interfaz de sonido Edirol FA-101.

Otra posible placa base, que yo he tenido, y que funciona, aunque es de menor potencia que la anterior, es la placa base [Mini ITX Atom N2800MT E PD11TI-2](https://resources.mini-box.com/online/MBD-I-DN2800MT-PD11TI-MITAC/MBD-I-DN2800MT-PD11TI-MITAC-specs.pdf). Existen muchas otras placas de refrigeración pasiva candidatas a alojar un sistema NatAmbio, así como PC compactos también sin ventiladores, con todos los componentes ya integrados.

Una ventaja de este tipo de soluciones para NatAmbio es que son bastante económicas.

Por supuesto, al menos mi equipo NatAmbio doméstico no tiene ni pantalla, ni ratón, ni teclado. Funciona de modo autónomo con NatAmbio, y jackd, ejecutándose como servicios bajo systemd.. Y los trabajos de gestión y actualización los hago conectado por sesión ssh. Para ello tengo conectado mi PC por ethernet a la red doméstica.

## Un interfaz de sonido para NatAmbio

El interfaz de sonido de NatAmbio es la puerta de entrada y salida del sonido en el PC. Un interfaz de calidad asegura un equipo de audio de calidad. Por calidad entendemos muy baja distorsión, respuesta en frecuencia plana, muy bajo ruido de fondo, niveles suficientes en entrada y salida.

Personalmente, mis interfaces de sonido son los FireWire. Aunque FireWire es hoy una tecnología obsoleta, precisamente esa obsolescencia ha creado un mercado de segunda mano extraordinariamente atractivo para NatAmbio. Muchas interfaces profesionales de muy alta calidad pueden encontrarse a precios reducidos y continúan funcionando perfectamente bajo GNU/Linux mediante FFADO. Por un coste muy razonable se puede equipar a NatAmbio de una excelente interfaz audio.

He adquirido, y he probado, varias interfaces de sonido compatibles con la librería firewire audio para GNU/Linux, [FFADO](https://ffado.org/), y, correctamente configuradas, funcionan estables sin cortes (xrun) durante días. Ahora mismo dispongo de dos Echo Audiofire4, que he llegado a conectarlas en cadena sin problema, una Edirol FA66 y una Edirol F101. Las cuatro funcionan perfectamente. Las interfaces Echo tienen controles software gestionables por ffado-dbus-service. Las interfaces Edirol tienen los controles en la propia tarjeta.

Para poder disponer de interfaz FireWire en el PC, he instalado en la placa base una tarjeta PCIe a Firewire, que actualmente es fácil de encontrar a bajo coste.

La opción actual más práctica es disponer de una interfaz de audio USB. Yo he probado el correcto funcionamiento de NatAmbio con una tarjeta muy sencilla, de calidad razonable, de Behringer, la UMC204HD, y con una tarjeta de calidad alta, la Focusrite Scarlett 6i6. En el caso de la tarjeta Scarlett es imprescindible instalar [alsa-scarlett-gui](https://github.com/geoffreybennett/alsa-scarlett-gui) para poder activarla y configurar todos sus controles de ruta y de nivel. La interfaz Focusrite Scarlett es muy estable durante días con jackd, al igual que la interfaz UMC204HD.

Recomendación: siempre que sea posible, utilizar una entrada SPDIF, ya sea óptica o coaxial. Al mantener la señal en dominio digital hasta el propio PC, se evita introducir ruido analógico adicional en el trayecto de entrada. Del mismo modo, una salida SPDIF hacia un DAC externo moderno permite obtener una calidad de conversión excelente con un coste relativamente contenido (SMSL, Topping, etc.). 

| Interfaz | Tipo | Entradas/Salidas | Ventajas | Inconvenientes | Perfil recomendado |
|---|---|---|---|---|---|
| Behringer UMC204HD | USB | 2 In / 4 Out | Muy económica, funciona directamente | Ligero ruido de fondo | Primer sistema NatAmbio económico |
| Focusrite Scarlett 6i6 | USB | 6 Out + SPDIF | Muy estable, buena calidad, ideal para NatAmbio | Requiere alsa-scarlett-gui | Sistema moderno recomendado |
| Edirol FA-101 | FireWire | Muchas E/S | Excelente calidad, barata de segunda mano | Requiere FireWire (obsoleto) | Usuario Linux con hardware FireWire |
| Echo Audiofire4 | FireWire | Compacta y muy configurable, barata de segunda mano | Calidad profesional, controles DBUS | Requiere FireWire (obsoleto) | Usuario avanzado que quiera máxima flexibilidad |

### Interfaces FireWire: guía básica de uso

Los usuarios de interfaces FireWire compatibles con FFADO pueden acceder a la configuración avanzada de sus dispositivos mediante DBUS.

Una pequeña guía práctica con ejemplos reales de detección de dispositivos, exploración de controles y modificación de parámetros puede encontrarse en:

[Guía DBUS para dispositivos FFADO](dbus_for_ffado.txt)

Cuando se emplea FFADO como librería JACK, los nombres de los puertos son específicos del interfaz. La siguiente captura muestra una instancia real de NatAmbio ejecutándose sobre una Edirol FA-101, con una entrada estéreo SPDIF y seis salidas destinadas al dipolo frontal, dipolo trasero y subwoofer:

![Edirol FA101 NatAmbio](figs/edirol_fa_101_natambio.png)

Aunque sus entradas y salidas están asociadas al clásico alias en JACK, system:capture_X para la entradas y system:playback_X.

La interfaz Edirol FA-101 presenta sus controles en la propia tarjeta, no así Echo Audiofire4, la cual tiene muchos controles por DBUS. Una breve lista de algunos de ellos se encuentra en [Pequeña guia de comandos DBUS para dispositivos FFADO](dbus_for_ffado.txt)

### Interfaces USB: más modernos y sencillos

Es muy sencillo localizar la tarjeta de sonido USB en nuestro GNU/Linux:

```
$cat /proc/asound/cards
 0 [PCH            ]: HDA-Intel - HDA Intel PCH
                      HDA Intel PCH at 0x6001120000 irq 144
 2 [USB            ]: USB-Audio - Scarlett 6i6 USB
                      Focusrite Scarlett 6i6 USB at usb-0000:00:14.0-5.4, high speed
```

Cuando jackd emplea ALSA sobre una interfaz USB, la distribución de carga entre hilos es diferente a la observada con FFADO sobre FireWire. Sin embargo, el consumo total de CPU es prácticamente equivalente.

![CPU load Focusrite Scarlett NatAmbio](figs/htop_focusrite_scarlett.png)

En cualquier caso, la carga de proceso es equivalente al citado en el primer apartado, donde el dispositivo audio era la Edirol FA-101. 

Se incluyen las capturas de pantalla de cómo se configura una tarjeta Focusrite Scarlett con alsa-scarlett-gui para que sirva de guía a otros usuarios:

Configuración global de la tarjeta. En mi caso la Scarlett está sincronizada al reloj SPDIF externo.
![alsa_scarlett_gui_01](figs/alsa_scarlett_gui_01.png)

Configuración de rutas internas. Se observa cómo las entradas SPDIF son encaminadas hacia los PCM internos de ALSA.

![alsa_scarlett_gui_02](figs/alsa_scarlett_gui_02.png)

Mezclador interno de la Scarlett. Esta matriz permite crear mezclas independientes entre entradas físicas, PCM y salidas hardware.
![alsa_scarlett_gui_03](figs/alsa_scarlett_gui_03.png)

Para terminar de documentar esta sección, se muestra el caso del uso de la tarjeta USB Behringer UMC204HD. 

![Behringer 204UMC NatAmbio](figs/behringer_umc204_natambio.png)

En este caso, se dispone de cuatro salidas analógicas, dos para cada dipolo NatAmbio. En este caso no se podría añadir un subwoofer con gestión del propio NatAmbio, pero con gestión propia del subwoofer en paso desde las salidas playback_1 y playback_2 es perfectamente viable.

En cuanto a rendimiento de proceso, todas las interfaces aquí comentadas operan con jackd sin xrun alguno durante días, y la carga de procesado de todas ellas es semejante. Las diferencias, inevitables, están en sus conectividades y en la calidad interna de sus electrónicas. Pero, en cualquier caso, hasta la más modesta UMC204 es de calidad adecuada para conformar un sistema doméstico  NatAmbio.

# Conectando el equipo al cerebro de NatAmbio

A partir del ejemplo de conexiones de jackd con la interfaz Focusrite Scarlett 6i6 se analiza cómo realizar todo el conexionado.

![Focusrite Scarlett NatAmbio](figs/focusrite_scarlett_natambio.png)

Siendo un equipo estéreo, la señal de entrada serán dos canales, izquierdo y derecho. Quien tenga más de una fuente, necesitara un previo para conmutarlas, o conectar y desconectar cables, o disponer de una tarjeta con varías entrada de línea. En mi instalación doméstica, la única fuente de sonido es la televisión, conectada por bluetooth a un receptor sencillo, pero con salida SPDIF (coaxial y óptica), por eso la entrada son los canales capture_5 y capture_6, que corresponden con la entrada SPDIF coaxial de la Scarlett 6i6.
Estas dos entradas se desdoblan a su vez en cuatro entradas a NatAmbio, dos para NAE modo alpha, que procesará el sonido para el dipolo estéreo frontal, y otras dos para NAE en modo beta, que procesará el sonido para el dipolo estéreo ambiental.
Las salidas de la Scarlett se conectan del siguiente modo:

- Salidas playback_5 y playback_6 son las salidas SPDIF de la Scarlett, que conectan con un DAC SMSL M300, que tiene una excelente SNR. Y desde las salidas analógicas XLR del DAC conecto las entrada de mis monitores frontales, unos Genelec 8020A.
- Salidas analógicas playback_1 y playback_2 son las que se conectan con el subwoofer, un Edifier T5s. Se pueden encaminar ambas salidas de la señal filtrada para subwoofer a un solo canal playback sin ningún problema.
- Salidas analógícas playback_3 y playback_4 son las que se conectan con el dipolo trasero ambiental, formado por un amplificador Fosi Audio V1.0G de 50W por canal y unos altavoces Roxel RBS 300, que tienen una calidad adecuada para unos simpleas altavoces de ambiente y un precio muy competitivo.

De mi experiencia mi recomendación es:

- Elegir una pareja de altavoces frontales de un calidad contrastada. Evidentemente cada cual tiene sus preferencias subjetivas, pero es mejor invertir más en los altavoces frontales que plantearse que ambos dipolos tengan calidad igualada.
- Elegir un subwoofer si se considera que la respuesta en frecuencia de la pareja frontal se queda corta. Mi caso es el típico de monitores frontales de calidad contrastada, con muy buen foco y dispersión, pero pequeños, con lo que el apoyo de un subwoofer es interesante.
- Elegir unos altavoces ambientales muy sencillos, pero cuyo sistema (sea el propio altavoz activo o el amplificador conectado) nos permita ajustar el volumen facilmente. De esta manera es muy sencillo controlar el nivel de ambiente trasero que se quiere recibir, incluso modificándolo según las grabaciones escuchadas.

## Un último consejo

Un último comentario: finalmente NatAmbio es un sistema muy flexible y, ciertamente, bastante complejo. Está lleno de posibles ajustes. Todo ello genera una sensación de permanente cambio, descubrimiento, trabajo y búsqueda. Se puede permanecer semanas, incluso meses, en lo que parece un proceso inacabable de ajustes.

De mi experiencia recomiendo que, a partir de un montaje operativo, con un sonido que resulte satisfactorio, se le dedique tiempo simplemente al disfrute de la música. Cuando NatAmbio desaparece de nuestro ruido mental y toda la atención vuelve a la música, el objetivo se ha cumplido. En ese momento ya no estamos escuchando un sistema de audio: estamos simplemente disfrutando de nuestras grabaciones favoritas.





