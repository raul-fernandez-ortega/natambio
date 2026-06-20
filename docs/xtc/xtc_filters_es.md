# Diseño de un cancelador de diafonía estéreo (XTC) por convolución para NatAmbio

**Autor:** Raúl Fernández Ortega  
**Fecha:** junio de 2026

> **Resumen —** *NatAmbio incorpora la cancelación de diafonía estéreo (XTC) mediante la convolución de filtros FIR generados internamente. La diafonía acústica —cada altavoz es percibido por ambos oídos— colapsa y difumina la imagen espacial del estéreo, y dado que el aislamiento físico entre canales resulta inviable en la práctica, la mitigación se aborda por procesamiento digital. Una crítica recurrente a los canceladores XTC existentes (de las familias recursiva tipo RACE y de inversión de matriz por FIR) es que mejoran notablemente la espacialidad a costa de introducir cierta coloración tonal. En este artículo se presenta el algoritmo de diseño de filtros XTC que emplea NatAmbio, concebido para equilibrar máxima espacialidad y mínima coloración, y formulado como una generalización FIR de los esquemas recursivos clásicos. Partiendo de un análisis iterativo de las cancelaciones sucesivas que mantiene intactos los caminos acústicos directos, se obtienen un filtro directo y un filtro cruzado expresables como series de potencias de la función G (la relación entre el camino cruzado y el directo). Estas series convergen siempre que |G| < 1 —condición garantizada por la sombra acústica de la cabeza— y se truncan en tan solo N = 3–4 términos; aunque el proceso de diseño es recursivo, el filtro realizado es FIR y, al modelarse en fase mínima, no introduce retardo de transporte apreciable, lo que lo hace apto para audio sincronizado con vídeo. La función G se parametriza a partir de modelos HRTF (ITD, ILD promedio y espectro de ILD), obtenidos promediando cinco bases de datos públicas (HUTUBS, RIEC, BiLi, CIPIC y ARI), con un ajuste empírico monótono del espectro de ILD gobernado por un parámetro α. El resultado es una solución parametrizable —validada empíricamente durante años de uso doméstico continuado— que, mediante el ajuste de ITD, ILD, α y el azimut Θ, permite alcanzar en cada sistema el equilibrio óptimo entre efecto espacial y neutralidad tonal.*

## Introducción

Como ya se ha comentado, NatAmbio es un sistema formado por dos dipolos estéreo XTC, uno frontal y otro ambiental. Por lo tanto, incluye en su software la función XTC, en este caso aplicada mediante la convolución de unos filtros FIR generados a tal propósito. Los filtros que NatAmbio emplea de modo interno son generados mediante un algoritmo cuyo diseño matemático se presenta a continuación. Inicialmente se hace una breve presentación del problema, posteriormente se hace referencia a las más conocidas propuestas XTC publicadas hasta la fecha, y finalmente se desarrolla el modelo particular que emplea NatAmbio.

## Referencias técnicas previas

Uno de los problemas ocultos detrás de cualquier sistema multi-altavoz para generación de un campo sonoro espacial (desde el sistema estéreo hasta Dolby Atmos) es la aparición de diafonía (natural): la señal de cada altavoz es percibida a la vez, y de distinta manera, por ambos oídos. En el caso estéreo, bien conocido, el resultado es que la disposición espacial de las fuentes sonoras virtuales colapsa y se difumina, perdiendo buena parte de su potencial.

Que la distorsión provocada por la diafonía estéreo es relevante, y muy indeseable, es fácil de comprobar mediante el experimento físico de ubicar un gran panel aislante acústico entre ambos altavoces y extendido hasta el oyente, preferiblemente con los altavoces muy próximos entre sí. Es una disposición incómoda, imposible en un entorno doméstico, pero permite experimentar de modo muy simple cuál es el potencial del estéreo para generar una imagen sonora de muy gran amplitud.

Dado que la opción del aislamiento físico es en la práctica inviable, la investigación se ha centrado en la posibilidad de generar canceladores de diafonía (XTC) mediante técnicas DSP. Una de las mejores soluciones conocidas para mitigar la indeseable diafonía de modo subjetivamente apreciable es [ambiophonics](https://en.wikipedia.org/wiki/Ambiophonics). Ralph Glasgal, Robin Miller y Angelo Farina, sus promotores, desarrollaron un filtro DSP denominado [RACE (Recursive Ambiophonic Crosstalk Elimination)](https://filmaker.com/papers/RGRM-RACE_rev.pdf) que posibilita la aplicación de la funcionalidad XTC en equipos de sonido aficionado o incluso sistemas profesionales.

Además de técnicas IIR, como el caso del citado RACE, también existen implementaciones [FIR para producir XTC por convolución](https://www.researchgate.net/publication/228356916_Ambiophonic_Principles_for_the_Recording_and_Reproduction_of_Surround_Sound_for_Music). Asimismo, especialmente conocido es el desarrollo XTC de [Edgar Choueiri](https://3d3a.princeton.edu/sites/g/files/toruqf931/files/documents/BACCHPaperV4d_0.pdf), el cual se puede encontrar [comercialmente](https://www.bacch.com/ubacch). La gran mayoría de la literatura sobre XTC es bastante antigua, de hace más de 15 años, y desde entonces las novedades públicas en el estudio de las posibilidades de este tipo de filtrado digital prácticamente han desaparecido.

La literatura principal acerca de XTC aplicado mediante filtros FIR, como por ejemplo [Angelo Farina](https://www.angelofarina.it/Public/Papers/146-Sharc2000.PDF) y el ya citado Edgar Choueiri, tiene como objetivo invertir la matriz de las cuatro impulsivas entre altavoces y oídos. En este nuevo estudio, la implementación de filtros FIR XTC se basa en un algoritmo recursivo, a semejanza del RACE original, aunque este era un filtro IIR.

Una de las críticas más repetidas a los algoritmos XTC existentes es que mejoran de manera muy destacada la espacialidad sonora, pero a costa de una cierta coloración tonal. Teniendo esto en mente, el diseño final de los detalles de este algoritmo recursivo se enfoca en buscar el equilibrio entre minimizar hasta hacer desaparecer la coloración y ofrecer a su vez el máximo posible de espacialidad. El resultado es una solución parametrizable que permite generar filtros XTC aplicables en diferentes configuraciones y que permiten, mediante su ajuste, alcanzar el equilibrio óptimo de cada caso.

## Notación

| Símbolo | Significado |
|---|---|
| $X_l,\ X_r$ | Señales de entrada de los canales izquierdo y derecho |
| $H_{ll},\ H_{rr}$ | Caminos acústicos directos (altavoz → oído del mismo lado) |
| $H_{lr},\ H_{rl}$ | Caminos acústicos cruzados (altavoz → oído contrario) |
| $H_{direct},\ H_{cross}$ | Versiones simétricas de los caminos directo y cruzado |
| $G = H_{cross}/H_{direct}$ | Función de transferencia cruzada normalizada |
| $F^{direct},\ F^{cross}$ | Filtros FIR resultantes (camino directo y cruzado) |
| $\delta$ | Impulso unitario (elemento neutro de la convolución) |
| $\ast$ | Operador convolución |
| $\Theta$ | Azimut de incidencia (semiángulo entre altavoces; separación total $2\Theta$) |
| $\text{ITD}$ | Diferencia interaural de tiempo (*Interaural Time Difference*) |
| $\text{ILD}_{dB}$ | Diferencia interaural de nivel en dB (*Interaural Level Difference*) |
| $a = 10^{-\text{ILD}_{dB}/20}$ | Factor de atenuación lineal asociado al ILD |
| $\alpha$ | Exponente del modelo espectral de ILD |
| $N$ | Número de términos (iteraciones) del sumatorio |

## Análisis del problema y resolución

Comenzando por el esquema sonoro del sistema estéreo básico y su diafonía:

![Escena sonora estéreo](images/esquema_XTC_01.svg)

En este análisis se va a desarrollar un camino iterativo. Es importante tener en cuenta que la primera señal que va a generarse es la propia de cada canal ( $\delta \ast X_l = X_l$ ), manteniendo sin filtro alguno los caminos $H_{ll}$ y $H_{rr}$ para, de esta forma, mantener el sistema acústico real intacto en los caminos directos. El filtrado XTC va a ser un añadido a esta respuesta acústica natural del sistema.

Asimismo, en el desarrollo del modelo, no se va a considerar la función impulsiva real de los altavoces, idealizándola a $\delta = 1$, y se va a asumir que el entorno es anecoico, sin contribución de ningún tipo por parte del entorno.

La señal $X_l$, introducida por el canal izquierdo, llegará a cada oído como:

$$S_{l} = X_{l} \ast H_{ll}$$
$$S_{r} = X_{l} \ast H_{lr}$$

Siendo $\ast$ el operador convolución.

Para el canal izquierdo, la señal directa se entregará intacta. Se asume que $H_{ll}$ representa el camino acústico natural entre el altavoz y el oído izquierdos del oyente. Desde la perspectiva XTC no existe ninguna contribución indeseada en este camino y, por tanto, no es necesario modificarlo. Por lo tanto, en esta primera iteración solo hay que incorporar un filtro FIR ($F_{r1}$) de cancelación al canal derecho, que es aquel que recibe la diafonía cruzada.

Para determinar este filtro $F_{r1}$, se parte de la señal cruzada, la cual sería:

$$S_{r} = X_{l} \ast H_{lr} + X_{l} \ast H_{rr} \ast F_{r1} = X_{l} \ast \left ( H_{lr} + H_{rr} \ast F_{r1}\right )$$

Para que $S_{r}=0$, que es la condición de cancelación de diafonía, se debe cumplir que:

$$ H_{lr} + H_{rr} \ast F_{r1} = 0 $$
$$ F_{r1} = - H_{lr} \ast H_{rr}^{inv} = \frac {-H_{lr}} {H_{rr}}$$

Interpretando la notación de división como convolución por la inversa: por ejemplo, un filtro convolucionado por su inverso da lugar a una $\delta$:

$$ H_{rr} \ast H_{rr}^{inv} = \frac {H_{rr}} {H_{rr}} = \delta $$

Ahora es necesario generar adicionalmente en el canal izquierdo un filtro de cancelación de la diafonía provocada por la aplicación del primer filtro, $F_{r1}$, sobre el canal derecho:

$$ F_{l1} \ast H_{ll} + H_{rl} \ast \frac {-H_{lr}} {H_{rr}} = 0$$
$$ F_{l1}  = \frac { H_{rl} \ast H_{lr}} {H_{rr}} \ast H_{ll}^{inv} = \frac {H_{rl} \ast H_{lr} } {H_{rr} \ast H_{ll}} $$

Siempre entendiendo por división la convolución por las impulsivas inversas. Si se sigue el análisis de las sucesivas cancelaciones de diafonía, se obtienen cuatro filtros, dos para el canal cruzado (filtros para el canal derecho debidos a la señal del canal izquierdo, y viceversa) y otros dos para el canal directo (filtros para el canal izquierdo debidos a la señal del canal izquierdo, y viceversa).

$$ F_{r}^{cross} = \sum_{i=1}^{N} \frac {-H_{lr}^{i} \ast H_{rl}^{i-1}} {H_{rr}^{i} \ast H_{ll}^{i-1}}$$
$$ F_{l}^{cross} = \sum_{i=1}^{N} \frac {-H_{rl}^{i} \ast H_{lr}^{i-1}} {H_{ll}^{i} \ast H_{rr}^{i-1}}$$
$$ F_{r}^{direct} = \sum_{i=1}^{N} \frac {H_{rl}^{i} \ast H_{lr}^{i}} {H_{rr}^{i} \ast H_{ll}^{i}}$$
$$ F_{l}^{direct} = \sum_{i=1}^{N} \frac {H_{lr}^{i} \ast H_{rl}^{i}} {H_{ll}^{i} \ast H_{rr}^{i}}$$

Donde la potencia $i$ indica $i$ convoluciones sucesivas de un mismo filtro, y donde la función $H_{xy}$ aparecerá en el denominador cuando se está haciendo referencia a su inversa.

Si se asume, algo lógico en entornos simétricos (o próximos a la simetría), que $H_{lr} = H_{rl} = H_{cross}$ y que $H_{ll} = H_{rr} = H_{direct}$, las ecuaciones finalmente quedan como:

$$ F^{cross} = \sum_{i=1}^{N} \frac {-H_{cross}^{2i-1}} {H_{direct}^{2i-1}} $$
$$ F^{direct} = \delta + \sum_{i=1}^{N} \frac {H_{cross}^{2i}} {H_{direct}^{2i}} $$

Definimos ahora una función $G$ como la convolución de la impulsiva acústica cruzada por la inversa de la impulsiva acústica directa:

$$G = \frac {H_{cross}} {H_{direct}}$$

Esta relación puede interpretarse como la respuesta acústica cruzada normalizada respecto al camino directo, es decir, la cantidad de señal cruzada que aparece en el oído contrario por cada unidad de señal recibida por el oído correspondiente. Esta interpretación, como se desarrollará más adelante, abre la posibilidad de vincular G con los párametros principales del modelo HRTF.

En el caso de $F^{cross}$, $G$ convoluciona en contrafase, así como en fase para el caso de $F^{direct}$ y, además, para $F^{direct}$ el primer término es una $\delta$, indicando que la primera entrega del filtro es la propia señal de entrada intacta. Reescribiendo de modo más simplificado estas ecuaciones se obtiene:

$$ F^{cross} = \sum_{i=1}^{N}- G^{2i-1}  $$
$$ F^{direct} = \delta + \sum_{i=1}^{N} G ^{2i}  $$

Las series convergen siempre y cuando $|G| < 1$, condición que se cumple de forma natural en el modelo matemático: la señal cruzada presenta menor nivel que la señal directa debido a la sombra acústica generada por la cabeza del oyente. Definido en términos acústicos esto equivale a que la energía del camino cruzado sea inferior a la del camino directo.

Aunque, en sentido estricto, el número de términos del sumatorio debería ser infinito, dado que cada término decae como $|G|^{2i-1}$, en pocos pasos su contribución cae a niveles despreciables. A modo de referencia, con $|G|\approx 0.32$ (el valor medio del ejemplo de la última sección, $ILD_{dB}=10$ dB), cada incremento de $i$ reduce el término en unos 20 dB: el término $i=4$ se sitúa ya del orden de $-70$ dB, de modo que $N=3$–$4$ resulta suficiente en la práctica.

Conviene subrayar que, aunque el *proceso de diseño* es recursivo, el filtro finalmente realizado es **FIR** (no recursivo en ejecución): la recurrencia se resuelve y se trunca en tiempo de diseño, generando una respuesta impulsiva de longitud finita. Nótese también que $F^{direct}\neq\delta$: lo que permanece inalterado es el *camino acústico* directo $H_{direct}$, pero la señal entregada al altavoz del lado directo sí incorpora los términos de corrección $\sum G^{2i}$, necesarios para cancelar la diafonía que las propias emisiones cruzadas reintroducen en el oído directo.

Sin embargo, es importante tener en consideración algunos aspectos prácticos durante la implementación. A bajas frecuencias (aproximadamente por debajo de 200 Hz), la diferencia de nivel entre recepción directa y cruzada disminuye significativamente, haciendo que $|G|$ pueda aproximarse a la unidad. En estas condiciones resulta necesario proteger la estabilidad del sistema limitando la energía asociada a los términos recursivos de orden superior (los $G^{n}$ con $n$ creciente). Esto está motivado porque la convergencia matemática de los filtros XTC no garantiza por sí sola un comportamiento acústico óptimo. En sistemas reales, la interacción entre los filtros XTC, la respuesta modal de la sala y la respuesta propia de los altavoces puede producir refuerzos audibles en bajas frecuencias. Por este motivo, puede resultar conveniente introducir atenuaciones adicionales o limitaciones espectrales sobre la función $G$, independientemente de que la serie permanezca matemáticamente convergente.

## Desarrollo del diseño final

Una vez descritos los filtros en términos de convoluciones sucesivas de la función impulsiva $G$ desde los altavoces hasta los oídos del oyente, es necesario caracterizarla para poder implementarla mediante DSP.

Hay que recordar que, en el desarrollo analítico de estos filtros XTC, no se incluyen los efectos acústicos de la sala y de los altavoces, aspectos que corresponden a la ecualización. Por lo tanto, solo estamos considerando las funciones de transferencia a los oídos del oyente.

Llegados a este punto puede aproximarse la función $G$ mediante parámetros derivados de los modelos HRTF, principalmente ITD (*Interaural Time Difference*) e ILD (*Interaural Level Difference*), puesto que las funciones impulso $H$, aisladas de las características propias del altavoz y de la sala de escucha, son precisamente los modelos matemáticos HRTF.

Una primera aproximación, la más sencilla, es asumir que la función $G$ es independiente de la frecuencia y solo dependiente del azimut:

$$ G = \delta \left ( \text{ITD}, a\right ) = \delta \left ( \text{ITD} \left ( \Theta \right), ILD \left ( \Theta \right )\right ) $$

Básicamente, es una función delta retardada un tiempo igual al ITD y multiplicada por un factor de atenuación lineal $a$. Este factor se relaciona con la diferencia de nivel interaural expresada en dB mediante $a = 10^{-ILD_{dB}/20}$. El ángulo azimut $\Theta$ es el ángulo de incidencia desde los altavoces hasta el oyente (en un sistema estéreo, los altavoces forman entre sí un ángulo $2\times\Theta$).

Con esta notación, la condición de convergencia del algoritmo $|G| < 1$ equivale a que el camino cruzado esté siempre por debajo del directo, es decir, $a < 1$ o, de forma equivalente, $ILD_{dB} > 0$.

La siguiente aproximación es incluir el efecto de la frecuencia sobre el ILD. Para ello se puede descomponer el ILD en un factor de atenuación promedio y otro que es su espectro en frecuencia para el ángulo azimut $\Theta$ dado:

$$ G = \delta \left ( \text{ITD}, \text{ILD}\right ) = \delta \left ( \text{ITD} \left ( \Theta \right), \text{ILD}_{avg} \left ( \Theta \right ) \right ) \ast \text{ILD}_{spectrum} \left ( \Theta, f \right ) $$

La función G se obtendría por convolución de la impulsiva dependiente de $\text{ITD}$, e $ILD_{avg}$ y la impulsiva del espectro $ILD_{spectrum}(f)$

## Parametrización de XTC a partir de HRTF

Con objeto de parametrizar XTC (su función G), se han estudiado varias librerías públicas de medidas HRTF, obteniendo valores promediados para $ITD$, $ILD_{avg}$ e $ILD_{spectrum}(f)$. La lista de librerías es la siguiente:

1. The HUTUBS head-related transfer function (HRTF) database.
2. The RIEC HRTF Dataset.
3. BiLi dataset.
4. CIPIC database.
5. ARI database.

Todas ellas se pueden encontrar en este enlace: https://www.sofaconventions.org/mediawiki/index.php/Files

Analizando los cinco conjuntos citados de base de datos HRTF (tomando valores promediados de las diferentes medidas disponibles) se puede tener una razonable aproximación general a $ITD$ e $ILD_{avg}$ en función del ángulo azimut.
<br>
<div align="center"><img src="images/ITD_vs_azimuth.png"/></div>
<br>
<div align="center"><img src="images/ILD_vs_azimuth.png"/></div>
<br>

Por otro lado, para $ILD_{spectrum}(f)$ se estudiaron los promedios de los diferentes modelos HRTF para diferentes ángulos:

Azimut a 10°:
<br>
<div align="center"><img src="images/ILD_normalized_az10.png"/></div>
<br>
Azimut a 15°:
<br>
<div align="center"><img src="images/ILD_normalized_az15.png"/></div>
<br>
Azimut a 20°:
<br>
<div align="center"><img src="images/ILD_normalized_az20.png"/></div>
<br>
Azimut a 30°:
<br>
<div align="center"><img src="images/ILD_normalized_az30.png"/></div>
<br>

Buscando la menor coloración, hay que evitar cualquier pico o valle en la forma de $ILD_{spectrum}(f)$, puesto que la disposición de estos picos en el espectro es muy variable con la anatomía del oyente. Por lo tanto, se evitan los desarrollos individuales y se opta por aproximaciones de carácter general.

Se evaluó como punto de partida un modelo paramétrico de $ILD(\Theta,f)$ (Akeroyd et al., 2021, ajustado a los datos de Shaw y Vaillancourt, 1985), pero su repunte en altas frecuencias no se ajusta bien al promedio de los modelos HRTF públicos estudiados. Por ello se ha optado por desarrollar una ecuación de ajuste empírico, más sencilla y monótona, de la siguiente forma:

 $$ILD_{spectrum}(f) = \alpha \cdot 10 \cdot log_{10}(f/1000 + 1)  \cdot \sin(\Theta)  $$

Esta forma se ajustó a la media de las $ILD$ normalizadas en azimut de los conjuntos HRTF públicos estudiados (HUTUBS, RIEC, BiLi, CIPIC y ARI; véase Referencias). Como puede apreciarse en las gráficas de $ILD_{spectrum}(f)$, el ajuste emplea un parámetro $\alpha$ que tendrá un valor de entre 1.5 y 2.0.

Finalmente, queda una decisión de diseño por tomar: la fase de $G(f)$, que corresponde con la fase de $ILD_{spectrum}(f)$. A la hora de implementar este algoritmo en NatAmbio se ha optado por un modelo de $ILD_{spectrum}(\Theta,f)$ de fase mínima. De esta forma, el filtro no añade retardo de transporte (la energía se concentra al inicio de la respuesta impulsiva), por lo que el procesado XTC no introduce latencia apreciable más allá de la propia del motor de convolución, lo que lo hace apto para su utilización con audio simultáneo al vídeo.

## Ejemplo de filtros obtenidos por este nuevo algoritmo

El algoritmo ha sido desarrollado y evaluado en un entorno doméstico real durante varios años de uso continuado. Aunque todavía no se dispone de estudios formales con grupos de oyentes ni de medidas perceptuales sistemáticas, la experiencia práctica obtenida ha servido para orientar todas las decisiones de diseño.

Con la experiencia de este uso particular se ha ido ajustando la parametrización final por proceso de escucha y valoración empírica, resultando que la percepción subjetiva ha dado los mejores resultados en cuanto a equilibrio entre el efecto XTC y baja coloración tonal cuando:

- ITD se ha aproximado a valores típicos de los modelos HRTF promedio.
- ILD se ha fijado en valores por encima (mayores que) los que indican los modelos HRTF (en torno a 4 dB más). En este sentido, la escucha subjetiva de los filtros XTC con valores de ILD próximos a los del HRTF natural provoca la percepción de cierta coloración, en especial en agudos. Estas conclusiones, válidas para un caso particular, requieren pruebas adicionales y sistematizadas en diferentes sistemas de audio para poder generalizarse.

En este caso particular, los filtros aplicados finalmente al sistema han sido generados con unos parámetros ITD = 180 µs, $ILD_{dB}$ = 10 dB, $\alpha$ = 1.8 y $\Theta$ = 20°. Conviene señalar que, mientras ITD se corresponde con el valor HRTF natural a 20°, ILD, como ya se ha mencionado, ha quedado con un valor por encima del valor HRTF natural a ese azimut: un ILD mayor que el natural produce un filtro cruzado más suave, intercambiando algo de espacialidad por una menor coloración. Interpretado desde el punto de vista matemático, aumentar ILD reduce la magnitud de G y acelera la convergencia de la serie, reduciendo simultáneamente la energía de los términos correctores de orden superior, lo cual, a su vez, disminuye la magnitud del efecto "filtro peine" (como se mostrará a continuación), que es el responsable principal de la mencionada coloración tonal.

Una visualización de las impulsivas obtenidas y los espectros en frecuencia de los filtros XTC aplicados, obtenidos a una frecuencia de muestreo de 48 kHz y una longitud de los filtros de 4096 muestras, es la siguiente:

<br>
<div align="center"><img src="images/grafica_temporal.png"/></div>
<br>
<div align="center"><img src="images/grafica_espectral.png"/></div>
<br>

Se puede comprobar que el filtro Direct XTC se aproxima mucho a una $\delta$ y que su rizado como filtro "peine" está controlado en la banda de $\pm 2$ dB. En cuanto al filtro Cross XTC, su nivel está por debajo del de Direct XTC en un valor promedio de 10 dB. Asimismo, es visible que, al partir de un modelado de impulsivas en fase mínima, los filtros XTC obtenidos no introducen retardo de transporte apreciable a la señal procesada (el pico de Direct XTC se sitúa en $t=0$).

Como se ha comentado anteriormente, el filtro XTC cross está acotado a 200 Hz, para evitar que su efecto sea percibido no como XTC sino como un realce indeseado en graves. Para frecuencias por debajo de 200 Hz se atenúa el nivel con una rampa de bajada de 6 dB/octava.

Finalmente se obtienen dos filtros, uno para el camino directo y otro para el cruzado, que deberán aplicarse por convolución para obtener la deseada cancelación de diafonía. Por supuesto, NatAmbio incluye el mecanismo de generación automática de filtros XTC, igualmente parametrizables, y los aplica a través de su propio motor de convolución, de modo integrado con el resto de los pasos de su proceso DSP global.

Como resumen final, este nuevo algoritmo XTC puede interpretarse como una generalización FIR de los esquemas clásicos de cancelación recursiva tipo RACE, donde las relaciones acústicas entre altavoces y oídos quedan parametrizadas mediante modelos HRTF simplificados y ajustables.

## Referencias

**Diafonía / XTC**

1. Glasgal, R. & Miller, R. (Robin). *Recursive Ambiophonic Crosstalk Elimination (RACE)*. Ambiophonics Institute / Filmaker Technology. <https://filmaker.com/papers/RGRM-RACE_rev.pdf>
2. Farina, A. et al. *Ambiophonic Principles for the Recording and Reproduction of Surround Sound for Music*. <https://www.researchgate.net/publication/228356916_Ambiophonic_Principles_for_the_Recording_and_Reproduction_of_Surround_Sound_for_Music>
3. Farina, A. *Crosstalk cancellation filters (SHARC 2000)*. <https://www.angelofarina.it/Public/Papers/146-Sharc2000.PDF>
4. Choueiri, E. *Optimal Crosstalk Cancellation for Binaural Audio with Two Loudspeakers (BACCH)*. 3D3A Lab, Princeton University. <https://3d3a.princeton.edu/sites/g/files/toruqf931/files/documents/BACCHPaperV4d_0.pdf>

**Modelos de ITD**

5. Woodworth, R. S. (1938). *Experimental Psychology*. Henry Holt, New York. (Límite de alta frecuencia.)
6. Kuhn, G. F. (1977). Model for the interaural time differences in the azimuthal plane. *J. Acoust. Soc. Am.* 62(1), 157–167. <https://doi.org/10.1121/1.381498> (Límite de baja frecuencia.)
7. Aaronson, N. L. & Hartmann, W. M. (2014). Testing, correcting, and extending the Woodworth model for interaural time difference. *J. Acoust. Soc. Am.* 135(2), 817–823. <https://doi.org/10.1121/1.4861243> (Corrección y transición LF–HF.)

**Modelos de ILD**

8. Shaw, E. A. G. & Vaillancourt, M. M. (1985). Transformation of sound-pressure level from the free field to the eardrum presented in numerical form. *J. Acoust. Soc. Am.* 78(3), 1120–1123. <https://doi.org/10.1121/1.393035> (Datos base del ILD paramétrico.)
9. Akeroyd, M. A., Firth, J., Graetzer, S. & Smith, S. (2021). A set of equations for numerically calculating the interaural level difference in the horizontal plane. *JASA Express Letters* 1(4), 044402. <https://doi.org/10.1121/10.0004261> (Forma funcional paramétrica ILD($\theta$, $f$); ajustada a los datos de Shaw y Vaillancourt 1985.)

**Conjuntos de datos HRTF (base del ajuste empírico de ILD)**

10. Brinkmann, F., Dinakaran, M., Pelzer, R., Wohlgemuth, J. J., Seipel, F., Voss, D., Grosche, P. & Weinzierl, S. (2019). *The HUTUBS head-related transfer function (HRTF) database*. Technische Universität Berlin. <https://doi.org/10.14279/depositonce-8487>
11. Watanabe, K., Iwaya, Y., Suzuki, Y., Takane, S. & Sato, S. (2014). Dataset of head-related transfer functions measured with a circular loudspeaker array. *Acoustical Science and Technology* 35(3), 159–165. (Base de datos RIEC, Tohoku University.) <https://www.riec.tohoku.ac.jp/pub/hrtf/>
12. Carpentier, T., Bahu, H., Noisternig, M. & Warusfel, O. (2014). Measurement of a Head-Related Transfer Function Database with High Spatial Resolution. *7th Forum Acusticum*. (Base de datos BiLi, IRCAM.)
13. Algazi, V. R., Duda, R. O., Thompson, D. M. & Avendano, C. (2001). The CIPIC HRTF database. *Proc. 2001 IEEE Workshop on Applications of Signal Processing to Audio and Acoustics (WASPAA)*, 99–102. <https://doi.org/10.1109/ASPAA.2001.969552>
14. Majdak, P., Balazs, P. & Laback, B. (2007). Multiple exponential sweep method for fast measurement of head-related transfer functions. *J. Audio Eng. Soc.* 55(7/8), 623–637. (Método de medida de la base de datos ARI, Acoustics Research Institute, Austrian Academy of Sciences.)

