// TPP4: propagacao de incendio florestal no modelo de fases paralelas.
// Compilar com mpicxx (MPI puro) ou mpicxx -fopenmp -DTPP4_HIBRIDO (hibrido).
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef TPP4_MPI_STUB
#include "testes/mpi_stub.hpp" // So para teste local SEM MPI real.
#else
#include <mpi.h>
#endif

#ifdef TPP4_HIBRIDO
#include <omp.h>
#endif

namespace {
enum Celula : std::uint8_t { SOLO = 0, ARVORE = 1, CHAMA = 2, CINZA = 3 };
struct Opcoes {
    int largura = 2048;
    int altura = 2048;
    int passos = 3000;
    int densidade = 88; // Percentual de vegetacao.
    std::uint64_t semente = 2026;
    std::string imagem;
};

std::uint64_t mistura(std::uint64_t v) {
    v += UINT64_C(0x9e3779b97f4a7c15);
    v = (v ^ (v >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    v = (v ^ (v >> 27)) * UINT64_C(0x94d049bb133111eb);
    return v ^ (v >> 31);
}

// Aleatoriedade reproduzivel por coordenada e passo, sem estado compartilhado.
std::uint64_t aleatorio(std::uint64_t semente, int x, int y, int passo) {
    return mistura(semente ^
                   (std::uint64_t(x + 1) * UINT64_C(0x9e3779b97f4a7c15)) ^
                   (std::uint64_t(y + 1) * UINT64_C(0xbf58476d1ce4e5b9)) ^
                   (std::uint64_t(passo + 1) * UINT64_C(0x94d049bb133111eb)));
}

int numero(const char *valor, const char *nome) {
    std::size_t pos = 0;
    long long resultado = std::stoll(valor, &pos);
    if (valor[pos] != '\0' || resultado < 0 || resultado > std::numeric_limits<int>::max()) {
        throw std::invalid_argument(std::string("Argumento invalido: ") + nome);
    }
    return static_cast<int>(resultado);
}

Opcoes interpretar(int argc, char **argv) {
    Opcoes op;
    for (int i = 1; i < argc; ++i) {
        std::string chave = argv[i];
        if (chave == "--ajuda" || chave == "--help") {
            std::cout << "Uso: ./tpp4_mpi [--largura 2048] [--altura 2048] "
                         "[--passos 3000] [--densidade 88] [--semente 2026] "
                         "[--imagem prefixo]\n";
            std::exit(0);
        }
        if (++i >= argc) throw std::invalid_argument("Falta valor para " + chave);
        if (chave == "--largura") op.largura = numero(argv[i], "largura");
        else if (chave == "--altura") op.altura = numero(argv[i], "altura");
        else if (chave == "--passos") op.passos = numero(argv[i], "passos");
        else if (chave == "--densidade") op.densidade = numero(argv[i], "densidade");
        else if (chave == "--semente") {
            std::size_t pos = 0;
            op.semente = std::stoull(argv[i], &pos);
            if (argv[i][pos] != '\0') throw std::invalid_argument("Semente invalida");
        } else if (chave == "--imagem") op.imagem = argv[i];
        else throw std::invalid_argument("Opcao desconhecida: " + chave);
    }
    if (op.largura < 8 || op.altura < 8 || op.passos < 1 ||
        op.densidade < 0 || op.densidade > 100) {
        throw std::invalid_argument("Requer largura/altura >= 8; passos >= 1; densidade entre 0 e 100");
    }
    if (std::uint64_t(op.largura) * std::uint64_t(op.altura) >
        std::uint64_t(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("A grade excede o limite do MPI_Gather");
    }
    return op;
}

void trocar_bordas(const std::vector<std::uint8_t> &atual,
                   std::vector<std::uint8_t> &cima,
                   std::vector<std::uint8_t> &baixo,
                   int linhas, int largura, int rank, int np) {
    const int anterior = (rank == 0) ? MPI_PROC_NULL : rank - 1;
    const int proximo = (rank == np - 1) ? MPI_PROC_NULL : rank + 1;
    // A linha superior vai para cima; a linha inferior do vizinho chega de cima.
    MPI_Sendrecv(atual.data(), largura, MPI_UNSIGNED_CHAR, anterior, 10,
                 baixo.data(), largura, MPI_UNSIGNED_CHAR, proximo, 10,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Sendrecv(atual.data() + std::size_t(linhas - 1) * largura,
                 largura, MPI_UNSIGNED_CHAR, proximo, 11,
                 cima.data(), largura, MPI_UNSIGNED_CHAR, anterior, 11,
                 MPI_COMM_WORLD, MPI_STATUS_IGNORE);
}

// Salva imagem PPM sem biblioteca grafica; executar fora da medicao.
void salvar_ppm(const std::vector<std::uint8_t> &local, const Opcoes &op,
                const std::string &arquivo, int rank) {
    const int nlocal = static_cast<int>(local.size());
    std::vector<std::uint8_t> global;
    if (rank == 0) global.resize(std::size_t(op.largura) * op.altura);
    MPI_Gather(local.data(), nlocal, MPI_UNSIGNED_CHAR,
               rank == 0 ? global.data() : nullptr, nlocal, MPI_UNSIGNED_CHAR,
               0, MPI_COMM_WORLD);
    if (rank != 0) return;
    std::ofstream f(arquivo, std::ios::binary);
    if (!f) throw std::runtime_error("Nao foi possivel criar " + arquivo);
    f << "P6\n" << op.largura << ' ' << op.altura << "\n255\n";
    constexpr unsigned char rgb[4][3] = {
        {34, 40, 47}, {43, 112, 72}, {255, 137, 41}, {76, 67, 65}
    };
    for (auto valor : global) {
        f.write(reinterpret_cast<const char *>(rgb[valor]), 3);
    }
}

} // namespace

int main(int argc, char **argv) {
    int provided = 0;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided);
    int rank = 0, np = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &np);
    if (provided < MPI_THREAD_FUNNELED) {
        if (rank == 0) std::cerr << "MPI sem suporte MPI_THREAD_FUNNELED\n";
        MPI_Abort(MPI_COMM_WORLD, 2);
    }

    Opcoes op;
    try { op = interpretar(argc, argv); }
    catch (const std::exception &ex) {
        if (rank == 0) std::cerr << ex.what() << '\n';
        MPI_Abort(MPI_COMM_WORLD, 2);
    }
    if (op.altura % np != 0) {
        if (rank == 0) std::cerr << "Altura deve ser divisivel por processos MPI\n";
        MPI_Abort(MPI_COMM_WORLD, 2);
    }

#ifdef TPP4_HIBRIDO
    const int threads = omp_get_max_threads();
    const char *modo = "hibrido";
#else
    const int threads = 1;
    const char *modo = "mpi";
#endif
    const int linhas = op.altura / np;
    const int inicio = rank * linhas;
    const int W = op.largura;
    const std::size_t local_n = std::size_t(linhas) * W;
    std::vector<std::uint8_t> atual(local_n), proximo(local_n);
    std::vector<std::uint8_t> cima(W, SOLO), baixo(W, SOLO);

    // Fase de inicializacao (fora do cronometro): mesma floresta para todo np.
#ifdef TPP4_HIBRIDO
#pragma omp parallel for schedule(static)
#endif
    for (int y = 0; y < linhas; ++y) {
        const int gy = inicio + y;
        for (int x = 0; x < W; ++x) {
            const bool foco = (std::abs(x - W / 2) <= 2 &&
                               std::abs(gy - op.altura / 2) <= 2);
            const bool arvore = (aleatorio(op.semente, x, gy, 0) % 100) <
                                std::uint64_t(op.densidade);
            atual[std::size_t(y) * W + x] = foco ? CHAMA : (arvore ? ARVORE : SOLO);
        }
    }
    if (!op.imagem.empty()) salvar_ppm(atual, op, op.imagem + "_inicio.ppm", rank);

    double tempo_calculo = 0.0, tempo_comunicacao = 0.0, tempo_coletiva = 0.0;
    std::int64_t chamas_globais = -1;
    int passos_executados = 0;
    MPI_Barrier(MPI_COMM_WORLD);
    const double inicio_cronometro = MPI_Wtime();
    double t = MPI_Wtime();
    trocar_bordas(atual, cima, baixo, linhas, W, rank, np);
    tempo_comunicacao += MPI_Wtime() - t;

    for (int passo = 1; passo <= op.passos; ++passo) {
        // FASE 1: cada processo atualiza a propria regiao.
        std::int64_t chamas_locais = 0;
        t = MPI_Wtime();
#ifdef TPP4_HIBRIDO
#pragma omp parallel for schedule(static) reduction(+:chamas_locais)
#endif
        for (int y = 0; y < linhas; ++y) {
            const int gy = inicio + y;
            for (int x = 0; x < W; ++x) {
                const std::size_t idx = std::size_t(y) * W + x;
                const std::uint8_t estado = atual[idx];
                std::uint8_t novo = estado;
                if (estado == CHAMA) novo = CINZA;
                else if (estado == ARVORE) {
                    int influencia = 0;
                    for (int dy = -1; dy <= 1; ++dy) {
                        const int ny = y + dy;
                        const std::uint8_t *linha = ny < 0 ? cima.data() :
                            (ny >= linhas ? baixo.data() : atual.data() + std::size_t(ny) * W);
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nx = x + dx;
                            if ((dx == 0 && dy == 0) || nx < 0 || nx >= W) continue;
                            // Vento no sentido leste: fontes a oeste pesam mais.
                            if (linha[nx] == CHAMA) influencia += (dx == -1 ? 3 : 1);
                        }
                    }
                    if (influencia > 0) {
                        const int probabilidade = std::min(95, 20 + 12 * influencia);
                        if (aleatorio(op.semente, x, gy, passo) % 100 <
                            std::uint64_t(probabilidade)) novo = CHAMA;
                    }
                }
                proximo[idx] = novo;
                chamas_locais += (novo == CHAMA);
            }
        }
        tempo_calculo += MPI_Wtime() - t;

        // FASE 2: condicao global de parada via comunicacao coletiva.
        t = MPI_Wtime();
        MPI_Allreduce(&chamas_locais, &chamas_globais, 1, MPI_LONG_LONG_INT,
                      MPI_SUM, MPI_COMM_WORLD);
        tempo_coletiva += MPI_Wtime() - t;
        atual.swap(proximo);
        passos_executados = passo;
        if (chamas_globais == 0 || passo == op.passos) break;

        // FASE 3: troca de fronteiras com vizinhos para a proxima iteracao.
        t = MPI_Wtime();
        trocar_bordas(atual, cima, baixo, linhas, W, rank, np);
        tempo_comunicacao += MPI_Wtime() - t;
    }

    const double tempo_local = MPI_Wtime() - inicio_cronometro;
    double tempo_total = 0.0;
    MPI_Reduce(&tempo_local, &tempo_total, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
    double fase_local[3] = {tempo_calculo, tempo_coletiva, tempo_comunicacao};
    double fase_max[3] = {0.0, 0.0, 0.0};
    MPI_Reduce(fase_local, fase_max, 3, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

    std::uint64_t assinatura_local = 0;
    std::int64_t arvores_local = 0, cinzas_local = 0, chamas_final_local = 0;
#ifdef TPP4_HIBRIDO
#pragma omp parallel for schedule(static) reduction(+:assinatura_local,arvores_local,cinzas_local,chamas_final_local)
#endif
    for (int y = 0; y < linhas; ++y) {
        for (int x = 0; x < W; ++x) {
            const std::size_t idx = std::size_t(y) * W + x;
            const auto estado = atual[idx];
            assinatura_local += (std::uint64_t(inicio + y) * W + x + 1) * estado;
            arvores_local += (estado == ARVORE);
            cinzas_local += (estado == CINZA);
            chamas_final_local += (estado == CHAMA);
        }
    }
    std::uint64_t assinatura_global = 0;
    std::int64_t arvores = 0, cinzas = 0, chamas_final = 0;
    MPI_Reduce(&assinatura_local, &assinatura_global, 1, MPI_UINT64_T, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&arvores_local, &arvores, 1, MPI_LONG_LONG_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&cinzas_local, &cinzas, 1, MPI_LONG_LONG_INT, MPI_SUM, 0, MPI_COMM_WORLD);
    MPI_Reduce(&chamas_final_local, &chamas_final, 1, MPI_LONG_LONG_INT, MPI_SUM, 0, MPI_COMM_WORLD);

    if (!op.imagem.empty()) salvar_ppm(atual, op, op.imagem + "_fim.ppm", rank);
    if (rank == 0) {
        std::cout << std::fixed << std::setprecision(6);
        std::cout << modo << ',' << np << ',' << threads << ',' << op.largura << ','
                  << op.altura << ',' << op.passos << ',' << op.densidade << ','
                  << op.semente << ',' << passos_executados << ',' << tempo_total << ','
                  << fase_max[0] << ',' << fase_max[1] << ',' << fase_max[2] << ','
                  << arvores << ',' << cinzas << ',' << chamas_final << ','
                  << assinatura_global << ',' << (chamas_globais == 0 ? "extinto" : "limite") << '\n';
    }
    MPI_Finalize();
    return 0;
}
