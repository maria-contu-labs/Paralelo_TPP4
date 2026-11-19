# TPP4 — Simulação de incêndio florestal (MPI / MPI + OpenMP)

**Disciplina:** Computação Paralela, PUCRS, 2026/2, Prof. César de Rose.
**Tema:** simulação baseada em autômato celular bidimensional, não ordenação de vetores.

## Conteúdo da entrega

- `incendio.cpp`: programa C++17 comum às duas versões.
- `Makefile`: compila `tpp4_mpi` e `tpp4_hibrido`.
- `testes_atlantica.sbatch`: job Slurm para 4 nós, 4 configurações, 3 repetições, conferência de assinatura e CSV.
- `analisa_resultados.py`: média e desvio-padrão e razões de tempo; gera tabela LaTeX somente depois de ler CSVs verdadeiros.
- `relatorio_tpp4.tex`: uma página de relatório + código integral numerado em apêndice; **texto de resultados ainda pendente**.
- `figuras/comparacao_simulacao.png`: comparação visual demonstrativa da inicialização e do estado após 140 passos em teste local.
- `testes/validacao.py`: compara C++ com referência em Python e com partições virtuais.
- `testes/mpi_stub.hpp`: implementação MPI mínima de **um único processo**, usada exclusivamente nos testes locais sem MPI.

## Modelo de fases paralelas

Cada rank armazena uma faixa horizontal da floresta e duas linhas extras de vizinhos (halos). Os estados são:

- 0 = solo / sem vegetação; 1 = árvore; 2 = chama; 3 = queimada.

Para cada passo:

1. **Computação local:** cada célula é atualizada a partir da grade do passo anterior (8 vizinhos); células em chamas se tornam queimadas; uma árvore pode acender conforme os vizinhos e o vento leste.
2. **Verificação coletiva:** `MPI_Allreduce(SUM)` combina o número de células ainda em chamas. Se é zero, encerra por extinção; se o máximo de passos foi atingido, encerra por limite de custo.
3. **Troca de fronteira:** se não encerrou, `MPI_Sendrecv` compartilha as linhas superior e inferior dos blocos (para o próximo passo). Uma troca inicial prepara a primeira iteração.

A floresta é inicializada independentemente nos processos, usando hash de coordenadas, semente e passo (sem dependência da quantidade de ranks ou threads). Um foco 5x5 central inicia a propagação. O vento leste eleva a influência de uma chama localizada a oeste. O modelo é **uma simulação simplificada**, não predição física de incêndios reais.

**MPI puro** executa o laço local sequencial em cada rank. **MPI+OpenMP** reparte as linhas locais com `parallel for`, `reduction` e `schedule(static)`; chamadas MPI são feitas apenas pela thread principal (`MPI_THREAD_FUNNELED`). Mesma regra, mesma entrada e mesma interface CSV. A verificação `MPI_Allreduce` é uma forma mais direta de realizar a redução global que os vários `Bcast` do pseudocódigo didático.

## Compilar e testar

Na máquina com MPI e OpenMP disponíveis:

```bash
make
mpirun -np 2 ./tpp4_mpi --largura 256 --altura 256 --passos 100
OMP_NUM_THREADS=2 mpirun -np 2 ./tpp4_hibrido --largura 256 --altura 256 --passos 100
```

Se `mpirun` não estiver disponível localmente, faça os testes MPI com `srun` na Atlântica. O código híbrido exige suporte a OpenMP no compilador.

Teste de lógica sem MPI real (requer `g++`, Python 3 e suporte a OpenMP):

```bash
make testar
```

A validação compara a simulação C++ com uma implementação independente em Python, decomposições virtuais 1/2/4/8/16/32/64 e OpenMP local com 1/2/4 threads. **Isso não é uma execução MPI distribuída**.

## Executar na Atlântica

Primeiro confirme os recursos com `sinfo` e `scontrol show node` e carregue os módulos MPI exigidos pelo cluster (`module avail`, se aplicável). O script foi preparado com base no contexto de **4 nós de 8 núcleos físicos / 16 CPUs lógicas por nó**, e pode precisar de pequenos ajustes de partição ou política local do Slurm.

```bash
make
sbatch testes_atlantica.sbatch
squeue -u "$USER"
```

Alternativa com parâmetros:

```bash
sbatch --export=ALL,GRID=2048,STEPS=3000,REPS=3,DENSITY=88,SEED=2026 testes_atlantica.sbatch
```

| Comparação | MPI puro | Híbrido | Trabalhadores usados |
|---|---|---|---:|
| Sem HT | 32 ranks × 1 thread | 4 ranks × 8 threads | 32 |
| Com HT | 64 ranks × 1 thread | 4 ranks × 16 threads | 64 |

Os quatro cenários usam os 4 nós, a mesma grade, a mesma semente e o mesmo limite de passos. A ordem dos cenários é invertida nas repetições pares. O script **interrompe** se a assinatura final, as contagens ou o número de passos divergirem entre os cenários. O arquivo `resultados/incendio_...csv` armazena os tempos reais.

**Medição**: `tempo_s` é o máximo entre os ranks, incluindo uma troca inicial, as atualizações locais, as verificações coletivas e as trocas de halos; exclui inicialização e criação de imagens. `calculo_s`, `coletiva_s`, `fronteiras_s` são os máximos por fase e, por virem de ranks diferentes, podem não somar exatamente `tempo_s`.

**Atenção:** os 3000 passos máximos são um limite para garantir uma carga de trabalho finita e repetível, não uma garantia de extinção. Sempre registrar se `parada=limite` ou `parada=extinto`. A assinatura verifica consistência entre implementações, não a fidelidade física do modelo.

## Analisar resultados e gerar relatório

Depois de o job terminar, na pasta do projeto:

```bash
python3 analisa_resultados.py resultados/incendio_*.csv
pdflatex -interaction=nonstopmode relatorio_tpp4.tex
pdflatex -interaction=nonstopmode relatorio_tpp4.tex
```

O programa gera `resultados/tabela_resultados.tex` com média, DP e n por configuração. Se houver Matplotlib, também cria `figuras/comparativo_tempos.png`, inserido automaticamente no relatório. O arquivo `relatorio_tpp4.tex` já a inclui automaticamente; complete os parágrafos de discussão e conclusão com as medições observadas. **Não entregue o modelo com campos em branco.** O código integral aparece em apêndice, com linhas numeradas, e a parte textual ocupa uma página.

## Visualizar a simulação (opcional, fora do benchmark)

```bash
mpirun -np 1 ./tpp4_mpi --largura 512 --altura 512 --passos 140 --imagem figuras/visual
```

Isso cria dois arquivos PPM (`_inicio.ppm` e `_fim.ppm`); programas como Preview/Imagemagick/GIMP os abrem. A geração de imagens está fora da janela de cronometragem e não é utilizada no script de desempenho.

## Limitações e cuidados

- O primeiro teste de MPI real deve confirmar `MPI_Sendrecv` entre ranks e `MPI_THREAD_FUNNELED` na Atlântica; até agora só foi possível validar localmente sem MPI real.
- Fixar a carga, não comparar tamanhos diferentes e relatar os tempos reais com média e DP.
- O Hyper-Threading não equivale a dobrar núcleos físicos, portanto ganhos não são garantidos.
- A rubrica completa não foi fornecida: conferir se exige gráficos, escalabilidade ou artefatos extras.
- Declarar o uso de ferramentas de IA conforme a política da disciplina.
