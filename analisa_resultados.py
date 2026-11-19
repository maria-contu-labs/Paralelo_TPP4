#!/usr/bin/env python3
"""Resume resultados reais em CSV; nao cria tempos ficticios."""
import csv
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def analisar(arquivos):
    grupo = defaultdict(list)
    consistencias = set()
    entrada = set()
    for arquivo in arquivos:
        with open(arquivo, newline='', encoding='utf-8') as f:
            for r in csv.DictReader(f):
                grupo[(r['modo'], int(r['processos_mpi']), int(r['threads_por_processo']))].append(float(r['tempo_s']))
                consistencias.add((r['passos_executados'], r['arvores_restantes'],
                                   r['celulas_queimadas'], r['chamas_restantes'],
                                   r['assinatura'], r['parada']))
                entrada.add((r['largura'], r['altura'], r['passos_maximos'], r['densidade'], r['semente']))
    if not grupo:
        raise SystemExit('Nenhuma linha encontrada nos CSVs.')
    if len(consistencias) != 1 or len(entrada) != 1:
        raise SystemExit('ERRO: entradas ou resultados diferentes: nao comparar estes casos.')
    labels = [("mpi",32,1), ("hibrido",4,8), ("mpi",64,1), ("hibrido",4,16)]
    nomes = {labels[0]: 'MPI 32', labels[1]: 'Hibrido 4x8', labels[2]: 'MPI 64 (HT)', labels[3]: 'Hibrido 4x16 (HT)'}
    media = {}
    print('Configuracao              N  Media(s)   Desvio(s)')
    for key in labels:
        valores = grupo.get(key, [])
        if not valores:
            continue
        m = statistics.mean(valores)
        desvio = statistics.stdev(valores) if len(valores) > 1 else 0.0
        media[key] = m
        print(f'{nomes[key]:<25} {len(valores):2d} {m:9.4f}  {desvio:9.4f}')
    for puro, hibrido in ((labels[0], labels[1]), (labels[2], labels[3])):
        if puro in media and hibrido in media:
            print(f'Razao {nomes[puro]} / {nomes[hibrido]} = {media[puro]/media[hibrido]:.3f}x')
    print('Consistencia do resultado final: OK |', next(iter(consistencias)))
    tex = Path('resultados/tabela_resultados.tex')
    tex.parent.mkdir(parents=True, exist_ok=True)
    with tex.open('w', encoding='utf-8') as f:
        for key in labels:
            if key not in media:
                continue
            valores = grupo[key]
            desv = statistics.stdev(valores) if len(valores) > 1 else 0.0
            nome = nomes[key].replace('4x8', r'4$\\times$8').replace('4x16', r'4$\\times$16')
            f.write(f'{nome} & {statistics.mean(valores):.3f} & {desv:.3f} & {len(valores)} \\\\\n')
        # TeX speedup textual pode ser referenciado manualmente no relatorio.
    print('Tabela LaTeX exportada:', tex)
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
        keys = [key for key in labels if key in media]
        if keys:
            fig, ax = plt.subplots(figsize=(6.5, 3.2), dpi=160)
            avg = [media[k] for k in keys]
            dp = [statistics.stdev(grupo[k]) if len(grupo[k]) > 1 else 0 for k in keys]
            ax.bar(range(len(keys)), avg, yerr=dp, capsize=4)
            ax.set_xticks(range(len(keys)), [nomes[k] for k in keys], rotation=12, ha='right')
            ax.set_ylabel('Tempo medio (s)')
            ax.set_title('TPP4 - Tempo total por configuracao')
            ax.grid(axis='y', alpha=0.22)
            fig.tight_layout()
            pasta = Path('figuras')
            pasta.mkdir(parents=True, exist_ok=True)
            grafico = pasta / 'comparativo_tempos.png'
            fig.savefig(grafico, bbox_inches='tight')
            plt.close(fig)
            print('Grafico de tempos reais exportado:', grafico)
    except ImportError:
        print('Matplotlib nao disponivel: tabela pronta, grafico opcional nao gerado.')


if __name__ == '__main__':
    if len(sys.argv) < 2:
        raise SystemExit('Uso: python3 analisa_resultados.py resultados/*.csv')
    analisar(sys.argv[1:])
