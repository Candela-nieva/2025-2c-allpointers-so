#!/bin/bash

echo "--- Inicializando Estructura de Despliegue ---"

# 1. Preparar Punto de Montaje del Storage (NUEVO)
# Esto crea la carpeta real en el sistema de archivos de la VM
MOUNT_POINT="/home/utnso/storage"
echo "Configurando Punto de Montaje en $MOUNT_POINT..."
# Crear directorio si no existe (requiere permisos de usuario común sobre /home/utnso)
if [ ! -d "$MOUNT_POINT" ]; then
    mkdir -p "$MOUNT_POINT"
    echo "Directorio $MOUNT_POINT creado"
else
    echo "El directorio $MOUNT_POINT ya existe"
fi

# Crear superblock.config con los valores pedidos
SUPERBLOCK="$MOUNT_POINT/superblock.config"
echo "Generando $SUPERBLOCK..."

# Escribimos el archivo usando cat y EOF
cat > "$SUPERBLOCK" <<EOF
FS_SIZE=65536
BLOCK_SIZE=16
EOF

echo "Superbloque creado con FS_SIZE=65536 y BLOCK_SIZE=16."