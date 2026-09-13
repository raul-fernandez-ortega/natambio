#!/usr/bin/env python3
"""Gráficas a posteriori de una tirada de nae_natambio_erb.py.

Lee el `_metrics.npz` que aquella escribió y dibuja sin volver a procesar
audio, que es lo que permite comparar tiradas de días distintos y volver sobre
una sin repetir media hora de cálculo.

Diferencia principal con las gráficas que genera el script de proceso: aquí el
eje de frecuencia es **logarítmico y real**, no índice de banda. Con el banco
ERB y su suelo, el índice reparte la altura a partes iguales entre bandas que
cubren una octava y bandas que cubren un tercio de octava, y el grave acaba
ocupando tanto sitio como de 6 a 18 kHz. En log, una octava mide siempre lo
mismo y uno se localiza.
"""
import argparse
import os
import sys

import numpy as np
import matplotlib
if not os.environ.get("DISPLAY"):
    matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.colors import LogNorm

FIG_SCALE = 2
FIGSIZE = (8 * FIG_SCALE, 4.5 * FIG_SCALE)

# Los mismos de nae_natambio_erb.py, para que las cifras se puedan cruzar.
DEGENERACY_THRESHOLDS = (1.2, 1.5, 2.0)


def wrap_axis(delta):
    """Un eje es módulo pi: envolver a (-90, 90] en radianes."""
    return np.angle(np.exp(2j * delta)) / 2.0


def band_edges(centers):
    """Bordes en frecuencia para pintar cada banda con su altura real.

    Las bandas se solapan —suman uno en cada bin— así que no tienen bordes
    propios. Se usan las medias GEOMÉTRICAS entre centros contiguos, que en un
    eje logarítmico es el punto medio visual, y los dos extremos se extrapolan
    con la misma razón que su vecino. Es una convención de dibujo y no una
    afirmación sobre el banco.
    """
    c = np.asarray(centers, dtype=float)
    if c.size == 1:
        return np.array([c[0] * 0.7, c[0] * 1.4])
    mid = np.sqrt(c[:-1] * c[1:])
    first = c[0] * c[0] / mid[0]
    last = c[-1] * c[-1] / mid[-1]
    return np.concatenate(([first], mid, [last]))


def pan_ticks():
    """Las marcas del eje de color, en lo que significan.

    Con side_weight = 1 (modo alpha) es tan(theta) = (L-R)/(L+R), así que TODO
    el recorrido de panorama cabe en +-45 grados y lo de más allá ya no es
    panorama: es |S| > |M|, contenido más en antifase que en suma.
    """
    vals = [-90, -45, -39.3, -18.4, 0, 18.4, 39.3, 45, 90]
    txt = ["-90  antifase", "-45  extremo D", "-39  -20 dB", "-18  -6 dB",
           "0  centro", "+18  +6 dB", "+39  +20 dB", "+45  extremo I", "+90  antifase"]
    return vals, txt


def eje_frecuencia(ax, f, fmin=None, fmax=None, etiqueta=True):
    """El eje Y en frecuencia real y logarítmica, con rejilla de audio.

    Con un banco AFLOORADO las bandas graves son anchas en escala log -- dos
    bandas pueden cubrir de 7 a 200 Hz, un tercio del dibujo. Es cierto y es
    informativo, dice que ahí no hay resolución; pero estorba para mirar el
    resto, y por eso se puede recortar.
    """
    ax.set_yscale("log")
    lo = fmin if fmin else f[0]
    hi = fmax if fmax else f[-1]
    ax.set_ylim(lo, hi)
    ticks = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]
    ticks = [x for x in ticks if lo <= x <= hi]
    ax.set_yticks(ticks)
    ax.set_yticklabels([("%dk" % (x // 1000)) if x >= 1000 else str(x) for x in ticks])
    if etiqueta:
        ax.set_ylabel("Frecuencia (Hz)")
    else:
        # tick_params y no set_yticklabels([]): con sharey, vaciar las etiquetas
        # de un eje se las quita también al que comparte con él.
        ax.tick_params(labelleft=False)
    ax.grid(True, which="major", axis="y", color="white", alpha=0.25, linewidth=0.6)


def destino(path, out_dir, suf, fmin, fmax, f):
    def corto(hz):
        hz = float(hz)
        return ("%gk" % round(hz / 1000.0, 1)) if hz >= 1000 else "%d" % round(hz)
    base = os.path.basename(path).replace("_metrics.npz", "")
    if fmin or fmax:
        suf += "_%s-%s" % (corto(fmin or f[0]), corto(fmax or f[-1]))
    return os.path.join(out_dir or os.path.dirname(path) or ".", base + suf + ".png")


def load(path):
    d = np.load(path)
    need = ("theta", "centers", "frame_size", "samplerate")
    missing = [k for k in need if k not in d.files]
    if missing:
        raise SystemExit("%s: le faltan %s -- ¿es de una versión anterior del script?"
                         % (os.path.basename(path), ", ".join(missing)))
    return d


def etiqueta(path, d):
    base = os.path.basename(path).replace("_metrics.npz", "")
    trozos = [base]
    for k, fmt in (("alpha", "alpha=%g"), ("delta_erb", "ΔERB=%g"),
                   ("b_min", "B_min=%.1f Hz")):
        if k in d.files:
            trozos.append(fmt % float(d[k]))
    if "theta" in d.files:
        trozos.append("%d bandas" % d["theta"].shape[0])
    return trozos[0] + "\n" + ", ".join(trozos[1:])


def theta_map(path, d, out_dir=None, fmin=None, fmax=None):
    th = d["theta"].astype(np.float64)
    centers = d["centers"]
    sr = int(d["samplerate"])
    fs = int(d["frame_size"])
    n_bands, n_win = th.shape

    t = np.arange(n_win + 1) * fs / float(sr)
    f = band_edges(centers)

    fig, ax = plt.subplots(figsize=FIGSIZE)
    mesh = ax.pcolormesh(t, f, np.degrees(wrap_axis(th)), cmap="twilight",
                         vmin=-90, vmax=90, shading="flat")
    eje_frecuencia(ax, f, fmin, fmax)
    ax.set_xlabel("Tiempo (s)")

    cbar = fig.colorbar(mesh, ax=ax)
    vals, txt = pan_ticks()
    cbar.set_ticks(vals)
    cbar.set_ticklabels(txt)
    cbar.set_label("theta: ángulo del eje principal en el plano (M, S)")

    ax.set_title("Ángulo del eje principal por banda — eje de frecuencia real\n"
                 + etiqueta(path, d))
    fig.tight_layout()

    dest = destino(path, out_dir, "_theta_map_log", fmin, fmax, f)
    fig.savefig(dest, dpi=110)
    plt.close(fig)
    return dest


def degeneracy_map(path, d, out_dir=None, fmin=None, fmax=None):
    """Dónde y cuándo el eje NO está definido por los datos.

    q = lambda1/lambda2. Con q grande hay una dirección dominante y la
    descomposición separa algo real; con q cerca de 1 los dos autovalores son
    iguales, el eje queda indeterminado y puede saltar de una ventana a la
    siguiente sin que haya pasado nada en la música.

    El original resume esto en un porcentaje por banda, que dice DÓNDE pero no
    CUÁNDO. Aquí va el mapa completo y el resumen a su lado, compartiendo el eje
    de frecuencia: el mapa para leerlo junto al de theta -- este es el que dice
    de qué partes de aquel hay que fiarse -- y la marginal para la cifra.
    """
    if "q" not in d.files:
        raise SystemExit("%s: no lleva q" % os.path.basename(path))
    q = d["q"].astype(np.float64)
    sr, fs = int(d["samplerate"]), int(d["frame_size"])
    t = np.arange(q.shape[1] + 1) * fs / float(sr)
    f = band_edges(d["centers"])

    fig, (ax, axm) = plt.subplots(
        1, 2, figsize=FIGSIZE, sharey=True,
        gridspec_kw={"width_ratios": [4, 1], "wspace": 0.04})

    mesh = ax.pcolormesh(t, f, np.maximum(q, 1.0), cmap="viridis",
                         norm=LogNorm(vmin=1.0, vmax=100.0), shading="flat")
    eje_frecuencia(ax, f, fmin, fmax)
    ax.set_xlabel("Tiempo (s)")
    ax.set_title("q = λ1/λ2 por banda: dónde hay un eje que encontrar")

    for th in DEGENERACY_THRESHOLDS:
        frac = np.mean(q < th, axis=1) * 100.0
        centros = np.asarray(d["centers"], dtype=float)
        axm.step(frac, centros, where="mid", linewidth=1.2, label="q < %g" % th)
    eje_frecuencia(axm, f, fmin, fmax, etiqueta=False)
    axm.set_xlim(0, 100)
    axm.set_xlabel("% del tiempo")
    axm.grid(True, which="major", axis="x", alpha=0.3, linewidth=0.6)
    axm.legend(loc="lower right", fontsize="small")
    axm.set_title("degeneradas")

    cbar = fig.colorbar(mesh, ax=axm, pad=0.16)
    cbar.set_ticks([1, 1.2, 1.5, 2, 5, 14, 100])
    cbar.set_ticklabels(["1  indefinido", "1,2", "1,5", "2  apenas",
                         "5", "14  claro", "100"])
    cbar.set_label("λ1/λ2")

    fig.suptitle(etiqueta(path, d))
    dest = destino(path, out_dir, "_degeneracy_log", fmin, fmax, f)
    fig.savefig(dest, dpi=110, bbox_inches="tight")
    plt.close(fig)
    return dest


def covarianza(d):
    """Smm, Sss y Sms por banda y ventana, reconstruidas del npz.

    El npz guarda el eje y los autovalores, no la covarianza; pero
    R = l1 v1 v1^T + l2 v2 v2^T la reconstruye entera, y de ahí salen las dos
    cifras que describen la escena: cuánto lado hay frente a centro, y si están
    correlacionados.
    """
    th = d["theta"].astype(np.float64)
    ev = d["eigenvalues"].astype(np.float64)
    l1, l2 = ev[..., 0], ev[..., 1]
    c, s = np.cos(th), np.sin(th)
    return (l1 * c * c + l2 * s * s,      # Smm
            l1 * s * s + l2 * c * c,      # Sss
            (l1 - l2) * c * s)            # Sms


def nombre_corto(path):
    return os.path.basename(path).split("_erb_")[0]


def firma(paths, out_dir=None, fmin=None, fmax=None, dest=None):
    """La firma de producción: cuánto lado hay, banda a banda.

    De todo lo que se ha mirado, este es el descriptor que separa unas
    grabaciones de otras de una forma que además se explica sola. Arriba, la
    relación lado/centro típica; abajo, con qué frecuencia se pasa de la raya.
    Son dos preguntas distintas -- cómo de ancho suele ser, y cuántas veces se
    va -- y una grabación puede tener una alta y la otra baja.

    Se dibujan varias a la vez a propósito: la firma sólo dice algo comparada.
    """
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(8 * FIG_SCALE, 6 * FIG_SCALE),
                                   sharex=True,
                                   gridspec_kw={"hspace": 0.08})
    lo_all, hi_all = [], []
    for path in paths:
        d = load(path)
        Smm, Sss, _ = covarianza(d)
        rat = Sss / np.maximum(Smm, 1e-300)
        ct = np.asarray(d["centers"], dtype=float)
        lo_all.append(ct[0]); hi_all.append(ct[-1])
        nom = nombre_corto(path)
        ax1.plot(ct, np.median(rat, axis=1), linewidth=1.6, marker="o",
                 markersize=3, label=nom)
        ax2.plot(ct, 100.0 * np.mean(rat > 1.0, axis=1), linewidth=1.6,
                 marker="o", markersize=3, label=nom)

    for ax in (ax1, ax2):
        ax.set_xscale("log")
        ax.set_xlim(fmin if fmin else min(lo_all), fmax if fmax else max(hi_all))
        ticks = [20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000]
        ticks = [x for x in ticks if ax.get_xlim()[0] <= x <= ax.get_xlim()[1]]
        ax.set_xticks(ticks)
        ax.set_xticklabels([("%dk" % (x // 1000)) if x >= 1000 else str(x) for x in ticks])
        ax.grid(True, which="major", alpha=0.3, linewidth=0.6)

    ax1.set_yscale("log")
    ax1.axhline(1.0, color="black", linestyle="dashed", linewidth=1.2)
    ax1.text(ax1.get_xlim()[0] * 1.05, 1.06, "tanto lado como centro",
             fontsize="small", va="bottom")
    ax1.set_ylabel("$S_{ss}/S_{mm}$ mediana")
    ax1.set_title("Firma de producción: cuánto lado hay frente a centro, banda a banda")
    ax1.legend(loc="upper left", fontsize="small")

    ax2.set_ylabel("% del tiempo con $S_{ss} > S_{mm}$")
    ax2.set_xlabel("Frecuencia (Hz)")
    ax2.set_ylim(0, None)

    if dest is None:
        dest = os.path.join(out_dir or os.path.dirname(paths[0]) or ".",
                            "firma_produccion.png")
    fig.savefig(dest, dpi=110, bbox_inches="tight")
    plt.close(fig)
    return dest


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("npz", nargs="+", help="ficheros _metrics.npz")
    p.add_argument("--out-dir", default=None, help="dónde escribir los PNG")
    p.add_argument("--signature", action="store_true",
                   help="una sola figura comparando la firma de producción de "
                        "todos los ficheros dados, en vez de un mapa por fichero")
    p.add_argument("--only", default=None,
                   help="lista separada por comas: theta, q. Por defecto todas")
    p.add_argument("--fmin", type=float, default=None,
                   help="recortar el eje de frecuencia por abajo (Hz). Útil con "
                        "suelos altos, donde dos bandas cubren todo el grave y "
                        "se comen un tercio del dibujo")
    p.add_argument("--fmax", type=float, default=None,
                   help="recortar el eje de frecuencia por arriba (Hz)")
    args = p.parse_args()

    if args.signature:
        print("  %s" % firma(args.npz, args.out_dir, args.fmin, args.fmax))
        return

    for path in args.npz:
        d = load(path)
        for nombre, fn in (("theta", theta_map), ("q", degeneracy_map)):
            if args.only and nombre not in args.only:
                continue
            print("  %s" % fn(path, d, args.out_dir, args.fmin, args.fmax))


if __name__ == "__main__":
    main()
