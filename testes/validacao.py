#!/usr/bin/env python3
"""Verificacao independente do modelo: referencia global x particoes virtuais.
Nao substitui testes MPI reais na Atlantica.
"""
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MASK = (1 << 64) - 1
C1 = 0x9E3779B97F4A7C15
C2 = 0xBF58476D1CE4E5B9
C3 = 0x94D049BB133111EB


def mix(v):
    v = (v + C1) & MASK
    v = ((v ^ (v >> 30)) * C2) & MASK
    v = ((v ^ (v >> 27)) * C3) & MASK
    return v ^ (v >> 31)


def aleatorio(seed, x, y, step):
    return mix((seed ^ ((x + 1) * C1) ^ ((y + 1) * C2) ^ ((step + 1) * C3)) & MASK)


def inicial(w, h, densidade, seed):
    return [2 if abs(x - w // 2) <= 2 and abs(y - h // 2) <= 2
            else 1 if aleatorio(seed, x, y, 0) % 100 < densidade else 0
            for y in range(h) for x in range(w)]


def atualizar(s, w, h, seed, passo, divisoes):
    linhas = h // divisoes
    out = [0] * (w * h)
    for rank in range(divisoes):
        inicio = rank * linhas
        # A referencia simula a leitura das halos no inicio de cada iteracao.
        cima = s[(inicio - 1) * w: inicio * w] if rank > 0 else [0] * w
        baixo = s[(inicio + linhas) * w: (inicio + linhas + 1) * w] if rank + 1 < divisoes else [0] * w
        for y in range(inicio, inicio + linhas):
            for x in range(w):
                idx = y * w + x
                estado = s[idx]
                novo = 3 if estado == 2 else estado
                if estado == 1:
                    influencia = 0
                    for dy in (-1, 0, 1):
                        ly = y - inicio + dy
                        vizinhos = cima if ly == -1 else baixo if ly == linhas else s[(y+dy)*w:(y+dy+1)*w]
                        for dx in (-1, 0, 1):
                            nx = x + dx
                            if (dx == dy == 0) or nx < 0 or nx >= w:
                                continue
                            if vizinhos[nx] == 2:
                                influencia += 3 if dx == -1 else 1
                    if influencia and aleatorio(seed, x, y, passo) % 100 < min(95, 20 + 12 * influencia):
                        novo = 2
                out[idx] = novo
    return out


def assinatura(s):
    return sum((i + 1) * c for i, c in enumerate(s)) & MASK


def verificar():
    w, h, seed, density, limit = 64, 64, 2026, 88, 45
    initial = inicial(w, h, density, seed)
    ref = initial[:]
    fim = 0
    for step in range(1, limit + 1):
        ref = atualizar(ref, w, h, seed, step, 1)
        fim = step
        if 2 not in ref:
            break
    esperado = (fim, assinatura(ref), ref.count(1), ref.count(3), ref.count(2))
    # Deteccao de erro na decomposicao: comparacao passo a passo para todos np.
    for np in (2, 4, 8, 16, 32, 64):
        x = initial[:]
        y = initial[:]
        for step in range(1, limit + 1):
            x = atualizar(x, w, h, seed, step, np)
            y = atualizar(y, w, h, seed, step, 1)
            assert x == y, f"Particao virtual {np} divergente no passo {step}"
            if 2 not in y:
                break
    for executable, thread_counts in (("teste_sequencial", (1,)), ("teste_openmp", (1, 2, 4))):
        for threads in thread_counts:
            cmd = [str(ROOT / "testes" / executable), "--largura", str(w),
                   "--altura", str(h), "--passos", str(limit),
                   "--densidade", str(density), "--semente", str(seed)]
            output = subprocess.check_output(cmd, env={"OMP_NUM_THREADS": str(threads)}, text=True).strip()
            items = output.split(",")
            observado = (int(items[8]), int(items[16]), int(items[13]), int(items[14]), int(items[15]))
            assert observado == esperado, f"C++ e Python divergentes: {executable}, threads={threads}: {observado} vs {esperado}"
    # Caso com extincao completa: todos os pontos iniciais queimam no primeiro passo.
    output = subprocess.check_output([str(ROOT / "testes" / "teste_sequencial"),
                                      "--largura", "64", "--altura", "64",
                                      "--densidade", "0", "--passos", "50"], text=True).strip().split(",")
    assert output[-1] == "extinto" and int(output[8]) == 1 and int(output[15]) == 0
    print("OK: modelo C++ x referencia Python; 1/2/4/8/16/32/64 particoes virtuais; OpenMP 1/2/4 threads; extincao.")


if __name__ == "__main__":
    verificar()
