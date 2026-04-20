#!/bin/bash
source utils/scripts/utils.sh

echo "=== PRUEBA 4: STORAGE (DEDUPLICACIÓN) ==="
kill_all
sleep 1

set_config configs/master.config ALGORITMO_PLANIFICACION PRIORIDADES
set_config configs/master.config TIEMPO_AGING 0
set_config configs/worker1.config TAM_MEMORIA 128
set_config configs/worker1.config RETARDO_MEMORIA 250
set_config configs/worker1.config ALGORITMO_REEMPLAZO LRU
set_config configs/storage.config FRESH_START FALSE
set_config configs/storage.config RETARDO_OPERACION 500
set_config configs/storage.config RETARDO_ACCESO_BLOQUE 500

start_storage
start_master
start_worker 1 configs/worker1.config

echo "Lanzando queries de preparación..."
run_query_background STORAGE_1 0
sleep 0.2
run_query_background STORAGE_2 2
sleep 0.2
run_query_background STORAGE_3 4
sleep 0.2
run_query_background STORAGE_4 6

echo "Esperando finalización..."
wait $(pgrep query_control) 2>/dev/null

echo -e "\n${GREEN}Estado actual de physical_blocks:${NC}"
ls -l ./storage/physical_blocks/

echo -e "\n${RED}Presiona ENTER para ejecutar STORAGE_5 (Validar deduplicación)...${NC}"
read

run_query STORAGE_5 0

echo -e "\n${GREEN}Estado FINAL de physical_blocks (Verificar Hardlinks):${NC}"
ls -l ./storage/physical_blocks/

kill_all
echo "Prueba Storage Finalizada."