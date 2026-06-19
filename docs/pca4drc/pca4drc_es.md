# Caracterización de regiones de escucha para Digital Room Correction mediante Principal Component Analysis

**Autor:** Raúl Fernández Ortega  
**Fecha:** junio de 2026

> **Resumen —** *La ecualización por convolución para Digital Room Correction (DRC) se apoya habitualmente en la respuesta impulsiva medida en un único punto de escucha, cuya representatividad es objeto de debate. En este artículo se propone un método de captación multipunto, sencillo y de bajo coste, basado en Principal Component Analysis (PCA), que permite caracterizar de forma controlada la impulsiva empleada para generar el filtro FIR de ecualización. Se introducen dos propiedades complementarias del banco de medidas: la **homogeneidad**, grado de semejanza estadística entre impulsivas y cuantificable mediante correlaciones, y la **coherencia**, grado en que dicho banco responde a unos objetivos de ecualización previamente definidos y, por tanto, propiedad derivada del diseño experimental. Tras un acondicionamiento de las impulsivas (centrado del pico y resta de la media), la descomposición de su matriz de covarianza proporciona una componente principal que condensa la información común del banco y atenúa la influencia de fenómenos menos correlacionados, como las primeras reflexiones dependientes de la geometría exacta de captación. La aplicación a un caso doméstico real (16 medidas) muestra que la componente principal concentra cerca del 70 % de la varianza total. La validación mediante una segunda campaña independiente, realizada en posiciones distintas pero coherentes con la misma definición experimental, sugiere que la corrección obtenida actúa sobre las características acústicas compartidas por la región de escucha y no únicamente sobre las posiciones concretas medidas. El procedimiento redefine el objetivo de la ecualización por filtro FIR: construir una impulsiva representativa de unas condiciones de escucha previamente definidas, en lugar de buscar la impulsiva exacta de una posición concreta.*

## Introducción

La obtención de la respuesta impulsiva de un sistema de sonido, particularmente de cada uno de los altavoces que lo conforman, es una técnica muy popular, ampliamente utilizada en un campo particular, la generación de filtros FIR de respuesta inversa para la ecualización por convolución (Digital Room Correction -DRC).

Típicamente, este proceso de medición consiste en ubicar un micrófono de medida acústica en el punto a ecualizar (el punto de escucha) y medir un barrido log-sweep desde cada uno de los altavoces. Las tomas de este barrido log-sweep a su vez se convolucionan con un filtro inverso del log-sweep y permite obtener las buscadas respuestas impulsivas.

Existe numerosa literatura acerca de la representatividad, o de la idoneidad, de la medida impulsiva puntual [1, 2] o sobre posibles técnicas de medición multipunto, promediado espacial y ecualización de zona de escucha [3, 4, 5, 6, 7].

En este artículo se propone un método de captación multipunto muy sencillo, basado en PCA, un algoritmo muy conocido, que posibilita caracterizar la impulsiva final generada de modo controlado por el propio proceso de medición, así como un estimador de la coherencia del banco final de registros.

## Obtención de la batería de impulsivas

El procedimiento se inicia tomando varias capturas log-sweep diferentes entre sí. Estas diferencias pueden deberse a un cambio de ubicación del micrófono, o a variaciones en las condiciones ambientales del entorno: una cortina abierta o cerrada, una puerta lateral abierta o cerrada, etc. La condición que deben cumplir todas las posiciones de captación es que se presuma que son representativas del sistema a ecualizar.

Un primer ejemplo: tomar varias medidas en distintas ubicaciones en torno al punto de escucha principal. Las distancias entre estos puntos es precisamente una decisión de caracterización de la batería final. Puede ocurrir que el interés se centre en torno a 20 cm del punto de escucha, o en un radio de 1 m. Puede ocurrir que la zona de interés incluya un rango de alturas: en el plano de escucha, por debajo del plano y por encima del plano. Es otra decisión de captación que se entiende que formará parte de la caracterización del sistema.

Otra posibilidad es que parte del banco de registros se obtenga con un elemento ambiental en una disposición y otra parte se obtenga en otra. Por ejemplo, la mitad con unas cortinas cerradas, y la otra mitad con las mismas cortinas abiertas. Una televisión entre los altavoces para un subgrupo de medidas, y esa misma televisión fuera del entorno de escucha para otro subgrupo. Las posibilidades son muy variadas, y la definición de este espacio de escucha (geométrico y ambiental) es, básicamente, una decisión de diseño del objetivo final de ecualización.

## Homogeneidad y coherencia de un banco de impulsivas

Llegados a este punto es necesario definir dos propiedades características del citado banco de medidas obtenido en el contexto de este proceso.

En primer lugar está la homogeneidad de las medidas. La homogeneidad describe el grado de semejanza entre las impulsivas que forman la colección. Hace referencia a cuánta semejanza tienen entre sí. En general, cuanto más próximos entre sí están los diferentes puntos de captación y más invariantes son las condiciones ambientales, mayor suele ser la homogeneidad del banco.

Por otro lado, y mucho más importante en el contexto de este artículo, es la coherencia de las medidas. Cuando en la sección anterior se ha hablado de decisiones de captación, estas hacen referencia a sobre qué condiciones geométricas y ambientales se van a establecer las posiciones de medición. De este modo, la coherencia describe el grado en que dicho banco de medidas responde a los objetivos de ecualización previamente definidos y a las decisiones tomadas a este respecto.

Por lo tanto, las etapas de diseño y ejecución del proceso de captación son clave para la correcta consecución de los objetivos buscados en ecualización. En primer lugar se decide sobre qué condiciones se quiere ecualizar (pequeña área alrededor del punto de escucha, o un área grande, o sobre un mismo plano vertical o variando dicho plano). Tomada la primera decisión, hay que pasar a determinar qué puntos concretos y hasta cuántas tomas se desean obtener, para terminar el proceso con una batería de impulsivas que trata de representar los efectos de las decisiones adoptadas. Esta característica es la que se va a denominar coherencia del banco de medidas: congruencia con las decisiones previamente adoptadas.

Por lo tanto, la homogeneidad puede cuantificarse claramente (correlaciones) pero la coherencia no es una propiedad estadística intrínseca del banco de impulsivas, sino una propiedad derivada del diseño experimental utilizado para obtenerlas. Distintas baterías de medidas podrían ser igualmente coherentes con una cierta lógica de decisiones de ecualización. La selección de los puntos de captación puede realizarse mediante reglas geométricas, algoritmos específicos o criterios prácticos. En todos los casos, estas decisiones forman parte del propio diseño experimental y, por lo tanto, condicionan la coherencia de la batería obtenida.

Además, en este sentido, un banco de medidas puede ser muy homogéneo y, sin embargo, poco coherente con el objetivo perseguido. Por ejemplo, si se desea ecualizar una región amplia de escucha, una batería de medidas concentrada en una pequeña zona presentará una elevada homogeneidad, pero una baja coherencia respecto al objetivo definido.

En resumen, este proceso incorpora decisiones de diseño que dependen de los objetivos perseguidos por los técnicos y usuarios del sistema audio a ecualizar.

## Decorrelando propiedades de las medidas: Principal Component Analysis (PCA)

Una vez concluida la etapa de mediciones, el siguiente paso es la ya conocida convolución de todas estas tomas por el filtro inverso del sweep original para, de este modo, disponer de una serie de respuestas impulsivas del sistema.

A partir de estas impulsivas se pasa a una etapa de análisis específica del proceso descrito en este artículo. Se trata de transformar estos registros en otra serie equivalente en la cual cada medida está decorrelada del resto. Es lo que se conoce como Principal Component Analysis. La técnica PCA no solo va a generar una proyección del banco original sobre este nuevo espacio de ejes decorrelados, sino que, además, posibilita la reducción de este banco a otro menor (incluso a una única componente) que mantenga el máximo de información disponible originalmente.

Es decir, si se quiere reducir un banco de N impulsivas a una sola medida que maximice la fracción de información disponible, la componente principal resultante de PCA es la candidata perfecta. Si se entiende que concentra la mayor fracción de varianza del conjunto, se puede concluir que incluye, en una fracción significativa, aquellos aspectos de homogeneidad del grupo original que han sido producidos por las propias decisiones de captación.

Para proceder a aplicar PCA en primer lugar se realiza una adecuación de las señales. De cada una de estas impulsivas se toma una muestra en la cual el pico de cada una de ellas esté exactamente centrado. El objetivo de este paso previo es facilitar al máximo que PCA incluya la impulsiva directa del altavoz en su componente principal, al eliminar el efecto geométrico de la distancia al altavoz. Alinear el primer impulso supone desalinear las reflexiones, provocadas por el medio, que le van siguiendo. De esta forma se optimiza que, además de que PCA recoja en su primera componente el máximo de información de la respuesta directa del altavoz, elimine una fracción muy significativa de la energía de las reflexiones, por tener menos correlación entre sí que el primer impulso. Más adelante se mostrará un análisis gráfico de un caso real, donde se podrán apreciar estas características. 

Finalmente, como acondicionamiento estándar previo a PCA, se resta a cada impulsiva su media. Sobre las N impulsivas así preparadas se calcula la matriz de covarianza $N \times N$ entre impulsos, cuya descomposición proporciona N valores propios y sus N vectores propios asociados. Al proyectar las impulsivas sobre estos vectores propios se obtienen N nuevas señales, decorreladas entre sí, que denominaremos componentes. Será la componente principal, la asociada al mayor valor propio, la que se tomará como impulsiva representativa en el subsiguiente proceso de ecualización. Dado que el signo de los vectores propios es arbitrario, conviene revisar la fase (polaridad) de la componente principal y, si resulta invertida, corregir su signo para que quede en la misma fase que las impulsivas medidas.

Si el peso del primer valor propio es significativamente mayor que el resto, está indicando que la primera componente impulsiva recoge una gran fracción de la información global del banco de impulsos. Es decir, la primera componente recoge una información común al banco de impulsos medidos, con un peso específico elevado sobre dicho banco.

En cualquier caso, un peso muy elevado del primer valor propio no hace más exacto ni más valioso el proceso PCA. Si es decisión del medidor excluir del proceso de ecualización algún fenómeno, al afectar a la matriz de varianzas, el peso del primer valor propio podrá ser menos representativo de la colección. Pero precisamente puede ocurrir que esto sea lo que se busca. Un ejemplo podría ser tomar varias tomas en un mismo punto con unas cortinas que se abren una fracción aleatoria del total. De esta forma, la impulsiva de la primera componente no recogerá el efecto de la cortina abierta ni cerrada, su efecto acústico quedará excluido de la ecualización final. La homogeneidad del banco de medidas ha bajado por efecto del cambio en las cortinas, pero es una batería consistente con la decisión de captación respecto a las cortinas.

Con esta serie de registros, en las condiciones que se han adoptado previamente, el medidor está decidiendo qué parte común de los aspectos acústicos del sistema van a formar parte de la ecualización final. PCA extrae la información maximizada para una única componente representativa, la principal, que será la que alimente el mecanismo de generación del correspondiente filtro FIR de ecualización.

## Aplicación del modelo a un caso práctico. Análisis de resultados.

Una vez descrito el proceso, a continuación se presenta su aplicación a un caso práctico común a cualquier entorno doméstico: sala de uso familiar/social y equipo de música estándar. 

Las medidas han sido tomadas en el punto de escucha y a su alrededor en un radio aproximado de 40 cm, en posiciones que no siguen ninguna disposición geométrica en especial. Todas las tomas han sido realizadas a la misma altura y el micrófono siempre girado para apuntar al centro entre los dos altavoces. Es decir, y es importante recalcarlo, las medidas no están siempre en los ejes acústicos de los altavoces con el punto de escucha.

A continuación se presentan las 16 medidas impulsivas tomadas del altavoz del citado sistema de audio doméstico (Figuras 1 a 8). Estas medidas han sido acondicionadas a tamaño de 131072 muestras, centrando sus picos y aplicando a cada una, en este caso práctico, una ventana de Blackman centrada en el pico. Únicamente a efectos de la visualización, no de la PCA, las impulsivas se muestran normalizadas a pico igual a la unidad:

<p align="center"><img src="a_left/impulse_spectrum_1.png" alt="Figura 1: Impulsivas 1 y 2"></p>
<p align="center"><img src="a_left/impulse_spectrum_2.png" alt="Figura 2: Impulsivas 3 y 4"></p>
<p align="center"><img src="a_left/impulse_spectrum_3.png" alt="Figura 3: Impulsivas 5 y 6"></p>
<p align="center"><img src="a_left/impulse_spectrum_4.png" alt="Figura 4: Impulsivas 7 y 8"></p>
<p align="center"><img src="a_left/impulse_spectrum_5.png" alt="Figura 5: Impulsivas 9 y 10"></p>
<p align="center"><img src="a_left/impulse_spectrum_6.png" alt="Figura 6: Impulsivas 11 y 12"></p>
<p align="center"><img src="a_left/impulse_spectrum_7.png" alt="Figura 7: Impulsivas 13 y 14"></p>
<p align="center"><img src="a_left/impulse_spectrum_8.png" alt="Figura 8: Impulsivas 15 y 16"></p>

**Figuras 1 a 8.** *Respuestas impulsivas y espectros de amplitud de las 16 capturas originales (altavoz izquierdo), representadas en pares. Las impulsivas han sido acondicionadas centrando sus picos, ventanadas con una ventana de Blackman y, a efectos de visualización, normalizadas a pico unitario.*

Los valores propios calculados por PCA para este caso concreto han sido:

<div align="center">

| Id | EigenValues (variance)| Explained fraction|
| --- | :---: | :---: |
|  0  |4.844e-08|   68.99 %|
|  1  |6.109e-09|    8.70 %|
|  2  |1.889e-09|    2.69 %|
|  3  |1.526e-09|    2.17 %|
|  4  |1.299e-09|    1.85 %|
|  5  |1.251e-09|    1.78 %|
|  6  |1.160e-09|    1.65 %|
|  7  |1.113e-09|    1.59 %|
|  8  |1.094e-09|    1.56 %|
|  9  |1.034e-09|    1.47 %|
| 10  |9.677e-10|    1.38 %|
| 11  |9.601e-10|    1.37 %|
| 12  |9.038e-10|    1.29 %|
| 13  |8.759e-10|    1.25 %|
| 14  |8.132e-10|    1.16 %|
| 15  |7.760e-10|    1.11 %|

</div>

Aspectos a destacar, que, aunque ya han sido descritos previamente, son claramente visibles en las gráficas:

1. Las impulsivas directas del altavoz son muy semejantes entre todas las medidas y son el impulso más destacado. Como posteriormente se mostrará, la propia respuesta del altavoz va a formar parte principal de la ecualización final.  
2. Las primeras reflexiones son muy distintas entre impulsivas, tanto en nivel como en posición. Posteriormente veremos cómo quedan representadas en la componente principal de PCA.
3. Las respuestas en frecuencia de las impulsivas son muy semejantes entre sí a bajas frecuencias. A bajas frecuencias lo que domina es el comportamiento modal de la sala. Como los puntos han sido medidos a una distancia entre ellos menor de 80 cm, el comportamiento modal en todos ellos es muy semejante.
4. En cambio, en agudos hay bastante variabilidad, efecto claro de la distancia y el ángulo de incidencia entre el eje del micrófono y el eje del altavoz.

A continuación se muestran las gráficas de las impulsivas obtenidas por PCA, las 16 componentes (Figuras 9 a 16):

<p align="center"><img src="a_left/PCA_spectrum_1.png" alt="Figura 9: Componentes PCA 1 y 2"></p>
<p align="center"><img src="a_left/PCA_spectrum_2.png" alt="Figura 10: Componentes PCA 3 y 4"></p>
<p align="center"><img src="a_left/PCA_spectrum_3.png" alt="Figura 11: Componentes PCA 5 y 6"></p>
<p align="center"><img src="a_left/PCA_spectrum_4.png" alt="Figura 12: Componentes PCA 7 y 8"></p>
<p align="center"><img src="a_left/PCA_spectrum_5.png" alt="Figura 13: Componentes PCA 9 y 10"></p>
<p align="center"><img src="a_left/PCA_spectrum_6.png" alt="Figura 14: Componentes PCA 11 y 12"></p>
<p align="center"><img src="a_left/PCA_spectrum_7.png" alt="Figura 15: Componentes PCA 13 y 14"></p>
<p align="center"><img src="a_left/PCA_spectrum_8.png" alt="Figura 16: Componentes PCA 15 y 16"></p>

**Figuras 9 a 16.** *Componentes PCA obtenidas a partir del banco de 16 impulsivas originales, representadas en pares. Los impulsos están normalizados a pico en 1; los espectros se muestran sin normalizar para poner de manifiesto las diferencias de nivel entre componentes.*

La representación de los impulsos se ha normalizado a pico en 1 para poder ser apreciados, no así en este caso los espectros, que están sin normalizar. Como indica la tabla de valores propios, casi un 70% de la varianza total del banco de 16 muestras queda contenida en la componente principal.

Como se puede apreciar, la componente principal ha recogido la mayor parte del comportamiento modal común en bajas frecuencias de grupo de impulsos analizado.

Otro aspecto significativo es que la componente principal presenta mucho menos variación en agudos que el resto de componentes. Esto indica, como a su vez la impulsiva muestra, que las reflexiones "duras" han desaparecido de esta componente, acumulándose en el resto de componentes de menor valor informativo. Este aspecto ya se comentó en la sección anterior como un efecto positivo de la adecuación previa de las impulsivas medidas.

El siguiente paso del estudio es generar el filtro FIR de ecualización (en este caso aplicando [DRC-FIR](https://drc-fir.sourceforge.net/) con una configuración estándar) a partir de esta componente principal, y aplicarlo de vuelta al sistema de sonido doméstico mediante el uso de un convolver ([BruteFIR](https://torger.se/anders/brutefir.html)). Y, en este momento, volver a tomar otras 16 medidas. Estas medidas no se van a tomar en puntos coincidentes con los anteriores, sino en nuevas posiciones pero manteniendo la coherencia buscada: área de 40 cm de radio alrededor del mismo punto de escucha definido y la sala doméstica en las mismas condiciones iniciales.

Estas son las 16 nuevas impulsivas medidas tras la ecualización (Figuras 17 a 24):

<p align="center"><img src="a_eq_left/impulse_spectrum_1.png" alt="Figura 17: Impulsivas EQ 1 y 2"></p>
<p align="center"><img src="a_eq_left/impulse_spectrum_2.png" alt="Figura 18: Impulsivas EQ 3 y 4"></p>
<p align="center"><img src="a_eq_left/impulse_spectrum_3.png" alt="Figura 19: Impulsivas EQ 5 y 6"></p>
<p align="center"><img src="a_eq_left/impulse_spectrum_4.png" alt="Figura 20: Impulsivas EQ 7 y 8"></p>
<p align="center"><img src="a_eq_left/impulse_spectrum_5.png" alt="Figura 21: Impulsivas EQ 9 y 10"></p>
<p align="center"><img src="a_eq_left/impulse_spectrum_6.png" alt="Figura 22: Impulsivas EQ 11 y 12"></p>
<p align="center"><img src="a_eq_left/impulse_spectrum_7.png" alt="Figura 23: Impulsivas EQ 13 y 14"></p>
<p align="center"><img src="a_eq_left/impulse_spectrum_8.png" alt="Figura 24: Impulsivas EQ 15 y 16"></p>

**Figuras 17 a 24.** *Respuestas impulsivas y espectros de amplitud de las 16 nuevas capturas realizadas tras la aplicación del filtro FIR de ecualización, en posiciones distintas a las originales pero manteniendo la misma coherencia experimental (radio de 40 cm alrededor del mismo punto de escucha).*

Es claramente visible el efecto de la ecualización sobre las nuevas impulsivas registradas, la variabilidad de amplitud con la frecuencia ha disminuido visiblemente, por ejemplo atenuando los modos resonantes en la zona de baja frecuencia.

Si se realiza un nuevo análisis PCA sobre este nuevo banco de medidas, tomadas en nuevas posiciones pero en alineación con el objetivo inicial, se obtienen los siguientes resultados (Figuras 25 a 32):

<p align="center"><img src="a_eq_left/PCA_spectrum_1.png" alt="Figura 25: Componentes PCA EQ 1 y 2"></p>
<p align="center"><img src="a_eq_left/PCA_spectrum_2.png" alt="Figura 26: Componentes PCA EQ 3 y 4"></p>
<p align="center"><img src="a_eq_left/PCA_spectrum_3.png" alt="Figura 27: Componentes PCA EQ 5 y 6"></p>
<p align="center"><img src="a_eq_left/PCA_spectrum_4.png" alt="Figura 28: Componentes PCA EQ 7 y 8"></p>
<p align="center"><img src="a_eq_left/PCA_spectrum_5.png" alt="Figura 29: Componentes PCA EQ 9 y 10"></p>
<p align="center"><img src="a_eq_left/PCA_spectrum_6.png" alt="Figura 30: Componentes PCA EQ 11 y 12"></p>
<p align="center"><img src="a_eq_left/PCA_spectrum_7.png" alt="Figura 31: Componentes PCA EQ 13 y 14"></p>
<p align="center"><img src="a_eq_left/PCA_spectrum_8.png" alt="Figura 32: Componentes PCA EQ 15 y 16"></p>

**Figuras 25 a 32.** *Componentes PCA del banco de 16 medidas post-ecualización, representadas en pares. Los espectros se muestran sin normalizar. La componente principal (Figura 25) evidencia los efectos de la ecualización aplicada.*

La primera componente muestra claramente la ecualización buscada (Figura 33):

<p align="center"><img src="a_eq_left/Spectrum_PCA_principal.png" alt="Figura 33: Espectro de la componente principal PCA post-ecualización"></p>

**Figura 33.** *Espectro de amplitud de la componente principal (primera componente PCA) del banco de medidas post-ecualización.*

La tabla de valores propios de este nuevo set de medidas es la siguiente:

<div align="center">

| Id | EigenValues (variance) | Explained fraction|
| --- | :---: | :---: |
|  0  |1.304e-07   |57.47 %|
|  1  |3.295e-08   |14.52 %|
|  2  |1.479e-08   | 6.51 %|
|  3  |6.021e-09   | 2.65 %|
|  4  |5.692e-09   | 2.51 %|
|  5  |4.803e-09   | 2.12 %|
|  6  |4.149e-09   | 1.83 %|
|  7  |3.934e-09   | 1.73 %|
|  8  |3.623e-09   | 1.60 %|
|  9  |3.511e-09   | 1.55 %|
| 10  |3.394e-09   | 1.50 %|
| 11  |3.198e-09   | 1.41 %|
| 12  |2.989e-09   | 1.32 %|
| 13  |2.860e-09   | 1.26 %|
| 14  |2.486e-09   | 1.10 %|
| 15  |2.140e-09   | 0.94 %|

</div>

Conviene notar que la componente principal de este segundo banco concentra una fracción de varianza menor que la del banco original (57,47 % frente a 68,99 %). Lejos de ser un problema, resulta coherente con el efecto buscado: la ecualización ha atenuado la energía modal común en baja frecuencia, que es precisamente la que la primera componente concentraba. Al reducirse esa parte común y corregible, gana peso relativo lo que un único filtro FIR no puede corregir —las diferencias dependientes de la posición, como primeras reflexiones y variabilidad en agudos—, restando dominancia a la componente principal. En otras palabras, la propia caída de la fracción es una firma de que la corrección ha actuado sobre la componente común del banco. A ello se suma, como factor menor, que este segundo banco se ha medido en posiciones distintas (aunque coherentes con la misma zona de interés), lo que introduce una variabilidad de muestreo adicional sobre la estructura de autovalores.

Los resultados experimentales obtenidos sugieren que aplicar PCA para ecualizar una batería de medidas coherente (entendiendo por coherencia lo descrito en la sección relativa a definiciones) resulta en un filtro FIR que también mantiene su validez sobre otro banco de medidas de coherencia semejante.

El procedimiento aquí descrito maximiza la fracción de información incorporada al proceso de ecualización cuando se restringe a una única impulsiva representativa. Es decir, para ecualizar una zona de escucha, de la amplitud deseada, tras la aplicación de PCA sobre una batería de medidas coherente con la zona definida, los resultados experimentales obtenidos sugieren que la ecualización mantiene su validez sobre otros grupos de medidas coherentes con la misma definición experimental. 

El aspecto más relevante del experimento no es que la componente principal inicial pueda ser ecualizada, sino que una segunda campaña de captación independiente, realizada en nuevas posiciones pero bajo las mismas condiciones de coherencia, produce nuevamente una componente principal ecualizada. Esto sugiere que la corrección obtenida no actúa sobre posiciones concretas de medición, sino sobre las características acústicas comunes al banco definido por el proceso experimental.

## Variantes del procedimiento a explorar

Como ya se ha mencionado, el objetivo con el que se deciden las condiciones de medida del banco impulsivo a ecualizar, mediante la aplicación de una única componente obtenida por descomposición PCA, no es disponer de una representación exacta del espacio global del sistema, sino que dicho banco presente condiciones coherentes de ecualización. Las impulsivas incluidas deben ser congruentes con las decisiones de captación.

En este sentido, existe una opción matemática que, modificando las características de estas impulsivas, podría aumentar su coherencia de diseño (en el sentido del objetivo buscado). Dicha opción es la de ponderar de modo individualmente distinto cada impulso. Un primer ejemplo sería ponderar cada impulso de modo que todos ellos estén normalizados a pico igual, o a energía igual. O asignar a cada impulso un peso distinto representativo de la condición buscada.

Estas modificaciones de ponderación modificarán a su vez el resultado del análisis PCA y, por lo tanto, la componente principal a ecualizar representará un objetivo distinto.

De hecho, como ya se ha citado, el acondicionamiento previo de las impulsivas buscando su misma longitud y todas ellas centradas en su pico, es una decisión en búsqueda de coherencia: destacar el importante rol del primer impulso acústico generado por el altavoz.

Otra forma de modificar la representatividad de los impulsos medidos en el resultado de PCA es, simplemente, repetir registros en condiciones idénticas.

Todas ellas son decisiones de diseño de la ecualización: tan válido puede ser tomar una sola medida en el punto de escucha como tomar 48 medidas en un área de 1 $\text{m}^2$, siempre y cuando el medidor sea consciente de la decisión que está tomando. Evidentemente, si las correlaciones entre sí del grupo de impulsivas son muy bajas, es muy probable que la coherencia interna de dicho grupo no sea muy representativa. Pero dichas correlaciones no tienen por qué ser, en sí mismas, una medida de la calidad de esta batería de impulsos para su aplicación en ecualización.

Otra posibilidad a explorar es emplear, como impulsiva a invertir para generar el filtro FIR de ecualización, no solo la componente principal sino una combinación lineal de primera y segunda componentes. Esto aumenta la fracción de información que se incorpora al proceso de ecualización.

## Conclusiones

En este artículo se ha presentado un procedimiento de caracterización multipunto de regiones de escucha para procesos de Digital Room Correction basado en Principal Component Analysis.

A diferencia de los enfoques tradicionales centrados en una medida impulsiva puntual, el método propuesto parte de un banco de medidas obtenidas bajo unas condiciones geométricas y ambientales previamente definidas. Dichas condiciones constituyen el verdadero objetivo de ecualización y forman parte inseparable del proceso de captación.

Para describir este proceso se han introducido dos conceptos complementarios. Por un lado, la homogeneidad, entendida como el grado de semejanza estadística entre las impulsivas medidas. Por otro, la coherencia, entendida como el grado en que una batería de medidas responde a un mismo objetivo de ecualización previamente definido. Mientras que la homogeneidad es una propiedad cuantificable del banco de impulsivas, la coherencia es una propiedad derivada del diseño experimental utilizado para obtenerlas.

La aplicación de PCA permite condensar la información contenida en una batería coherente de medidas en una única impulsiva representativa. El análisis experimental realizado muestra que la componente principal resultante conserva la información común del banco y reduce significativamente la influencia de fenómenos menos correlacionados, como determinadas reflexiones especulares dependientes de la geometría exacta de captación.

La validación experimental efectuada mediante una segunda campaña independiente de mediciones muestra que la ecualización obtenida a partir de la componente principal mantiene sus efectos sobre nuevos grupos de medidas coherentes con la misma definición experimental. Este resultado sugiere que la corrección obtenida actúa sobre las características acústicas compartidas por la región de escucha considerada, y no únicamente sobre las posiciones concretas utilizadas durante el proceso inicial de captación.

Desde esta perspectiva, el proceso de captación deja de ser una fase pasiva de adquisición de datos para convertirse en una fase activa de definición del propio objetivo de ecualización. La selección de las condiciones geométricas y ambientales de medición pasa a formar parte esencial del resultado final, permitiendo adaptar la corrección obtenida a regiones de escucha y escenarios de uso definidos explícitamente por el usuario o por el técnico responsable de la calibración.

Finalmente, este procedimiento modifica la forma de pensar a la hora de ecualizar por filtro FIR un sistema de audio: el objetivo ya no es encontrar la impulsiva correcta de una posición concreta, sino construir una impulsiva representativa de una serie de condiciones de escucha previamente definidas.

## Referencias

[1] A. Mäkivirta, T. Lund, "Is single microphone position enough for immersive system equalization and level calibration in production monitoring?", *AES E-Library*, paper 20402, 2019. Disponible en: <https://aes.org/publications/elibrary-page/?id=20402>

[2] A. Mäkivirta, T. Lund, "Spatial stability of the frequency response estimate and the benefit of spatial averaging", presentado en la *141ª Convención de la AES*, Los Ángeles, paper 9629, octubre 2016. Disponible en: <https://www.aes.org/events/141/papers/?ID=5035>

[3] S. Bharitkar, P. Hilmes, C. Kyriakakis, "Robustness of spatial average equalization: A statistical reverberation model approach", *J. Acoust. Soc. Am.*, vol. 116, no. 6, pp. 3491–3497, diciembre 2004. DOI: 10.1121/1.1819509

[4] F. Lingvall, L.-J. Brännmark, "Multiple-point statistical room correction for audio reproduction: Minimum mean squared error correction filtering", *J. Acoust. Soc. Am.*, vol. 125, no. 4, pp. 2121–2128, abril 2009. DOI: 10.1121/1.3075615

[5] S. Cecchi, L. Palestini, P. Peretti, L. Romoli, F. Piazza, A. Carini, "Evaluation of a multipoint equalization system based on impulse response prototype extraction", *J. Audio Eng. Soc.*, vol. 59, no. 3, pp. 110–123, 2011. Disponible en: <https://aes.org/publications/elibrary-page/?id=16789>

[6] S. Cecchi, A. Carini, S. Spors, "Room response equalization — A review", *Applied Sciences*, vol. 8, no. 1, p. 16, MDPI, enero 2018. DOI: 10.3390/app8010016

[7] C. Tuna, A. Zevering, A. G. Prinn, P. Götz, A. Walther, E. A. P. Habets, "Data-driven local average room transfer function estimation for multi-point equalization", *J. Acoust. Soc. Am.*, vol. 152, no. 6, pp. 3635–3647, diciembre 2022. DOI: 10.1121/10.0015218
