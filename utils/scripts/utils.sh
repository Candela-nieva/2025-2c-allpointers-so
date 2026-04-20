#!/bin/bash

# Colores
GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

# Directorio donde guardaremos los configs modificados para las pruebas
TEST_CONFIG_DIR="tests_configs"

# Función para modificar configs usando sed
# Uso: set_config <archivo_en_tests_configs> <clave> <valor>
set_config() {
    local file=$1
    local key=$2
    local value=$3
    # Busca la clave y reemplaza todo lo que sigue al =
    sed -i "s|^\($key\s*=\).*|\1$value|" "$file"
}

# Función para iniciar Master
start_master() {
    echo -e "${GREEN}Iniciando Master...${NC}"
    # Ejecuta desde la raíz, busca el binario en su carpeta y usa el config de pruebas
    ./master/bin/master $TEST_CONFIG_DIR/master.config > master.log 2>&1 &
    MASTER_PID=$!
    sleep 1
}

# Función para iniciar Storage
start_storage() {
    echo -e "${GREEN}Iniciando Storage...${NC}"
    ./storage/bin/storage $TEST_CONFIG_DIR/storage.config > storage.log 2>&1 &
    STORAGE_PID=$!
    sleep 2 # Dar tiempo a levantar FS
}

# Uso: start_worker <id> <config_file_name>
# El config file debe estar dentro de tests_configs/
start_worker() {
    local id=$1
    local config_name=$2
    echo -e "${GREEN}Iniciando Worker $id...${NC}"
    ./worker/bin/worker "$TEST_CONFIG_DIR/$config_name" "$id" > "worker$id.log" 2>&1 &
    # Guardamos el PID en una variable dinámica para matarlo después
    eval "WORKER${id}_PID=$!"
    sleep 1
}

# Uso: run_query <nombre_script> <prioridad>
run_query() {
    local script=$1
    local prio=$2
    echo -e "Ejecutando QC: Script=$script Prio=$prio"
    ./query_control/bin/query_control "$TEST_CONFIG_DIR/query.config" "$script" "$prio" > "qc_$script.log" 2>&1
}

# Uso: run_query_background <nombre_script> <prioridad>
run_query_background() {
    local script=$1
    local prio=$2
    ./query_control/bin/query_control "$TEST_CONFIG_DIR/query.config" "$script" "$prio" > "qc_${script}_$RANDOM.log" 2>&1 &
}

kill_all() {
    echo -e "${RED}Matando procesos...${NC}"
    killall -9 master storage worker query_control 2>/dev/null
    wait 2>/dev/null
}