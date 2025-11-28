#!/bin/bash
source utils/scripts/utils.sh

echo "=== PRUEBA 1: PLANIFICACIÓN ==="

# --- FASE 1: FIFO ---
echo -e "\n${GREEN}>>> FASE 1: FIFO${NC}"
kill_all
sleep 1

# Configurar (Edita los archivos en tests_configs/)
set_config tests_configs/master.config ALGORITMO_PLANIFICACION FIFO
set_config tests_configs/worker1.config ALGORITMO_REEMPLAZO LRU
set_config tests_configs/worker1.config TAM_MEMORIA 1024
set_config tests_configs/worker1.config RETARDO_MEMORIA 500

set_config tests_configs/worker2.config ALGORITMO_REEMPLAZO CLOCK-M
set_config tests_configs/worker2.config TAM_MEMORIA 1024
set_config tests_configs/worker2.config RETARDO_MEMORIA 500

set_config tests_configs/storage.config RETARDO_OPERACION 250
set_config tests_configs/storage.config RETARDO_ACCESO_BLOQUE 250
set_config tests_configs/storage.config FRESH_START TRUE

start_storage
start_master
start_worker 1 worker1.config
start_worker 2 worker2.config

echo "Lanzando Queries FIFO..."
run_query_background FIFO_1 4
sleep 0.5
run_query_background FIFO_2 3
sleep 0.5
run_query_background FIFO_3 5
sleep 0.5
run_query_background FIFO_4 1

echo -e "\n${RED}Esperando finalización. Presiona ENTER para iniciar FASE 2 (Prioridades)...${NC}"
read

# --- FASE 2: PRIORIDADES ---
echo -e "\n${GREEN}>>> FASE 2: PRIORIDADES + AGING${NC}"
kill_all
sleep 2

# Configurar cambios
set_config tests_configs/master.config ALGORITMO_PLANIFICACION PRIORIDADES
set_config tests_configs/master.config TIEMPO_AGING 5000
set_config tests_configs/storage.config FRESH_START FALSE

start_storage
start_master
start_worker 1 worker1.config
start_worker 2 worker2.config

echo "Lanzando Queries Aging..."
run_query_background AGING_1 4
sleep 0.5
run_query_background AGING_2 3
sleep 0.5
run_query_background AGING_3 5
sleep 0.5
run_query_background AGING_4 1

wait
echo "Prueba Planificación Finalizada."