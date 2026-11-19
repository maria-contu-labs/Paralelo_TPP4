MPICXX ?= mpicxx
CXX ?= g++
FLAGS = -O3 -std=c++17 -Wall -Wextra -Wpedantic

.PHONY: all clean testar
all: tpp4_mpi tpp4_hibrido

tpp4_mpi: incendio.cpp
	$(MPICXX) $(FLAGS) $< -o $@

tpp4_hibrido: incendio.cpp
	$(MPICXX) $(FLAGS) -fopenmp -DTPP4_HIBRIDO $< -o $@

testar:
	$(CXX) $(FLAGS) -DTPP4_MPI_STUB incendio.cpp -o testes/teste_sequencial
	$(CXX) $(FLAGS) -fopenmp -DTPP4_MPI_STUB -DTPP4_HIBRIDO incendio.cpp -o testes/teste_openmp
	python3 testes/validacao.py

clean:
	rm -f tpp4_mpi tpp4_hibrido testes/teste_sequencial testes/teste_openmp
