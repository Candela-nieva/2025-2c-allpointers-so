#include "storageUtils.h"

int retardo_operacion;
int retardo_acceso_bloque;
bool fresh_start;
int fs_size;
int tam_bloq;
int cantBloq;
//int arrayDeBits[];
char path_files[256];
char path_blocks[256];
t_bitarray* bitarray;
void* mappeo;
t_dictionary *diccionario_archivos = NULL;
FILE* archBitmap;
// Hicimos globales para que podamos hacer msync con mappeo y
// cerrar archBitmap cuuando terminemos de usarlo
t_log* loggerStorage = NULL;
t_config *config = NULL;
t_config *config_SB = NULL;
t_config_storage *config_struct = NULL;
t_config_superblock *config_superBlock = NULL;
char* config_storage;
t_config* configHash = NULL;
// SEMAFOROS
pthread_mutex_t mutex_hash_index;
pthread_mutex_t mutex_bitmap;
//==========INICIALIZACION==========

void inicializar_config(void){
    config_struct = malloc(sizeof(t_config_storage)); //Reserva memoria
    config_struct->modulo = NULL;
    config_struct->puerto_escucha = NULL;
    config_struct->fresh_start = NULL;
    config_struct->punto_montaje = NULL;
    config_struct->retardo_operacion = NULL;
    config_struct->retardo_acceso_bloque = NULL;
    config_struct->log_level = NULL;

    config_superBlock = malloc(sizeof(t_config_superblock));
    config_superBlock->fs_size = NULL;
    config_superBlock->tam_bloq = NULL;
}

void cargar_config() {
    config = config_create(config_storage);
    config_struct->modulo = config_get_string_value (config, "MODULO");
    config_struct->puerto_escucha = config_get_string_value(config, "PUERTO_ESCUCHA");
    config_struct->fresh_start = config_get_string_value(config, "FRESH_START");
    config_struct->punto_montaje = config_get_string_value(config, "PUNTO_MONTAJE");
    config_struct->retardo_operacion = config_get_string_value(config, "RETARDO_OPERACION");
    config_struct->retardo_acceso_bloque = config_get_string_value(config, "RETARDO_ACCESO_BLOQUE");
    config_struct->log_level = config_get_string_value(config, "LOG_LEVEL");

    retardo_operacion = atoi(config_struct->retardo_operacion);
    retardo_acceso_bloque = atoi(config_struct->retardo_acceso_bloque);

}

// Función para iniciar el logger
void crear_logger () {
    loggerStorage=iniciar_logger("storage.log","STORAGE",true, log_level_from_string(config_struct->log_level));
}

t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle){
	t_log* nuevo_logger;
	nuevo_logger = log_create(nombreArchivoLog, nombreLog, seMuestraEnConsola, nivelDetalle);
    if (nuevo_logger == NULL) {
		perror("Error en el logger"); // Maneja error si no se puede crear el logger
		exit(EXIT_FAILURE);
	}
	return nuevo_logger;
}

//==========CONEXIONES==========

void iniciar_servidor_multihilo(void)
{
    int fd_sv = crear_servidor(config_struct->puerto_escucha);
    log_info(loggerStorage, "Servidor STORAGE escuchando en puerto %s", config_struct->puerto_escucha);
    while (1)
    {
        int fd_conexion = esperar_cliente(fd_sv, "STORAGE", loggerStorage);
        int operacion = recibir_operacion(fd_conexion);
        if(operacion == HANDSHAKE_WORKER){
            log_info(loggerStorage, "Conexion Exitosa con un nuevo Worker, ENVIANDO TAMANIO BLOQUE : %d", tam_bloq);
            t_paquete* paquete = crear_paquete(ENVIAR_TAMANIO_BLOQUE);
            agregar_a_paquete(paquete, &tam_bloq, sizeof(int));
            enviar_paquete(paquete, fd_conexion);
            eliminar_paquete(paquete);
            close(fd_conexion); // NUEVO: cierro si no voy a atender más
            //pthread_t hilo_worker;
            //pthread_create(&hilo_worker, NULL, atender_conexion, NULL);
            //pthread_detach(hilo_worker);
        }else{
            log_info(loggerStorage, "Operacion desconocida. Cerrando conexion.");
            close(fd_conexion);
        }
    }
    // Nunca llega acá
    close(fd_sv);
    return;
}

//==========FRESH_START==========

void inicializar_montaje(){
    diccionario_archivos = dictionary_create();
    inicializar_semaforos();
    cargar_config_hashIndex();
    cargar_config_superBlock();
    freshStart();
    initialFile();
    log_info(loggerStorage, "SE ABRIO EL DIRECTORIO RAIZ : FS SIZE = %d ; BLOCK SIZE = %d",fs_size,tam_bloq);
}

void inicializar_semaforos() {
    pthread_mutex_init(&mutex_hash_index, NULL);
    pthread_mutex_init(&mutex_bitmap, NULL);
}

void cargar_config_hashIndex(){
    char path_blocks_hash[256];
    sprintf(path_blocks_hash, "%s/blocks_hash_index.config", config_struct->punto_montaje);
    configHash = config_create(path_blocks_hash);

}

void cargar_config_superBlock(){
    char ruta_completa[512];
    snprintf(ruta_completa, sizeof(ruta_completa), "%s%s", config_struct->punto_montaje, "/superblock.config");
    config_SB = config_create(ruta_completa);
    config_superBlock->fs_size = config_get_string_value(config_SB, "FS_SIZE");
    config_superBlock->tam_bloq = config_get_string_value(config_SB, "BLOCK_SIZE");
    
    fs_size = atoi(config_superBlock->fs_size);
        printf("llegamos a despues");

    tam_bloq = atoi(config_superBlock->tam_bloq);
    cantBloq = fs_size / tam_bloq; //al ser un bitmap, cada entrada es de 1 bit, por lo que el tamanio es igual a cantBloques bits
    log_info(loggerStorage,"CantBloques: %d", cantBloq);

    sprintf(path_blocks, "%s/physical_blocks", config_struct->punto_montaje);
    sprintf(path_files, "%s/files", config_struct->punto_montaje);
    
}

void freshStart(){
    verificar_freshStart();
    if(fresh_start) {
        formateo();
    }
}

void verificar_freshStart(){
    if(strcmp((config_struct->fresh_start), "TRUE") == 0){
        fresh_start = true;
    }else{
        fresh_start = false;
    }
}

void formateo() {
    limpiar_fs();
    recrear_fs();
}

//==========ELIMINACION Y CREACION DE ESTRUCTURAS==========

void limpiar_fs() {
    char path_bitmap[256];
    char path_blocks_hash[256];

    sprintf(path_bitmap, "%s/bitmap.bin", config_struct->punto_montaje);
    sprintf(path_blocks_hash, "%s/blocks_hash_index.config", config_struct->punto_montaje);

    char cmd[512];
    sprintf(cmd, "rm -rf %s/bitmap.bin %s/blocks_hash_index.config %s/physical_blocks %s/files",
            config_struct->punto_montaje, config_struct->punto_montaje,
            config_struct->punto_montaje, config_struct->punto_montaje);
    system(cmd);
}

void recrear_fs() {
    crear_bitmap();
    crear_directorios();
    crear_BlocksHashIndex();
}

void crear_bitmap() {
    char pathBitmap[256];
    sprintf(pathBitmap, "%s/bitmap.bin", config_struct->punto_montaje);
    FILE* archBitmap = fopen(pathBitmap,"wb+");
    int fildes = fileno(archBitmap);
    ftruncate(fildes, cantBloq/8);
    mappeo = mmap(NULL, cantBloq, PROT_READ | PROT_WRITE, MAP_SHARED, fildes, 0);
    if (mappeo == MAP_FAILED) {
        log_error(loggerStorage, "Fallo el mappeo del bitmap");
        exit(EXIT_FAILURE);
    }
    bitarray = bitarray_create_with_mode(mappeo, cantBloq/8, LSB_FIRST);
    fclose(archBitmap);
    // tal vez hay que sacar este fclose por lo que dijeron en el foro!!!!!!!!!
}

void crear_directorios() {
    crear_directorio(config_struct->punto_montaje, "files", NULL);
    crear_directorio(config_struct->punto_montaje, "physical_blocks", NULL);
    crear_physical_blocks();
}

void crear_directorio(char* path, char* nombreDirectorio, char *nuevoPath) {
    char directorio[256];
    sprintf(directorio, "%s/%s", path, nombreDirectorio);
    if(mkdir(directorio, 0755) == -1) {
        if (errno != EEXIST) {
            log_error(loggerStorage, "Error al crear el directorio '%s': %s", nombreDirectorio, strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    if(nuevoPath != NULL){
        strcpy(nuevoPath,directorio);   //en caso de que querramos conservar el path del nuevo directorio, sino pasamos NULL
    }
}

void crear_BlocksHashIndex() {
    char pathBlocksHashIndex[256];
    sprintf(pathBlocksHashIndex, "%s/blocks_hash_index.config", config_struct->punto_montaje);
    FILE* archBlocksHashIndex = fopen(pathBlocksHashIndex,"w+");
    if(!archBlocksHashIndex) {
        log_error(loggerStorage, "Error al crear el archivo 'blocks_hash_index.config': %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    fclose(archBlocksHashIndex);
}

char *obtener_path_bloque_fisico(int nroBloque){
    char Bloque[256];
    int anchoEntrada = calcularAncho();
    sprintf(Bloque,"%0*d", anchoEntrada, nroBloque);
    char *pathBloq = malloc(256);
    sprintf(pathBloq, "%s/block%s.dat", path_blocks, Bloque);
    log_info(loggerStorage, "path del bloque fisico %d : %s",nroBloque,pathBloq);
    return pathBloq;
}

void crear_physical_blocks() {
    int anchoEntrada = calcularAncho();
    char nombreArch[256];
    char nroBloque[32];
    for(int i=0; i < cantBloq; i++){
        sprintf(nroBloque,"%0*d", anchoEntrada, i);
        sprintf(nombreArch, "%s/block%s.dat", path_blocks, nroBloque);
        FILE *archBloque = fopen(nombreArch, "w+");
        if (!archBloque) {
            log_error(loggerStorage, "Error al crear '%s': %s", nombreArch, strerror(errno));
            exit(EXIT_FAILURE);
        }
        ftruncate(fileno(archBloque), tam_bloq);
        if(!archBloque) {
            log_error(loggerStorage, "Error al crear el archivo de bloque '%s' : %s", nombreArch, strerror(errno));
            exit(EXIT_FAILURE);
        }
        fclose(archBloque);
    }
}

void initialFile(){
    op_create("initial_file","BASE");
    op_truncate("initial_file","BASE",tam_bloq);
    //marcar_ocupado_en_bitmap(0);
    op_write("initial_file","BASE", 0, "hOla, me llamo Rusell y soy un guia explorador");
    op_commit("initial_file","BASE");

    //op_tag("initial_file","BASE","BASE2");
    
    /*int bloqueInicial = buscar_bloque_libre();
    bitarray_set_bit(bitarray,bloqueInicial);
    char *path_bloq = obtener_path_bloque_fisico(bloqueInicial);
    FILE *bloqFis = fopen(path_bloq, "w");
    for(int i = 0; i < tam_bloq;i++){
        fputc(0,bloqFis);
    }
    free(path_bloq);*/
}

//==========BITMAP==========
int buscar_bloque_libre(){
    for(int i = 0; i < cantBloq; i++){
        if(bitarray_test_bit(bitarray, i)==0){
            log_info(loggerStorage, "BLOQUE LIBRE ENCONTRADO %d",i);
            marcar_ocupado_en_bitmap(i);
            return i;
        }
            
    }
    log_info(loggerStorage, "NO SE ENCONTRO BLOQUE LIBRE");
    return -1;
}

//==========FORMATO DE LAS ENTRADAS==========

int calcularAncho(){
    int ancho = 1;
    int aux = cantBloq - 1;

    while(aux >= 10){
        ancho++;
        aux /= 10;
    }
    
    log_info(loggerStorage,"CANT CIFRAS = %d", ancho);
    return ancho;
}


//==========OPERACIONES==========

bool op_create(char *nombreArch, char *nombreTag){
    usleep(retardo_operacion);
    char initial[256];
    crear_directorio(path_files, nombreArch,initial);
    char tagBase[256];
    crear_directorio(initial, nombreTag, tagBase);
    crear_metadata(tagBase,NULL);
    char logicalBlocks[256];
    crear_directorio(tagBase, "logical_blocks", logicalBlocks);
    crear_fcb(nombreArch, nombreTag);

    return true;
}

t_metadata* leer_metadata(char* archivo, char* nombreTag) {
    // 1. Armar el path del archivo metadata
    char* path_metadata = path_Metadata(archivo, nombreTag);

    // 2. Crear estructura config
    t_config* config = config_create(path_metadata);
    if (config == NULL) {
        log_error(loggerStorage, "No se pudo abrir metadata %s", path_metadata);
        return NULL;
    }

    // 3. Crear estructura para devolver
    t_metadata* meta = malloc(sizeof(t_metadata));

    // 4. Leer el tamaño
    meta->tamanio = config_get_int_value(config, "TAMAÑO");

    // 5. Leer el estado
    meta->estado = strdup(config_get_string_value(config, "ESTADO"));

    // 6. Leer el array de bloques
    char** array_blocks = config_get_array_value(config, "BLOCKS");

    meta->blocks = list_create();
    for (int i = 0; array_blocks[i] != NULL; i++) {
        int* block = malloc(sizeof(int));
        *block = atoi(array_blocks[i]);
        list_add(meta->blocks, block);
    }
    /*for (int i = 0; array_blocks[i] != NULL; i++) {
        //int* block = malloc(sizeof(int));
        int block = atoi(array_blocks[i]);
        list_add(meta->blocks, block);
    }*/
    // 7. Cerrar el config
    config_destroy(config);
    free(path_metadata);
    return meta;
}

char* crear_bloq_log(char* pathTag, t_metadata *meta,int nro){
    /*int* cero = malloc(sizeof(int));
    *cero = 0;
    list_add(meta->blocks, cero);*/
    list_add(meta->blocks, 0);
    char nombreBloq[32];
    sprintf(nombreBloq, "%06d.dat", nro);

    char *path_logical = malloc(256);
    sprintf(path_logical, "%s/logical_blocks/%s", pathTag, nombreBloq);

    return path_logical;
}

bool agrandarArchivo (t_metadata* meta, char* pathTag, int nro, char* path_block0) {
    char *path_logical = crear_bloq_log(pathTag, meta, nro);
        if (link(path_block0, path_logical) == -1) {
            log_error(loggerStorage, "Link a block0 falló: %s", strerror(errno));
            destruir_metadata(meta);
            return false;
        }
    free(path_logical);
    return true;
}

void eliminar_bloq_log (char* pathTag, int nro) {
    // eliminar hardlink lógico, se puede hacer una funcion xq se repite
    char nombreBloq[32];
    sprintf(nombreBloq, "%06d.dat", nro);
    char path_logical[256];
    sprintf(path_logical, "%s/logical_blocks/%s", pathTag, nombreBloq);
    unlink(path_logical);
}

void achicarArchivo (t_metadata* meta, char* pathTag, int ancho, int nro, int bloque_fisico) {

    eliminar_bloq_log(pathTag, nro);

    // revisar nlink del físico
    char path_fisico[512];
    sprintf(path_fisico, "%s/block%0*d.dat", path_blocks, ancho, bloque_fisico);
    //stat devuelve las estadisticas de un file mediante el struct st,
    //siendo st_link la cantidad de hardlinks que lo referencian
    //si ya se hizo unlink antes, la cantidad de links deberia ser 0
    struct stat st;
    if (stat(path_fisico, &st) == 0) {
        if (st.st_nlink == 0 && bloque_fisico != 0) {
            marcar_libre_en_bitmap(bloque_fisico);
        }
    }

    int* pfis = list_remove(meta->blocks, nro);
    if (pfis)
        free(pfis);
}

bool op_truncate(char* nombreArch, char *nombreTag, int nuevoTamanio) {
    usleep(retardo_operacion);
    t_metadata* meta = leer_metadata(nombreArch, nombreTag);
    if (!meta) return false;

    int bloques_actuales = meta->tamanio / tam_bloq;
    int bloques_nuevos = nuevoTamanio / tam_bloq;

    t_tag* tag = buscar_Tag_Arch(nombreArch, nombreTag);
    if (!tag) {
        log_error(loggerStorage, "Tag no encontrado %s:%s", nombreArch, nombreTag);
        destruir_metadata(meta);
        return false;
    }
    int ancho = calcularAncho();
    // creo que faltan mutex de tag en esta funcion !!!!!!!!!!!
    if (bloques_nuevos > bloques_actuales) {
        char *path_block0 = obtener_path_bloque_fisico(0);
        for (int i = bloques_actuales; i < bloques_nuevos; i++) {
            if(!agrandarArchivo(meta, tag->pathTag, i, path_block0)) {
                free(path_block0);
                destruir_metadata(meta);
                return false;
            }
            tag->logBlocks++;
            //list_add(tag->physicalBlocks, (void*)0); //decimos que tiene asociado el bloque 0
            list_add(tag->physicalBlocks, 0);
            log_info(loggerStorage, "##<QUERY_ID> - %s:%s Se agregó el hard link del bloque lógico %06d al bloque físico 0", nombreArch, nombreTag, i);
        }
        free(path_block0);
    } else if (bloques_nuevos < bloques_actuales) {
        for (int i = bloques_actuales - 1; i >= bloques_nuevos; i--) {
            int *pbloqfis = (int)list_get(meta->blocks, i);
            int bloque_fisico = *pbloqfis;
            achicarArchivo(meta, tag->pathTag, ancho, i, bloque_fisico);
            tag->logBlocks--;
            log_info(loggerStorage, "##<QUERY_ID> - %s>:%s Se eliminó el hard link del bloque lógico %06d al bloque físico %d", nombreArch, nombreTag, i, bloque_fisico);
        }
    }

    tag->tamanio = nuevoTamanio;
    meta->tamanio = nuevoTamanio;
    //guardar_metadata(meta, nombreArch, nombreTag);
    //esto se tiene que hacer recien en commit
    // estamos seguros de esto??????????????????????
    log_info(loggerStorage, "##<QUERY_ID> - File Truncado %s:%s - Tamaño: %d", nombreArch, nombreTag, nuevoTamanio);
    destruir_metadata(meta);
    return true;
}
//cp [source] [destination]: Copy files or directories. podemos usar la funcion system para correr este comando
bool op_tag(char* nombreArch, char *nombreTagOrigen, char *nombreNuevoTag){
    usleep(retardo_operacion);
    t_tag *tagOrigen = buscar_Tag_Arch(nombreArch,nombreTagOrigen);
    if(tagOrigen){
        char pathArch[256];
        sprintf(pathArch,"%s/%s/%s",path_files,nombreArch,nombreNuevoTag);
        log_info(loggerStorage, "nuevo path : %s",pathArch);
        char cmd[256];
        sprintf(cmd,"cp -r %s %s",tagOrigen->pathTag,pathArch);
        log_info(loggerStorage, "comando : %s",cmd);
        system(cmd);
        crear_copia_tag(nombreArch,tagOrigen,nombreNuevoTag);
        return true;
    }
    return false;
}

bool op_commit(char* nombreArch, char *nombreTag){
    usleep(retardo_operacion);
    t_tag *tag = buscar_Tag_Arch(nombreArch,nombreTag);
    if(tag){
        pthread_mutex_lock(&tag->mutexTag);
        if (tag->estado == COMMITED) {
            pthread_mutex_unlock(&tag->mutexTag);
            log_info(loggerStorage, "File:Tag %s:%s ya está COMMITED. No hay cambios", nombreArch, nombreTag);
            return true; 
        }
        for(int i = 0; i < tag->logBlocks; i++){
            char *bloqLogPath = obtener_path_bloq_logico(tag, i);
            char *contenido = leer_contenido_bloque(bloqLogPath);
            char *hash = crypto_md5 (contenido, tam_bloq);
            free(contenido);
            int bloqActual = (int)list_get(tag->physicalBlocks,i);
            pthread_mutex_lock(&mutex_hash_index);
            if(config_has_property(configHash, hash)){
                // CASO DE DUPLICADO ENCONTRADO
                char *bloqFis = config_get_string_value(configHash, hash);
                int nroFis;
                nroFis = atoi(bloqFis + 5); //bloqFis siempre sera del mismo formato "blockNRO" por lo que a partir del 6to caracter esta el nro Fisico
                if(bloqActual != nroFis){
                    // Comenté esto porque estaban mal varias cosas:
                    // 1. Marcabamos libre en bitmap sin verificar si habia otro hardlink apuntandolo
                    // 2. list add index desplaza una posicion el bloque actual (+1). Habia que usar replace, que no desplaza, solo reeemplaza
                    // 3. Link hardlinkea al reves los parametros, el de la izq indica al que vas a apuntar, el de la der el bloque logico
                    // 4. Agregue mutex. El del tag quedo muy grande la SC, pero hay muchos usos intermedios.
                    
                    //marcar_libre_en_bitmap(bloqActual);
                    //list_remove(tag->physicalBlocks, bloqLog);
                    //list_add_in_index (tag->physicalBlocks, i, nroFis);
                    //char *bloqALiberar = obtener_path_bloque_fisico(bloqActual);
                    //char *nuevoBloq = obtener_path_bloque_fisico(nroFis);
                    //unlink(bloqLog);
                    //link(bloqLog,nuevoBloq);

                    //Reemplaza el número de bloque en la lista
                    list_replace(tag->physicalBlocks, i, (void*)nroFis);

                    // Reasignar hardlink
                    char*nuevoBloqPath = obtener_path_bloque_fisico(nroFis);
                    // Desvincular hardlink al bloque fisico anterior
                    unlink(bloqLogPath);
                    if(link(nuevoBloqPath, bloqLogPath) == -1) {
                        log_error(loggerStorage, "Fallo al crear link canónico: %s", strerror(errno));
                    }
                    liberar_bloque_si_no_referenciado(bloqActual, 1/*! Pasar QUERY_ID despues !*/);
                    free(nuevoBloqPath);
                }
                free(bloqFis);
            }else{
                //CASO CONTENIDO ÚNICO
                //Para este punto conviene que ancho y pathHash sean variable Global
                char Bloque[256];
                int anchoEntrada = calcularAncho();
                sprintf(Bloque,"block%0*d", anchoEntrada, bloqActual);
                char path_blocks_hash[256];
                sprintf(path_blocks_hash, "%s/blocks_hash_index.config", config_struct->punto_montaje);
                //Abrimos en modo append (a) para no borrar
                FILE *archHash = fopen(path_blocks_hash, "a");
                if (archHash) {
                    char nuevaEntrada[256];
                    sprintf(nuevaEntrada, "%s=%s",hash,Bloque);
                    fputs(nuevaEntrada, archHash);
                    fclose(archHash);
                } else {
                    log_error(loggerStorage, "Fallo al abrir blocks_hash_index.config en modo append");
                }
            }
            pthread_mutex_unlock(&mutex_hash_index);
            free(bloqLogPath);
            free(hash);
        }
        tag->estado = COMMITED;
        
        t_metadata *meta = leer_metadata(nombreArch, nombreTag);
        meta->estado = strdup("COMMITED");
        meta->blocks = list_duplicate(tag->physicalBlocks);
        guardar_metadata(meta, nombreArch, nombreTag);
        destruir_metadata(meta); //elimina memory leak?
        log_info(loggerStorage, "##%d - Commit de File:Tag %s:%s", 1/*query_id!!!!*/, nombreArch, nombreTag);
        pthread_mutex_unlock(&tag->mutexTag);
        return true;
    }
    return false;
}

char* leer_contenido_bloque(char* path_bloque_logico) {
    // Reservamos memoria para el contenido del bloque
    char* contenido = malloc(tam_bloq);
    if (contenido == NULL) {
        log_error(loggerStorage, "Error al reservar memoria para el contenido del bloque.");
        return NULL;
    }

    // Abrimos el hardlink que referencia al bloq fis

    FILE* arcBloque = fopen(path_bloque_logico, "r");
    if (arcBloque == NULL) {
        log_error(loggerStorage, "Error al abrir bloque lógico %s: %s", path_bloque_logico, strerror(errno));
        free(contenido);
        return NULL;
    }

    size_t bytes_leidos = fread(contenido, 1, tam_bloq, arcBloque);
    fclose(arcBloque);

    // Verificación opcional, pero puede servir si el archivo no está lleno
    if (bytes_leidos != tam_bloq) {
        log_warning(loggerStorage, "Lectura parcial. Leídos %zu de %d bytes en %s.", bytes_leidos, tam_bloq, path_bloque_logico);
        //Lo de zu es z por el tipo de size_t y u porque es unsigned!
        // Rellenamos el resto del buffer con ceros
        memset(contenido + bytes_leidos, 0, tam_bloq - bytes_leidos);
    }
    return contenido;
}

void liberar_bloque_si_no_referenciado(int bloque_fisico, int query_id) {
    char* path_bloque_fisico = obtener_path_bloque_fisico(bloque_fisico);
    struct stat st;
    if (stat(path_bloque_fisico, &st) == 0) {
        // Si es <= 1, SÓLO el archivo físico en physical_blocks lo referencia (o ya fue eliminado).
        if (st.st_nlink <= 1) { 
            pthread_mutex_lock(&mutex_bitmap);
            marcar_libre_en_bitmap(bloque_fisico);
            pthread_mutex_unlock(&mutex_bitmap);
            
            //OPCIONAL: Podríamos no crear todos los bloques fisicos
            //al inicio porque ocupamos mucha memoria, e irlos creando
            //a medida que los piden? En ese caso:
            //unlink(path_bloque_fisico)

            log_info(loggerStorage, "##%d - Bloque Físico Liberado - Número de Bloque: %d", query_id, bloque_fisico);
        }
    }
    free(path_bloque_fisico);
}

bool op_write(char* nombreArch, char *nombreTag, int direccBase, void *contenido){
    usleep(retardo_operacion);
    t_tag *tag = buscar_Tag_Arch(nombreArch,nombreTag);
    if(tag){
        if(tag->estado != COMMITED) {
            int bloqLog = direccBase / tam_bloq;
            int offset = direccBase % tam_bloq;
            log_info(loggerStorage, "## BLOQ LOGICO A ESCRIBIR %d", bloqLog);
            pthread_mutex_lock(&tag->mutexTag);
            int bloqFis = list_get(tag->physicalBlocks, bloqLog);
            pthread_mutex_unlock(&tag->mutexTag);
            log_info(loggerStorage, "## LE CORRESPONDE EL BLOQFIS %d", bloqFis);
            char *pathBloqFis = obtener_path_bloque_fisico(bloqFis);
            char *pathBloqLog = obtener_path_bloq_logico(tag, bloqLog);
            log_info(loggerStorage, "## SE ESCRIBE SOBRE EL bloqLog %s", pathBloqLog);
            //buscar bloque fisico al que esta asociado el link del bloque logico
            struct stat st;
            if (stat(pathBloqFis, &st) == 0) {
                if (st.st_nlink == 2) { //2 referencias, la del archivo original, y la del hardlink
                    //marcar_libre_en_bitmap(bloque_fisico);
                    FILE *bloqL = fopen(pathBloqLog, "r+");
                    if(!bloqL){
                        log_error(loggerStorage, "Error al abrir el bloque lógico '%s' : %s", pathBloqLog, strerror(errno));
                        return false;
                    }
                    fseek(bloqL, offset, SEEK_SET);
                    fwrite(contenido, 1, strlen(contenido), bloqL);
                    fclose(bloqL);
                    free(pathBloqLog);
                    return true;
                }else{
                    unlink(pathBloqLog);
                    //marcar_libre_en_bitmap(bloqFis);
                    //repensar esto
                    int nuevoBloqFis = buscar_bloque_libre();
                    //list_remove(tag->physicalBlocks, bloqLog);
                    list_add_in_index(tag->physicalBlocks, bloqLog, nuevoBloqFis);
                    //actualizamos metaActual
                    char *pathBloqFis = obtener_path_bloque_fisico(nuevoBloqFis);
                    link(pathBloqLog, pathBloqFis);
                    log_info(loggerStorage, "##<QUERY_ID> - %s:%s Se agregó el hard link del bloque lógico %06d al bloque físico %06d", nombreArch, nombreTag, bloqLog, nuevoBloqFis);
                    usleep(retardo_acceso_bloque);
                    FILE *bloqL = fopen(pathBloqFis, "r+");
                    if(!bloqL){
                        log_error(loggerStorage, "Error al abrir el bloque lógico '%s' : %s", pathBloqFis, strerror(errno));
                        return false;
                    }
                    fseek(bloqL, offset, SEEK_SET);
                    fwrite(contenido, 1, strlen(contenido), bloqL);
                    fclose(bloqL);
                    free(pathBloqLog);
                    free(pathBloqFis);
                    return true;
                    //faltan frees y falta mejor implementacion de bloques logicos
                }
            }
        }
    }
}

bool op_read(char* nombreArch, char *nombreTag, int nroBloque, char *contenido){
    usleep(retardo_operacion);
    t_tag *tag = buscar_Tag_Arch(nombreArch, nombreTag);
    char *pathBloq = obtener_path_bloq_logico(tag,nroBloque);
    if(!(contenido = leer_contenido_bloque(pathBloq))){
        log_info(loggerStorage, "##<QUERY_ID> - Bloque Lógico Leído <%s>:<%s> - Número de Bloque: <%d>",nombreArch,nombreTag,nroBloque);
        free(pathBloq);
        return true;
    }else{
        free(pathBloq);
        return false;
    }
    
}


void crear_copia_tag(char* nombreArch,t_tag *tagOrigen, char *nombreNuevoTag){
    t_fcb *arch = dictionary_get(diccionario_archivos,nombreArch);
    t_tag *nuevoTag = crear_tag(nombreNuevoTag,nombreArch,arch->tags);
    //REVISAR ESTRUCTURAS ADMINISTRATIVAS PARA TAG Y METADATA
    nuevoTag->tamanio = tagOrigen->tamanio;
    nuevoTag->estado = WORK_IN_PROGRESS;
    nuevoTag->logBlocks = tagOrigen->logBlocks;
    nuevoTag->physicalBlocks = list_duplicate (tagOrigen->physicalBlocks);
    t_metadata *meta = leer_metadata(nombreArch, nombreNuevoTag);

    meta->estado = strdup("WORK_IN_PROGRESS");
    guardar_metadata(meta, nombreArch, nombreNuevoTag);
    destruir_metadata(meta);
}

void marcar_libre_en_bitmap(int nro_fisico) {
    if (nro_fisico <= 0 || nro_fisico >= cantBloq)
        return;
    bitarray_clean_bit(bitarray, nro_fisico);
    size_t bytes_bitmap = (cantBloq + 7) / 8;
    msync(mappeo, bytes_bitmap, MS_SYNC);
    log_info(loggerStorage, "## Bloque Físico Liberado - Número de Bloque: %d", nro_fisico);
}

void marcar_ocupado_en_bitmap(int nro_fisico) {
    if (nro_fisico < 0 || nro_fisico >= cantBloq)
        return;
    bitarray_set_bit(bitarray, nro_fisico);
    size_t bytes_bitmap = (cantBloq + 7) / 8;
    msync(mappeo, bytes_bitmap, MS_SYNC);
    log_info(loggerStorage, "## Bloque Físico Ocupado - Número de Bloque: %d", nro_fisico);
}

void guardar_metadata(t_metadata* meta, char* archivo, char* nombreTag) {
    char* path_meta = path_Metadata(archivo, nombreTag);
    log_info(loggerStorage, "Abriendo path meta: %s", path_meta);
    // Asegurar que existe el archivo
    /*FILE* f = fopen(path_meta, "r");
    if (!f)
        f = fopen(path_meta, "w");
    if (f)
        fclose(f);*/

    t_config* config = config_create(path_meta);
    if (!config) {
        log_error(loggerStorage, "No se pudo abrir (para guardar) metadata: %s", path_meta);
        free(path_meta);
        return;
    }

    // TAMAÑO
    char tamanio_str[32];
    sprintf(tamanio_str, "%d", meta->tamanio);
    log_info(loggerStorage, "seteando tamanio: %s", tamanio_str);
    config_set_value(config, "TAMAÑO", tamanio_str);
    log_info(loggerStorage, "tamanio seteado");
    // ESTADO
    /*char estado_str[128];
    if(meta->estado == COMMITED){
        sprintf(estado_str, "COMMITED");
    }else{
        sprintf(estado_str, "WORK_IN_PROGRESS");
    }*/
    /*log_info(loggerStorage, "seteando ESTADO: %s",estado_str);
    config_set_value(config, "ESTADO", estado_str);
    log_info(loggerStorage, "Estado seteado");*/
    log_info(loggerStorage, "seteando ESTADO: %s",meta->estado);
    config_set_value(config, "ESTADO", meta->estado);
    log_info(loggerStorage, "Estado seteado");

    // BLOCKS
    // Construir string tipo [17,2,5]
    char buf[4096];
    char tmp[64];
    buf[0] = '\0';
    strcat(buf, "[");

    int n = list_size(meta->blocks);
    /*for (int i = 0; i < n; i++) {
        int* v = list_get(meta->blocks, i);
        sprintf(tmp, "%d", *v);
        strcat(buf, tmp);
        if (i < n - 1) strcat(buf, ",");
    }*/
    for (int i = 0; i < n; i++) {
        int v = list_get(meta->blocks, i);
        sprintf(tmp, "%d", v);
        strcat(buf, tmp);
        if (i < n - 1) strcat(buf, ",");
    }
    strcat(buf, "]");
    config_set_value(config, "BLOCKS", buf);

    config_save(config);
    config_destroy(config);
    free(path_meta);
}

void destruir_metadata(t_metadata* meta) {
    if (!meta)
        return;
    if (meta->estado)
        free(meta->estado);
    if (meta->blocks) {
        //void liberar_int(void* x) { free(x); }
        list_destroy_and_destroy_elements(meta->blocks, free);
        //list_destroy (meta->blocks);
    }
    free(meta);
}

void crear_metadata (char* path, char* nuevoPath) {
    char pathConfig [256];
    sprintf(pathConfig, "%s/metadata.config", path);
    FILE* archivo = fopen(pathConfig, "w+");
    if(!archivo) {
        log_error(loggerStorage, "Error al crear el archivo 'metadata.config': %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    fputs("TAMAÑO=0\n",archivo);
    fputs("BLOCKS=[]\n",archivo);
    fputs("ESTADO=WORK_IN_PROGRESS\n",archivo);

    fclose(archivo);
    if(nuevoPath != NULL){
        strcpy(nuevoPath,pathConfig);
    }
}


//==========ADMINISTRACION DE ARCHIVOS Y TAGS==========
/*char *path_Metadata(char *nombreArch, char *nombreTag){
    return string_from_format("%s/%s/%s/metadata.config", path_files, nombreArch, nombreTag);
}*/
char *path_Metadata(char *nombreArch, char *nombreTag){
    char *metadata = malloc(256); // reservo memoria dinámica
    if (!metadata) return NULL;
    sprintf(metadata, "%s/%s/%s/metadata.config", path_files, nombreArch, nombreTag);
    return metadata;
}

t_fcb *crear_fcb(char *nombreNuevoArch, char *nombreNuevoTag){
    t_fcb *fcb = malloc(sizeof(t_fcb));
    fcb->nombreArch = nombreNuevoArch;
    fcb->tags = dictionary_create();
    crear_tag(nombreNuevoTag,nombreNuevoArch, fcb->tags);
    dictionary_put(diccionario_archivos,fcb->nombreArch,fcb);
    return fcb;
}

t_tag *crear_tag(char *nombreNuevoTag, char *nombreArch,t_dictionary *diccionarioTagsArch){
    t_tag *tag = malloc(sizeof(t_tag));
    char *pathNuevoTag = malloc(256); 
    sprintf(pathNuevoTag, "%s/%s/%s", path_files, nombreArch, nombreNuevoTag);
    tag->pathTag = pathNuevoTag;
    log_info(loggerStorage,"Nuevo Path Tag = %s", tag->pathTag );
    tag->nombreTag = nombreNuevoTag;
    tag->tamanio = 0;
    tag->physicalBlocks = list_create();
    tag->logBlocks = 0;
    tag->estado = WORK_IN_PROGRESS;
    dictionary_put(diccionarioTagsArch, tag->nombreTag, tag);
    return tag;
}

t_tag *buscar_Tag_Arch(char *Arch, char *Tag){
    t_fcb *fcb = dictionary_get(diccionario_archivos, Arch);
    if(!fcb){
        log_info(loggerStorage, "Error : Archivo No Encontrado : %s",Arch);
        return NULL;
    }
        log_info(loggerStorage, "Error : Archivo No Encontrado : %s",Arch);
        return NULL;
    }
        eturn tag;
}

bool archRepetido(char *nombreArch){
    if(dictionary_has_key(diccionario_archivos,nombreArch)){
        log_info(loggerStorage, "Error : Nombre de Archivo Repetido : %s",nombreArch);
        return true;
    }else{
        return false;
    }
}

bool tagRepetido(char *nombreArch, char *nombreTag){
    t_fcb *fcb = dictionary_get(diccionario_archivos, nombreArch);
    if(dictionary_has_key(fcb->tags,nombreTag)){
        log_info(loggerStorage, "Error : Nombre de Tag Repetido : %s | %s",nombreArch, nombreTag);
        return true;
    }else{
        return false;
    }
}

/*void crear_bloq_log(t_tag *tag,char *bloq_fis){
    int nroBloqLog = list_size(tag->physicalBlocks);
    char bloqLog[256];
    sprintf(bloqLog, "%06d.dat",nroBloqLog);
    char *pathBlockLog = malloc(256);
    sprintf(pathBlockLog, "%s/logical_blocks/%s", tag->pathTag,bloqLog);
    log_info(loggerStorage,"Nuevo Bloq Log = %s", pathBlockLog);
    FILE *bloqL = fopen(pathBlockLog, "w+");
    ftruncate(fileno(bloqL), tam_bloq);
    link(bloq_fis,pathBlockLog);
    fclose(bloqL);
    free(pathBlockLog);
    list_add(tag->logBlocks,nroBloqLog);
}*/

//elimina su ultimo bloq_log
/*
void eliminar_bloq_log(t_tag *tag){
    if(list_size(tag->logBlocks) > 0){
    int nroBloqLog = (list_size(tag->logBlocks) - 1); //representa el ultimo bloque logico
    char *pathBlockLog  = obtener_path_bloq_logico(tag, nroBloqLog);
    log_info(loggerStorage,"Bloq Log a Eliminar= %s", pathBlockLog);
    char cmd[512];
    sprintf(cmd, "rm -rf %s",pathBlockLog);
    system(cmd);
    list_remove(tag->logBlocks,nroBloqLog);
    free(pathBlockLog);
    }else{
        log_info(loggerStorage,"No hay bloque logico que eliminar");
    }
}
*/
/*void crear_bloq_log(t_tag *tag,char *bloq_fis){
    int nroBloqLog = list_size(tag->logBlocks); //representa el siguiente bloque logico a crear
    char *pathBlockLog  = obtener_path_bloq_logico(tag, nroBloqLog);
    log_info(loggerStorage,"Nuevo Bloq Log = %s", pathBlockLog);
    FILE *bloqL = fopen(pathBlockLog, "w+");
    ftruncate(fileno(bloqL), tam_bloq);
    link(bloq_fis,pathBlockLog);
    fclose(bloqL);
    free(pathBlockLog);
    list_add(tag->logBlocks,nroBloqLog);
}*/

char *obtener_path_bloq_logico(t_tag *tag, int nroBloqLog){
    char bloqLog[256];
    sprintf(bloqLog, "%06d.dat",nroBloqLog);
    char *pathBlockLog = malloc(256);
    sprintf(pathBlockLog, "%s/logical_blocks/%s", tag->pathTag,bloqLog);
    return pathBlockLog;
}