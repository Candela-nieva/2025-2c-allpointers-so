#!/bin/bash
source utils/scripts/utils.sh

# Obtener ruta absoluta de la carpeta de queries
REPO_ROOT=$(pwd)
QUERIES_PATH="$REPO_ROOT/queries"

mkdir -p tests_configs

echo "Generando configs de prueba..."
echo "Configurando PATH_SCRIPTS = $QUERIES_PATH"

# 1. Copiar Master
if [ -f master/master.config ]; then
    cp master/master.config tests_configs/master.config
else
    echo -e "${RED}Falta master/master.config${NC}"
fi

# 2. Copiar Storage
if [ -f storage/storage.config ]; then
    cp storage/storage.config tests_configs/storage.config
    # Asegurar punto de montaje si es relativo (opcional, pero recomendado)
    # set_config tests_configs/storage.config PUNTO_MONTAJE "$REPO_ROOT/mnt" 
else
    echo -e "${RED}Falta storage/storage.config${NC}"
fi

# 3. Copiar Query Control
if [ -f query_control/query.config ]; then
    cp query_control/query.config tests_configs/query.config
else
    # Crear uno dummy si no existe, a veces QC no usa config en disco sino argumentos
    touch tests_configs/query.config
fi

# 4. Copiar y Configurar Workers
if [ -f worker/worker.config ]; then
    # Generar config para Worker 1
    cp worker/worker.config tests_configs/worker1.config
    set_config tests_configs/worker1.config PATH_SCRIPTS "$QUERIES_PATH"
    
    # Generar config para Worker 2
    cp worker/worker.config tests_configs/worker2.config
    set_config tests_configs/worker2.config PATH_SCRIPTS "$QUERIES_PATH"
    
    # Generar configs para Estabilidad (Workers 3 a 6)
    for i in {3..6}; do
        cp worker/worker.config "tests_configs/worker$i.config"
        set_config "tests_configs/worker$i.config" PATH_SCRIPTS "$QUERIES_PATH"
    done
else
    echo -e "${RED}Falta worker/worker.config${NC}"
    exit 1
fi

echo -e "${GREEN}Configs listos en tests_configs/. PATH_SCRIPTS actualizado.${NC}"
