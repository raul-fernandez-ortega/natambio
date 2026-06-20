# Cómo se monta un equipo NatAmbio para ejecutar en software NatAmbio

Una vez desarrollado el concepto, explicadas las ideas y el algoritmo, presentadas las herramientas y el software principal del sistema, llega la pregunta que todo futuro usuario se hará: ¿Cómo se construye un equipo NatAmbio completo?

NatAmbio tiene un cerebro, el PC DSP que alberga el software y todo el control, y unos pulmones, la interfaz de audio, encargada de llevar la señal estéreo al cerebro para su procesamiento y devolver posteriormente el resultado al previo, amplificador y altavoces. Vamos a describir ambos elementos y a explicar cómo se conectan con el resto del equipo de sonido.

# Un PC para DSP/NatAmbio

En primer lugar se necesita un PC que albergue el sistema operativo GNU/Linux (recomendación: distribución Debian). Dado que va a estar inmerso en un sistema de sonido, uno de sus requisitos es que sea silencioso. Que funcione sin ruido. Existen muchos sistemas con ventiladores de bajo ruido, pero la que seguro la mejor opción es un PC con refrigeración pasiva. Ahí el ruido es cero. Tradicionalmente, los equipos de refrigeración pasiva estaban asociados a procesadores de baja potencia. Sin embargo, la situación actual es muy diferente y existen soluciones capaces de ejecutar NatAmbio con amplio margen de recursos.

Como referencia práctica, mi sistema doméstico está montado empleando un PC silencioso con una placa base [Asrock N100DC-ITX](https://www.asrock.com/mb/Intel/N100DC-ITX/index.la.asp), trabajando sobre un equipo de sonido de doble dipolo estéreo más subwoofer gestionado por NatAmbio. La configuración incluye un NAE, dos filtros XTC, un filtro de cruce low/high-pass y cuatro filtros DRC, sumando diez procesos de convolución de 8192 muestras. La carga total de CPU ronda el 5% con jackd funcionando a 256 muestras por período. El procesador dispone de cuatro núcleos y NatAmbio aprovecha su naturaleza multithread para repartir eficientemente la carga.

![CPU load NatAmbio](figs/htop_natambio.png)

Otra posible placa base, que yo he tenido, y que funciona, aunque es de menor potencia que la anterior, es la placa base [Mini ITX Atom N2800MT E PD11TI-2](https://resources.mini-box.com/online/MBD-I-DN2800MT-PD11TI-MITAC/MBD-I-DN2800MT-PD11TI-MITAC-specs.pdf). Existen muchas otras placas de refrigeración pasiva candidatas a alojar un sistema NatAmbio, así como PC compactos también sin ventiladores, con todos los componentes ya integrados.

Una ventaja de este tipo de soluciones para NatAmbio es que son bastante económicas.

Por supuesto, al menos mi equipo NatAmbio doméstico no tiene ni pantalla, ni ratón, ni teclado. Funciona de modo autónomo con NatAmbio, y jackd, ejecutándose como servicios bajo systemd.. Y los trabajos de gestión y actualización los hago conectado por sesión ssh. Para ello tengo conectado mi PC por ethernet a la red doméstica.

# Un interfaz de sonido para NatAmbio

El interfaz de sonido de NatAmbio es la puerta de entrada y salida del sonido en el PC. Un interfaz de calidad asegura un equipo de audio de calidad. Por calidad entendemos muy baja distorsión, respuesta en frecuencia plana, muy bajo ruido de fondo, niveles suficientes en entrada y salida.

Personalmente, mis interfaces de sonido son los FireWire. Aunque FireWire es hoy una tecnología obsoleta, precisamente esa obsolescencia ha creado un mercado de segunda mano extraordinariamente atractivo para NatAmbio. Muchas interfaces profesionales de muy alta calidad pueden encontrarse a precios reducidos y continúan funcionando perfectamente bajo GNU/Linux mediante FFADO. Por un coste muy razonable se puede equipar a NatAmbio de una excelente interfaz audio.

He adquirido, y he probado, varias interfaces de sonido compatibles con la librería firewire audio para GNU/Linux, [FFADO](https://ffado.org/), y, correctamente configuradas, funcionan estables sin cortes (xrun) durante días. Ahora mismo dispongo de dos Echo Audiofire4, que he llegado a conectarlas en cadena sin problema, una Edirol FA66 y una Edirol F101. Las cuatro funcionan perfectamente. Las interfaces Echo tienen controles software gestionables por ffado-dbus-service. Las interfaces Edirol tienen los controles en la propia tarjeta.

Para poder disponer de interfaz FireWire en el PC, he instalado en la placa base una tarjeta PCIe a Firewire, que actualmente es fácil de encontrar a bajo coste.

La opción actual más práctica es disponer de una interfaz de audio USB. Yo he probado el correcto funcionamiento de NatAmbio con una tarjeta muy sencilla, de calidad razonable, de Behringer, la UMC204HD, y con una tarjeta de calidad alta, la Focusrite Scarlett 6i6. En el caso de la tarjeta Scarlett es imprescindible instalar [alsa-scarlett-gui](https://github.com/geoffreybennett/alsa-scarlett-gui) para poder activarla y configurar todos sus controles de ruta y de nivel. La interfaz Focusrite Scarlett es muy estable durante días con jackd, al igual que la interfaz UMC204HD.

Recomendación: siempre que sea posible, utilizar una entrada SPDIF, ya sea óptica o coaxial. Al mantener la señal en dominio digital hasta el propio PC, se evita introducir ruido analógico adicional en el trayecto de entrada. Del mismo modo, una salida SPDIF hacia un DAC externo moderno permite obtener una calidad de conversión excelente con un coste relativamente contenido (SMSL, Topping, etc.). 

### Interfaces FireWire: guía básica de uso

Cuando se conecta un interfaz firewire a un PC GNU/Linux, se puede identificar porque se crea una rama `/sys/bus/firewire/devices/fwX` (siendo X un número típicamente el 1). Alli se puede identificar:


```
/sys/bus/firewire/devices/fw1/guid → 0x0040ab0000c224

/sys/bus/firewire/devices/fw1/model_name → EDIROL FA-101

/sys/bus/firewire/devices/fw1/vendor_name → EDIROL

```
Los controles internos de cada interfaz se pueden averiguar consultando dbus:

```
dbus-send --session --dest=org.ffado.Control --type=method_call --print-reply /org/ffado/Control/DeviceManager org.freedesktop.DBus.Introspectable.Introspect
method return time=1781969853.455709 sender=:1.123 -> destination=:1.122 serial=16 reply_serial=2
   string "<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node name="/org/ffado/Control/DeviceManager">
	<interface name="org.ffado.Control.Element.Container">
		<method name="getNbElements">
			<arg direction="out" type="i" name="count"/>
		</method>
		<method name="getElementName">
			<arg direction="in" type="i" name="id"/>
			<arg direction="out" type="s" name="name"/>
		</method>
		<signal name="Destroyed">
		</signal>
		<signal name="Updated">
		</signal>
		<signal name="PreUpdate">
		</signal>
		<signal name="PostUpdate">
		</signal>
	</interface>
	<interface name="org.ffado.Control.Element.Element">
		<method name="getId">
			<arg direction="out" type="t" name="id"/>
		</method>
		<method name="getName">
			<arg direction="out" type="s" name="name"/>
		</method>
		<method name="getLabel">
			<arg direction="out" type="s" name="label"/>
		</method>
		<method name="getDescription">
			<arg direction="out" type="s" name="description"/>
		</method>
		<method name="canChangeValue">
			<arg direction="out" type="b" name="can_change"/>
		</method>
		<method name="getVerboseLevel">
			<arg direction="out" type="i" name="level"/>
		</method>
		<method name="setVerboseLevel">
			<arg direction="in" type="i" name="level"/>
		</method>
	</interface>
	<interface name="org.freedesktop.DBus.Introspectable">
		<method name="Introspect">
			<arg direction="out" type="s" name="data"/>
		</method>
	</interface>
	<node name="0040ab0000c22497"/>
</node>"

```

```
dbus-send --session --dest=org.ffado.Control --type=method_call --print-reply /org/ffado/Control/DeviceManager/0040ab0000c22497 org.freedesktop.DBus.Introspectable.Introspect
method return time=1781969955.724823 sender=:1.123 -> destination=:1.124 serial=17 reply_serial=2
   string "<!DOCTYPE node PUBLIC "-//freedesktop//DTD D-BUS Object Introspection 1.0//EN"
"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd">
<node name="/org/ffado/Control/DeviceManager/0040ab0000c22497">
	<interface name="org.ffado.Control.Element.Container">
		<method name="getNbElements">
			<arg direction="out" type="i" name="count"/>
		</method>
		<method name="getElementName">
			<arg direction="in" type="i" name="id"/>
			<arg direction="out" type="s" name="name"/>
		</method>
		<signal name="Destroyed">
		</signal>
		<signal name="Updated">
		</signal>
		<signal name="PreUpdate">
		</signal>
		<signal name="PostUpdate">
		</signal>
	</interface>
	<interface name="org.ffado.Control.Element.Element">
		<method name="getId">
			<arg direction="out" type="t" name="id"/>
		</method>
		<method name="getName">
			<arg direction="out" type="s" name="name"/>
		</method>
		<method name="getLabel">
			<arg direction="out" type="s" name="label"/>
		</method>
		<method name="getDescription">
			<arg direction="out" type="s" name="description"/>
		</method>
		<method name="canChangeValue">
			<arg direction="out" type="b" name="can_change"/>
		</method>
		<method name="getVerboseLevel">
			<arg direction="out" type="i" name="level"/>
		</method>
		<method name="setVerboseLevel">
			<arg direction="in" type="i" name="level"/>
		</method>
	</interface>
	<interface name="org.freedesktop.DBus.Introspectable">
		<method name="Introspect">
			<arg direction="out" type="s" name="data"/>
		</method>
	</interface>
	<node name="ConfigRom"/>
	<node name="Generic"/>
	<node name="Mixer"/>
</node>"
```

Cuando se emplea FFADO como librería JACK, los nombres de los puertos son específicos del interfaz:

![Edirol FA101 NatAmbio](figs/edirol_fa_101_natambio.png)

Aunque sus entradas y salidas están asociadas al clásico alias en JACK, system:capture_X para la entradas y system:playback_X.

# Conectando el equipo al cerebro de NatAmbio





