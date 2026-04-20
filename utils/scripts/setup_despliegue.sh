#!/bin/bash

echo "--- Inicializando Estructura de Despliegue ---"

# 1. Crear directorios temporales (que quizás no están en el repo)
mkdir -p tests_configs queries

# 2. Dar permisos de ejecución a tus scripts
# Asumimos que ya existen en utils/scripts porque los bajaste del repo
if [ -d "utils/scripts" ]; then
    echo "Dando permisos +x a los scripts en utils/scripts..."
    chmod +x utils/scripts/*.sh
else
    echo "Advertencia: No encontré la carpeta utils/scripts."
fi

# 3. Copiar los archivos de datos (Queries) a la carpeta centralizada
# Como están en utils/tests, las movemos a 'queries/' para que el config las encuentre
if [ -d "utils/tests" ]; then
    echo "Copiando archivos de queries desde utils/tests/ a queries/..."
    cp utils/tests/* queries/ 2>/dev/null
    echo "[OK] Queries copiadas."
else
    echo "[INFO] No se copiaron queries automáticas. Asegúrate de tener los archivos en la carpeta 'queries/'."
fi

# 4. Preparar Punto de Montaje del Storage (NUEVO)
# Esto crea la carpeta real en el sistema de archivos de la VM
MOUNT_POINT="/home/utnso/storage"
echo "Configurando Punto de Montaje en $MOUNT_POINT..."

# Crear directorio si no existe (requiere permisos de usuario común sobre /home/utnso)
if [ ! -d "$MOUNT_POINT" ]; then
    mkdir -p "$MOUNT_POINT"
    echo "[OK] Directorio $MOUNT_POINT creado."
else
    echo "[INFO] El directorio $MOUNT_POINT ya existe."
fi

# Crear superblock.config con los valores pedidos
SUPERBLOCK="$MOUNT_POINT/superblock.config"
echo "Generando $SUPERBLOCK..."

# Escribimos el archivo usando cat y EOF
cat > "$SUPERBLOCK" <<EOF
FS_SIZE=65536
BLOCK_SIZE=16
EOF

echo "[OK] Superbloque creado con FS_SIZE=65536 y BLOCK_SIZE=16."

echo ""
echo "ESTRUCTURA LISTA:"
echo "  /utils/scripts   -> LOs scripts de prueba (ya existentes)"
echo "  /queries         -> Archivos de datos para las pruebas"
echo "  /tests_configs   -> Carpeta temporal para configs generados"
echo ""
echo "Siguiente paso: Ejecutar ./utils/scripts/setup_configs.sh"