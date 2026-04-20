#!/bin/bash
source utils/scripts/utils.sh

echo "=== PRUEBA 5: ESTABILIDAD GENERAL ==="
kill_all
sleep 1

# Configuración Master/Storage
set_config tests_configs/master.config ALGORITMO_PLANIFICACION PRIORIDADES
set_config tests_configs/master.config TIEMPO_AGING 250
set_config tests_configs/storage.config FRESH_START FALSE
set_config tests_configs/storage.config RETARDO_OPERACION 100
set_config tests_configs/storage.config RETARDO_ACCESO_BLOQUE 100

# Configuración Workers
# W1 & W2
set_config tests_configs/worker1.config TAM_MEMORIA 128
set_config tests_configs/worker1.config RETARDO_MEMORIA 100
set_config tests_configs/worker1.config ALGORITMO_REEMPLAZO LRU

set_config tests_configs/worker2.config TAM_MEMORIA 256
set_config tests_configs/worker2.config RETARDO_MEMORIA 50
set_config tests_configs/worker2.config ALGORITMO_REEMPLAZO CLOCK-M

# W3 & W4
set_config tests_configs/worker3.config TAM_MEMORIA 64
set_config tests_configs/worker3.config RETARDO_MEMORIA 150
set_config tests_configs/worker3.config ALGORITMO_REEMPLAZO CLOCK-M

set_config tests_configs/worker4.config TAM_MEMORIA 96
set_config tests_configs/worker4.config RETARDO_MEMORIA 125
set_config tests_configs/worker4.config ALGORITMO_REEMPLAZO LRU

# W5 & W6
set_config tests_configs/worker5.config TAM_MEMORIA 160
set_config tests_configs/worker5.config RETARDO_MEMORIA 75
set_config tests_configs/worker5.config ALGORITMO_REEMPLAZO LRU

set_config tests_configs/worker6.config TAM_MEMORIA 192
set_config tests_configs/worker6.config RETARDO_MEMORIA 175
set_config tests_configs/worker6.config ALGORITMO_REEMPLAZO CLOCK-M

echo "Iniciando Infraestructura Base (W1, W2)..."
start_storage
start_master
start_worker 1 worker1.config
start_worker 2 worker2.config

echo "Lanzando 25 instancias de AGING_1 (Prioridad 20)..."
for i in {1..25}; do
   run_query_background AGING_1 20
done

echo "Esperando 45 segundos..."
sleep 45

echo -e "${GREEN}>>> Iniciando Workers 3 y 4${NC}"
start_worker 3 worker3.config
start_worker 4 worker4.config

echo "Esperando 45 segundos..."
sleep 45

echo -e "${RED}>>> Matando Workers 1 y 2${NC}"
kill $WORKER1_PID
kill $WORKER2_PID

echo "Esperando 45 segundos..."
sleep 45

echo -e "${GREEN}>>> Iniciando Workers 5 y 6${NC}"
start_worker 5 worker5.config
start_worker 6 worker6.config

echo "Esperando finalización de todas las queries..."
wait $(pgrep query_control) 2>/dev/null

echo "Prueba Estabilidad Finalizada."
kill_all