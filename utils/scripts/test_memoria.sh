#!/bin/bash
source utils/scripts/utils.sh

echo "=== PRUEBA 2: MEMORIA WORKER ==="

# Config común
set_config configs/master.config ALGORITMO_PLANIFICACION FIFO
set_config configs/master.config TIEMPO_AGING 0
set_config configs/storage.config FRESH_START FALSE
set_config configs/storage.config RETARDO_OPERACION 500
set_config configs/storage.config RETARDO_ACCESO_BLOQUE 500

# --- FASE 1: CLOCK-M ---
echo -e "\n${GREEN}>>> FASE 1: CLOCK-M (Memoria 48)${NC}"
kill_all
sleep 1

set_config configs/worker1.config TAM_MEMORIA 48
set_config configs/worker1.config RETARDO_MEMORIA 250
set_config configs/worker1.config ALGORITMO_REEMPLAZO CLOCK-M

start_storage
start_master
start_worker 1 configs/worker1.config

run_query MEMORIA_WORKER 0

echo -e "\n${RED}Presiona ENTER para FASE 2 (LRU)...${NC}"
read

# --- FASE 2: LRU ---
echo -e "\n${GREEN}>>> FASE 2: LRU (Memoria 48)${NC}"
kill_all
sleep 1

set_config configs/worker1.config ALGORITMO_REEMPLAZO LRU

start_storage
start_master
start_worker 1 configs/worker1.config

run_query MEMORIA_WORKER_2 0

wait
echo "Prueba Memoria Finalizada."