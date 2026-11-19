#!/usr/bin/env bash
#SBATCH --job-name=tpp4_incendio
#SBATCH --nodes=4
#SBATCH --ntasks-per-node=16
#SBATCH --exclusive
#SBATCH --time=01:00:00
#SBATCH --output=tpp4_%j.out
#SBATCH --error=tpp4_%j.err

set -euo pipefail
cd "${SLURM_SUBMIT_DIR:-$PWD}"

# sbatch --export=ALL,GRID=2048,STEPS=3000,REPS=3 testes_atlantica.sbatch
GRID="${GRID:-2048}"
STEPS="${STEPS:-3000}"
REPS="${REPS:-3}"
DENSITY="${DENSITY:-88}"
SEED="${SEED:-2026}"
mkdir -p resultados
CSV="resultados/incendio_${GRID}x${GRID}_${STEPS}passos_${SLURM_JOB_ID:-manual}.csv"
HEAD='repeticao,modo,processos_mpi,threads_por_processo,largura,altura,passos_maximos,densidade,semente,passos_executados,tempo_s,calculo_s,coletiva_s,fronteiras_s,arvores_restantes,celulas_queimadas,chamas_restantes,assinatura,parada'
echo "$HEAD" > "$CSV"

printf 'Alocacao: %s | nos: %s | grade: %sx%s | passos: %s | repeticoes: %s\n' \
       "${SLURM_JOB_ID:-manual}" "${SLURM_JOB_NODELIST:-?}" "$GRID" "$GRID" "$STEPS" "$REPS"
make all

referencia='' # Assinatura + iteracoes + contagens de celulas da 1a execucao.
executar() {
    local rep="$1" bin="$2" np="$3" por_no="$4" threads="$5" hint="$6" places="$7"
    local line modo got_np got_threads largura altura max_passos densidade semente passos tempo calculo coletiva fronteiras arvores cinzas chamas assinatura parada atual
    echo "Executando rep=$rep bin=$bin np=$np ranks_no=$por_no threads=$threads hint=$hint" >&2
    line="$(
        OMP_NUM_THREADS="$threads" OMP_DYNAMIC=FALSE OMP_PLACES="$places" OMP_PROC_BIND=spread \
        srun --exact --nodes=4 --ntasks="$np" --ntasks-per-node="$por_no" \
             --cpus-per-task="$threads" --hint="$hint" \
             "./$bin" --largura "$GRID" --altura "$GRID" --passos "$STEPS" \
             --densidade "$DENSITY" --semente "$SEED"
    )"
    IFS=',' read -r modo got_np got_threads largura altura max_passos densidade semente passos tempo calculo coletiva fronteiras arvores cinzas chamas assinatura parada <<< "$line"
    if [[ "$got_np" != "$np" || "$got_threads" != "$threads" || -z "$assinatura" || -z "$parada" ]]; then
        echo "CSV invalido ou recursos incorretos: $line" >&2
        exit 1
    fi
    atual="$passos:$arvores:$cinzas:$chamas:$assinatura:$parada"
    if [[ -n "$referencia" && "$atual" != "$referencia" ]]; then
        echo "ERRO! Variantes produziram estados diferentes: $atual (esperado $referencia)" >&2
        exit 1
    fi
    referencia="$atual"
    printf '%s,%s\n' "$rep" "$line" | tee -a "$CSV"
}

for ((rep=1; rep<=REPS; rep++)); do
    # Alterna a ordem dos pares para mitigar efeito de aquecimento.
    if ((rep % 2 == 1)); then
        executar "$rep" tpp4_mpi      32  8  1 nomultithread cores
        executar "$rep" tpp4_hibrido   4  1  8 nomultithread cores
        executar "$rep" tpp4_mpi      64 16  1 multithread threads
        executar "$rep" tpp4_hibrido   4  1 16 multithread threads
    else
        executar "$rep" tpp4_hibrido   4  1 16 multithread threads
        executar "$rep" tpp4_mpi      64 16  1 multithread threads
        executar "$rep" tpp4_hibrido   4  1  8 nomultithread cores
        executar "$rep" tpp4_mpi      32  8  1 nomultithread cores
    fi
done
printf 'Resultados em: %s\n' "$CSV"
