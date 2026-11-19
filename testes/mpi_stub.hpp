// Simula apenas MPI_COMM_WORLD de 1 rank para validar o codigo sem MPI instalado.
// NUNCA usar no cluster: as versoes reais incluem <mpi.h>.
#pragma once
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using MPI_Comm = int;
using MPI_Datatype = int;
using MPI_Op = int;
using MPI_Status = int;
constexpr int MPI_COMM_WORLD = 0;
constexpr int MPI_PROC_NULL = -1;
constexpr int MPI_THREAD_FUNNELED = 1;
constexpr int MPI_UNSIGNED_CHAR = 1;
constexpr int MPI_LONG_LONG_INT = 2;
constexpr int MPI_DOUBLE = 3;
constexpr int MPI_UINT64_T = 4;
constexpr int MPI_SUM = 1;
constexpr int MPI_MAX = 2;
constexpr MPI_Status* MPI_STATUS_IGNORE = nullptr;
inline std::size_t tamanho_tipo(MPI_Datatype t) {
    if (t == MPI_UNSIGNED_CHAR) return 1;
    if (t == MPI_DOUBLE) return sizeof(double);
    if (t == MPI_LONG_LONG_INT) return sizeof(long long);
    return sizeof(std::uint64_t);
}
inline int MPI_Init_thread(int*, char***, int required, int *provided) { *provided=required; return 0; }
inline int MPI_Comm_rank(MPI_Comm, int* rank) { *rank=0; return 0; }
inline int MPI_Comm_size(MPI_Comm, int* n) { *n=1; return 0; }
inline int MPI_Abort(MPI_Comm, int code) { std::exit(code); }
inline int MPI_Finalize() { return 0; }
inline int MPI_Barrier(MPI_Comm) { return 0; }
inline double MPI_Wtime() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
inline int MPI_Sendrecv(const void*, int, MPI_Datatype, int dest, int,
                        void*, int, MPI_Datatype, int source, int, MPI_Comm, MPI_Status*) {
    if (dest != MPI_PROC_NULL || source != MPI_PROC_NULL) {
        std::fprintf(stderr,"MPI fake suporta apenas 1 processo.\n"); std::exit(4);
    }
    return 0;
}
inline int MPI_Allreduce(const void* send, void* recv, int n, MPI_Datatype t, MPI_Op, MPI_Comm) {
    std::memcpy(recv, send, std::size_t(n)*tamanho_tipo(t)); return 0;
}
inline int MPI_Reduce(const void* send, void* recv, int n, MPI_Datatype t, MPI_Op, int, MPI_Comm) {
    std::memcpy(recv, send, std::size_t(n)*tamanho_tipo(t)); return 0;
}
inline int MPI_Gather(const void* send, int n, MPI_Datatype t, void* recv,
                      int, MPI_Datatype, int, MPI_Comm) {
    std::memcpy(recv, send, std::size_t(n)*tamanho_tipo(t)); return 0;
}
