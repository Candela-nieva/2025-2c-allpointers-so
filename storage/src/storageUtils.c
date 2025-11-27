#include "storageUtils.h"
//Añadir qids
int retardo_operacion;
int retardo_acceso_bloque;
bool fresh_start;
int fs_size;
int tam_bloq;
int cantBloq;
int cant_workers = 0;
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
pthread_mutex_t mutex_diccionario_archivos;
//==========INICIALIZACION==========
//prueba

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

// Liberar un TAG completo (Nombre, Path, Mutex, Lista de Bloques)
void destruir_tag_item(void* elemento) {
    t_tag* tag = (t_tag*)elemento;
    if (tag) {
        if (tag->nombreTag) free(tag->nombreTag);
        if (tag->pathTag) free(tag->pathTag);
        
        // Liberar la lista de bloques físicos
        if (tag->physicalBlocks) {
            list_destroy_and_destroy_elements(tag->physicalBlocks, free);
        }
        
        pthread_mutex_destroy(&tag->mutexTag);
        free(tag);
    }
}

// Liberar un FCB completo (Nombre y Diccionario de Tags)
void destruir_fcb_item(void* elemento) {
    t_fcb* fcb = (t_fcb*)elemento;
    if (fcb) {
        if (fcb->nombreArch) free(fcb->nombreArch);
        
        if (fcb->tags) {
            dictionary_destroy_and_destroy_elements(fcb->tags, destruir_tag_item);
        }
        free(fcb);
    }
}

void terminar_programa_storage(int signal) {
    log_info(loggerStorage, "Finalizando Storage correctamente...");
    if (configHash) config_destroy(configHash);
    if (config) config_destroy(config);
    if (config_SB) config_destroy(config_SB);
    if (config_struct) free(config_struct);
    if (config_superBlock) free(config_superBlock);
    pthread_mutex_lock(&mutex_bitmap);
    if(bitarray)
        bitarray_destroy(bitarray);
    pthread_mutex_unlock(&mutex_bitmap);
    
    pthread_mutex_lock(&mutex_hash_index);
    if (mappeo) {
        // Liberar la memoria mapeada
        // Usamos cantBloq/8 porque es el tamaño en bytes
        munmap(mappeo, cantBloq / 8); 
    }
    pthread_mutex_unlock(&mutex_hash_index);
    pthread_mutex_lock(&mutex_diccionario_archivos);
    if(diccionario_archivos)
        dictionary_destroy_and_destroy_elements(diccionario_archivos, destruir_fcb_item);
    pthread_mutex_unlock(&mutex_diccionario_archivos);
    pthread_mutex_destroy(&mutex_bitmap);
    pthread_mutex_destroy(&mutex_hash_index);
    pthread_mutex_destroy(&mutex_diccionario_archivos);
    if(loggerStorage) log_destroy(loggerStorage);

    exit(0);
}


//==========CONEXIONES==========

t_worker *registrarWorker(int fd_conexion, int idWorker){
    t_worker *nuevoWorker = malloc(sizeof(t_worker));
    nuevoWorker->socket = fd_conexion;
    nuevoWorker->ID_Worker = idWorker;
    return nuevoWorker;
}

void darDeBajaWorker(t_worker *aEliminar){
    close(aEliminar->socket);
    free(aEliminar);
}

void iniciar_servidor_multihilo(void)
{
    int fd_sv = crear_servidor(config_struct->puerto_escucha);
    log_info(loggerStorage, "Servidor STORAGE escuchando en puerto %s", config_struct->puerto_escucha);
    while (1)
    {   
        //int *fd_conexion = malloc(sizeof(int));
        //*fd_conexion = esperar_cliente(fd_sv, "STORAGE", loggerStorage);
        int fd_conexion = esperar_cliente(fd_sv, "STORAGE", loggerStorage);
        int operacion = recibir_operacion(fd_conexion);
        if(operacion == HANDSHAKE_WORKER){
            ++cant_workers;
            int idWorker;
            void *buffer = recibir_buffer(fd_conexion);
            memcpy(&idWorker, buffer,sizeof(int));
            free(buffer);
            // LOG OBLIGATORIO //
            log_info(loggerStorage, "##Se conecta el Worker <%d> - Cantidad de Workers: <%d>", idWorker,cant_workers);
            
            t_worker *nuevoWorker = registrarWorker(fd_conexion,idWorker);

            t_paquete* paquete = crear_paquete(ENVIAR_TAMANIO_BLOQUE);
            agregar_a_paquete(paquete, &tam_bloq, sizeof(int));
            enviar_paquete(paquete, fd_conexion);
            eliminar_paquete(paquete);
            //close(fd_conexion); // NUEVO: cierro si no voy a atender más
            pthread_t hilo_worker;
            pthread_create(&hilo_worker, NULL, atender_worker, nuevoWorker);
            pthread_detach(hilo_worker);
        } else {
            log_info(loggerStorage, "Operacion desconocida. Cerrando conexion.");
            close(fd_conexion);
        }
    }
    // Nunca llega acá
    close(fd_sv);
    return;
}

void* atender_worker(void* arg){
    t_worker *worker = (t_worker *)arg;
    //int qid;
    //free(arg);
    while (1)
    {
        log_info(loggerStorage, "##Se escuchan peticiones de Worker %d", worker->ID_Worker);
        op_storage inst = recibir_operacion(worker->socket);
        log_info(loggerStorage, "##Instruccion recibida %d", inst);
        if(inst == -1) {
            cant_workers--;
            // LOG OBLIGATORIO //
            log_info(loggerStorage, "##Se desconecta el Worker %d - Cantidad de Workers: %d", worker->ID_Worker, cant_workers);
            darDeBajaWorker(worker);
            return NULL;
        }
        void* buffer = recibir_buffer(worker->socket);
        if(buffer == NULL)
            continue;
    
        switch(inst){
            case CREATE_FILE:
                log_info(loggerStorage,"Operacion Recibida - CREATE : %d",inst);
                atenderCreate(worker->socket, buffer);
                break;
            case TRUNCATE_FILE:
                log_info(loggerStorage,"Operacion Recibida - TRUNCATE : %d",inst);
                atenderTruncate(worker->socket,buffer);
                break;
            case FILE_TAG:
                log_info(loggerStorage,"Operacion Recibida - TAG : %d",inst);
                atenderTag(worker->socket,buffer);
                break;
            case COMMIT_TAG:
                log_info(loggerStorage,"Operacion Recibida - COMMIT : %d",inst);
                atenderCommit(worker->socket,buffer);
                break;
            case WRITE_BLOCK:
                log_info(loggerStorage,"Operacion Recibida - WRITE : %d",inst);
                atenderWrite(worker->socket,buffer);
                break;
            case READ_BLOCK:
                log_info(loggerStorage,"Operacion Recibida - READ : %d",inst);
                atenderRead(worker->socket,buffer);
                break;
            case DELETE_TAG:
                log_info(loggerStorage,"Operacion Recibida - DELETE : %d",inst);
                atenderDelete(worker->socket,buffer);
                break;
            default:
                log_info(loggerStorage,"Fallo : Operacion Desconocida : %d", inst);
                break;
        }
    }
    // Nunca llega acá
    close(worker->socket);
    return NULL;
}

//========== CASOS DE ATENCION ==========

void atenderCreate(int fd_conexion, void* buffer){
    int QID;
    char *nombreArch, *nombreTag;
    recibir_QID_nombreArch_nombreTag(buffer, &QID, &nombreArch, &nombreTag);

    t_motivo resultado = op_create(nombreArch, nombreTag, QID);
    free(buffer);
    
    enviar_operacion(fd_conexion, resultado);

    if(nombreArch)
        free(nombreArch);
    if(nombreTag)
        free(nombreTag);
}

void atenderTruncate(int fd_conexion, void* buffer){
    int QID, nuevoTam;
    char *nombreArch; 
    char *nombreTag;

    int offset = recibir_QID_nombreArch_nombreTag(buffer, &QID, &nombreArch, &nombreTag);

    memcpy(&nuevoTam, buffer + offset, sizeof(int));
    free(buffer);

    t_motivo resultado = op_truncate(nombreArch, nombreTag, nuevoTam, QID);
    enviar_operacion(fd_conexion, resultado);

    if(nombreArch)
        free(nombreArch);
    if(nombreTag)
        free(nombreTag);
}

void atenderTag(int fd_conexion, void* buffer){
    int QID, tamArchDestino,tamNuevoTag;
    char *nombreArch;
    char *nombreTag;
    char *nombreNuevoTag;
    char *archDestino;
    int offset = recibir_QID_nombreArch_nombreTag(buffer,&QID, &nombreArch, &nombreTag);

    memcpy(&tamArchDestino,buffer + offset, sizeof(int));
    offset += sizeof(int);
    archDestino = malloc(tamArchDestino + 1);
    memcpy(archDestino,buffer + offset, tamArchDestino);
    archDestino[tamArchDestino] = '\0';
    offset += tamArchDestino;
    memcpy(&tamNuevoTag,buffer + offset, sizeof(int));
    offset += sizeof(int);
    nombreNuevoTag = malloc(tamNuevoTag + 1);
    memcpy(nombreNuevoTag,buffer + offset, tamNuevoTag);
    nombreNuevoTag[tamNuevoTag] = '\0';
    free(buffer);
    //enviar_operacion(fd_conexion, resultado); 
    t_motivo resultado = op_tag(nombreArch, nombreTag, archDestino,nombreNuevoTag,QID);
    enviar_operacion(fd_conexion,resultado);
    if(archDestino)
        free(archDestino);
    if(nombreNuevoTag)
        free(nombreNuevoTag);
    if(nombreArch)
        free(nombreArch);
    if(nombreTag)
        free(nombreTag);
}

void atenderCommit(int fd_conexion, void* buffer){
    int QID;
    char *nombreArch;
    char *nombreTag;

    recibir_QID_nombreArch_nombreTag(buffer,&QID, &nombreArch, &nombreTag);
    free(buffer);

    t_motivo resultado = op_commit(nombreArch, nombreTag, QID);
    enviar_operacion(fd_conexion, resultado); 
    if(nombreArch)
        free(nombreArch);
    if(nombreTag)
        free(nombreTag);
}

void atenderWrite(int fd_conexion, void* buffer){
    int QID, nroBloqueLogico, tamCont;
    char *nombreArch;
    char *nombreTag;
    char *contenido;
    //void *contenido;

    int offset = recibir_QID_nombreArch_nombreTag(buffer,&QID, &nombreArch, &nombreTag);
    memcpy(&nroBloqueLogico,buffer + offset, sizeof(int));
    offset += sizeof(int);
    memcpy(&tamCont, buffer + offset, sizeof(int));
    offset += sizeof(int);
    contenido = malloc(tamCont + 1);
    memcpy(contenido, buffer + offset, tamCont);
    contenido[tamCont] = '\0';
    free(buffer);

    t_motivo resultado = op_write_block(nombreArch, nombreTag, nroBloqueLogico, contenido, QID);
    enviar_operacion(fd_conexion,resultado);
    if(nombreArch)
        free(nombreArch);
    if(nombreTag)
        free(nombreTag);
    if(contenido)
        free(contenido);
}

void atenderRead(int fd_conexion, void* buffer){
    int QID, nroBloq;
    char *nombreArch;
    char *nombreTag; 
    char *contenido=NULL;

    int offset = recibir_QID_nombreArch_nombreTag(buffer,&QID, &nombreArch, &nombreTag);
    memcpy(&nroBloq,buffer + offset, sizeof(int));
    free(buffer);
    
    t_motivo resultado = op_read_block(nombreArch, nombreTag, nroBloq,&contenido,QID);
    t_paquete *paquete = crear_paquete(resultado);
    if(resultado == RESULTADO_OK) {
        agregar_a_paquete_string(paquete, contenido, tam_bloq);
        enviar_paquete(paquete, fd_conexion);
        eliminar_paquete(paquete);
    } else {
        enviar_paquete(paquete, fd_conexion);
        eliminar_paquete(paquete);
    }
    
    if(nombreArch)
        free(nombreArch);
    if(nombreTag)
        free(nombreTag);
    if(contenido)
        free(contenido);
}

void atenderDelete(int fd_conexion, void* buffer){
    //int offset = 0;
    int QID;
    char *nombreArch;
    char *nombreTag;

    recibir_QID_nombreArch_nombreTag(buffer,&QID, &nombreArch, &nombreTag);
    free(buffer);
    log_info(loggerStorage, "Atendiendo DELETE de %s:%s", nombreArch, nombreTag);
    t_motivo resultado = op_delete_tag(nombreArch, nombreTag, QID);
    enviar_operacion(fd_conexion,resultado);

    if(nombreArch)
        free(nombreArch);
    if(nombreTag)
        free(nombreTag);
}

int recibir_QID_nombreArch_nombreTag(void* buffer, int* QID, char** nombreArch,char** nombreTag){
    int archLen, tagLen;
    int offset = 0;

    memcpy(QID, buffer + offset, sizeof(int));
    offset += sizeof(int);

    memcpy(&archLen, buffer + offset, sizeof(int));
    offset += sizeof(int);

    *nombreArch = malloc(archLen + 1);
    memcpy(*nombreArch,buffer + offset, archLen);
    (*nombreArch)[archLen] = '\0';
    offset += archLen;

    memcpy(&tagLen, buffer + offset, sizeof(int));
    offset += sizeof(int);

    *nombreTag = malloc(tagLen + 1);
    memcpy(*nombreTag, buffer + offset, tagLen);
    (*nombreTag)[tagLen] = '\0';
    offset += tagLen;

    // Devolvemos el offset final para que el buffer
    // restante pueda ser leído
    return offset;
}
//========== FRESH_START ==========

void inicializar_montaje(){
    diccionario_archivos = dictionary_create();
    inicializar_semaforos();
    cargar_config_superBlock();
    freshStart();
    cargar_config_hashIndex();
    
    if(fresh_start)
        initialFile();
    log_info(loggerStorage, "SE ABRIO EL DIRECTORIO RAIZ : FS SIZE = %d ; BLOCK SIZE = %d",fs_size,tam_bloq);
}

void inicializar_semaforos() {
    pthread_mutex_init(&mutex_hash_index, NULL);
    pthread_mutex_init(&mutex_bitmap, NULL);
    pthread_mutex_init(&mutex_diccionario_archivos, NULL);
}

void cargar_config_hashIndex(){
    if (configHash != NULL) {
        config_destroy(configHash); // Liberar si ya existía
    }
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
    

    tam_bloq = atoi(config_superBlock->tam_bloq);
    cantBloq = fs_size / tam_bloq; //al ser un bitmap, cada entrada es de 1 bit, por lo que el tamanio es igual a cantBloques bits
    log_info(loggerStorage,"CantBloques: %d", cantBloq);

    sprintf(path_blocks, "%s/physical_blocks", config_struct->punto_montaje);
    sprintf(path_files, "%s/files", config_struct->punto_montaje);
    
}

void freshStart(){
    verificar_freshStart();
    if(fresh_start)
        formateo();
    else
        recuperar_estructuras_FS();
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

void recuperar_estructuras_FS() {
    log_info(loggerStorage, "Iniciando recuperación de FileSystem existente...");
    DIR *dir_files = opendir(path_files);
    if (dir_files == NULL) {
        log_error(loggerStorage, "No se pudo abrir el directorio de files para recuperación.");
        return;
    }

    struct dirent *entrada_archivo;
    while ((entrada_archivo = readdir(dir_files)) != NULL) {
        if (strcmp(entrada_archivo->d_name, ".") == 0 || strcmp(entrada_archivo->d_name, "..") == 0)
            continue;

        char* nombre_archivo = entrada_archivo->d_name;
        
        t_fcb *fcb = malloc(sizeof(t_fcb));
        fcb->nombreArch = strdup(nombre_archivo);
        fcb->tags = dictionary_create();
        dictionary_put(diccionario_archivos, fcb->nombreArch, fcb);
        
        //log_info(loggerStorage, "Recuperado FCB: %s", nombre_archivo);

        recuperar_tags_file(nombre_archivo, fcb);
    }
    free(entrada_archivo);
    closedir(dir_files);
    recargar_bitmap();
    log_info(loggerStorage, "Recuperación finalizada.");
    
}

void recuperar_tags_file(char *nombre_archivo, t_fcb *file){
    char path_archivo_completo[512];
    sprintf(path_archivo_completo, "%s/%s", path_files, nombre_archivo);
        
    DIR *dir_tags = opendir(path_archivo_completo);
    if (dir_tags == NULL){
        log_error(loggerStorage, "No se pudo abrir el directorio de tag para recuperación.");
        return;
    }
    struct dirent *entrada_tag;
    while ((entrada_tag = readdir(dir_tags)) != NULL) {
        if (strcmp(entrada_tag->d_name, ".") == 0 || strcmp(entrada_tag->d_name, "..") == 0)
            continue;
        char* nombre_tag = entrada_tag->d_name;
        t_metadata* meta = leer_metadata(nombre_archivo, nombre_tag);
        if (!meta) {
            log_error(loggerStorage, "Metadata corrupta o faltante en %s:%s", nombre_archivo, nombre_tag);
            continue;
        }
        t_tag *tag = crear_tag(nombre_tag, nombre_archivo, file->tags);
        //t_tag *tag = malloc(sizeof(t_tag));
        //tag->nombreTag = strdup(nombre_tag);
        //tag->pathTag = malloc(512);
        //sprintf(tag->pathTag, "%s/%s/%s", path_files, nombre_archivo, nombre_tag);    
        //tag->tamanio = meta->tamanio;

        //t_tag *tag = malloc(sizeof(t_tag));
        //char *pathNuevoTag = malloc(256); 
        //sprintf(pathNuevoTag, "%s/%s/%s", path_files, nombre_archivo, nombre_tag);
        //tag->pathTag = pathNuevoTag;
        //log_info(loggerStorage,"Nuevo Path Tag = %s", tag->pathTag);
        //tag->nombreTag = strdup (nombre_tag);
        //log_info(loggerStorage,"Nuevo Nombre Tag = %s", tag->nombreTag);
        tag->tamanio = meta->tamanio;
        if (strcmp(meta->estado, "COMMITED") == 0) 
            tag->estado = COMMITED;
        else 
            tag->estado = WORK_IN_PROGRESS;
        
        tag->physicalBlocks = list_create();
        for(int i=0; i < list_size(meta->blocks); i++) {
            int* ptr_meta = list_get(meta->blocks, i);
            int* ptr_tag = malloc(sizeof(int));
            *ptr_tag = *ptr_meta;
            list_add(tag->physicalBlocks, ptr_tag);
        }

        tag->logBlocks = list_size(tag->physicalBlocks); 
        //dictionary_put(file->tags,tag->nombreTag,tag);
        //log_info(loggerStorage, "  -> Recuperado Tag: %s (Size: %d, Blocks: %d)", 
        //        nombre_tag, tag->tamanio, tag->logBlocks);

        destruir_metadata(meta);
    }
    free(entrada_tag);
    closedir(dir_tags);
}

void recargar_bitmap() {
    char pathBitmap[256];
    sprintf(pathBitmap, "%s/bitmap.bin", config_struct->punto_montaje);
    
    // MODO "r+b" (Lectura/Escritura binaria, SIN truncar/borrar)
    FILE* archBitmap = fopen(pathBitmap, "r+b"); 
    
    if (!archBitmap) {
        // Si falla (no existe), lo creamos de cero por seguridad
        log_warning(loggerStorage, "Bitmap no encontrado en recuperación. Creando uno nuevo...");
        crear_bitmap(); 
        return;
    }

    int fildes = fileno(archBitmap);
    
    mappeo = mmap(NULL, cantBloq/8, PROT_READ | PROT_WRITE, MAP_SHARED, fildes, 0);
    if (mappeo == MAP_FAILED) {
        log_info(loggerStorage, "El mapeo con el bitmap falló");
        exit(EXIT_FAILURE);
    }
    
    bitarray = bitarray_create_with_mode(mappeo, cantBloq/8, LSB_FIRST);
    fclose(archBitmap);
    
    log_info(loggerStorage, "Bitmap recargado desde disco.");
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

void crear_physical_blocks() {
    int anchoEntrada = calcularAncho();
    char nombreArch[512];
    char nroBloque[32];

    void* buffer_vacio = calloc(1, tam_bloq);

    for(int i=0; i < cantBloq; i++){
        sprintf(nroBloque,"%0*d", anchoEntrada, i);
        sprintf(nombreArch, "%s/block%s.dat", path_blocks, nroBloque);
        //snprintf(nombreArch, sizeof(nombreArch),"%s/block%s.dat", path_blocks, nroBloque);
        FILE *archBloque = fopen(nombreArch, "w+");
        if (!archBloque) {
            log_error(loggerStorage, "Error al crear '%s': %s", nombreArch, strerror(errno));
            free(buffer_vacio);
            exit(EXIT_FAILURE);
        }
        fwrite(buffer_vacio, 1, tam_bloq, archBloque);
        //ftruncate(fileno(archBloque), tam_bloq);
        fclose(archBloque);
    }
    free(buffer_vacio);
}

void initialFile(){
    op_create("initial_file","BASE", 0);
    op_truncate("initial_file","BASE", tam_bloq, 0);
    char escrituraInicial[tam_bloq + 1];
    memset(escrituraInicial, '0', (tam_bloq));
    escrituraInicial[tam_bloq] = '\0';
    op_write_block("initial_file","BASE", 0,escrituraInicial, 0);
    op_commit("initial_file", "BASE", 0);

    
    //char escrituraInicial[tam_bloq + 1];
    //memset(escrituraInicial, '0', (tam_bloq));
    //escrituraInicial[tam_bloq] = '\0';
    //op_write_block("initial_file","BASE", 0,escrituraInicial, 0);
    //char *contALeer;
    //op_read_block("initial_file","BASE", 0,&contALeer, 0);
    //free(contALeer);
}

//==========BITMAP==========
int buscar_bloque_libre(int query_id){
    for(int i = 0; i < cantBloq; i++){
        if(bitarray_test_bit(bitarray, i)==0){
            // LOG OBLIGATORIO //
            log_info(loggerStorage, "##%d - Bloque Físico Reservado - Número de Bloque: %d", query_id, i);
            marcar_ocupado_en_bitmap(i);
            return i;
        }
            
    }
    log_info(loggerStorage, "##%d - No se encontro un Bloque Físico Libre", query_id);
    return -1;
}

void marcar_libre_en_bitmap(int nro_fisico, int query_id) {
    if (nro_fisico <= 0 || nro_fisico >= cantBloq)
        return;
    bitarray_clean_bit(bitarray, nro_fisico);
    size_t bytes_bitmap = (cantBloq + 7) / 8;
    msync(mappeo, bytes_bitmap, MS_SYNC);
    // LOG OBLIGATORIO //
    log_info(loggerStorage, "##%d - Bloque Físico Liberado - Número de Bloque: %d", query_id, nro_fisico);
}

void marcar_ocupado_en_bitmap(int nro_fisico) {
    if (nro_fisico < 0 || nro_fisico >= cantBloq)
        return;
    bitarray_set_bit(bitarray, nro_fisico);
    size_t bytes_bitmap = (cantBloq + 7) / 8;
    msync(mappeo, bytes_bitmap, MS_SYNC);
    log_info(loggerStorage, "## Bloque Físico Ocupado - Número de Bloque: %d", nro_fisico);
}

//==========FORMATO DE LAS ENTRADAS==========

int calcularAncho(){
    int ancho = 1;
    int aux = cantBloq - 1;

    while(aux >= 10){
        ancho++;
        aux /= 10;
    }
    
    //log_info(loggerStorage,"CANT CIFRAS = %d", ancho);
    return ancho;
}

//========== MANEJO DE BLOQUES FISICOS Y BLOQUES LOGICOS ==========
char *obtener_path_bloque_fisico(int nroBloque){
    char Bloque[256];
    int anchoEntrada = calcularAncho();
    sprintf(Bloque,"%0*d", anchoEntrada, nroBloque);
    char *pathBloq = malloc(256);
    sprintf(pathBloq, "%s/block%s.dat", path_blocks, Bloque);
    //log_info(loggerStorage, "path del bloque fisico %d : %s",nroBloque,pathBloq);
    return pathBloq;
}

char *obtener_path_bloq_logico(t_tag *tag, int nroBloqLog){
    char bloqLog[256];
    sprintf(bloqLog, "%06d.dat",nroBloqLog);
    char *pathBlockLog = malloc(256);
    sprintf(pathBlockLog, "%s/logical_blocks/%s", tag->pathTag,bloqLog);
    return pathBlockLog;
}

char* crear_bloq_log(char* pathTag, t_metadata *meta,int nro){
    int* cero = malloc(sizeof(int));
    *cero = 0;
    list_add(meta->blocks, cero);
    //list_add(meta->blocks, 0);
    char nombreBloq[32];
    sprintf(nombreBloq, "%06d.dat", nro);

    char *path_logical = malloc(256);
    sprintf(path_logical, "%s/logical_blocks/%s", pathTag, nombreBloq);

    return path_logical;
}

void eliminar_bloq_log (char* pathTag, int nro) {
    // eliminar hardlink lógico, se puede hacer una funcion xq se repite
    char nombreBloq[32];
    sprintf(nombreBloq, "%06d.dat", nro);
    char path_logical[256];
    sprintf(path_logical, "%s/logical_blocks/%s", pathTag, nombreBloq);
    unlink(path_logical);
}


//==========OPERACIONES==========

t_motivo op_create(char *nombreArch, char *nombreTag, int query_id){
    usleep(retardo_operacion * 1000);
    if(archRepetido(nombreArch)){
        log_info(loggerStorage, "##%d - Fallo : File preexistente %s:%s", query_id,nombreArch, nombreTag);
        return ERROR_FILE_PREEXISTENTE;
    }
    char initial[256];
    crear_directorio(path_files, nombreArch, initial);
    // LOG OBLIGATORIO //
    log_info(loggerStorage, "##%d - File Creado %s:%s", query_id, nombreArch, nombreTag);
    char tagBase[256];
    crear_directorio(initial, nombreTag, tagBase);
    crear_metadata(tagBase,NULL);
    char logicalBlocks[256];
    crear_directorio(tagBase, "logical_blocks", logicalBlocks);
    crear_fcb(nombreArch, nombreTag);
    // LOG OBLIGATORIO //
    log_info(loggerStorage, "##%d - Tag Creado %s:%s", query_id, nombreArch, nombreTag);
    return RESULTADO_OK;
}

t_motivo op_truncate(char* nombreArch, char *nombreTag, int nuevoTamanio, int query_id) {
    usleep(retardo_operacion  * 1000);
    t_metadata* meta = leer_metadata(nombreArch, nombreTag);
    if (!meta) return ERROR_FILE_INEXISTENTE;
    
    int bloques_actuales = meta->tamanio / tam_bloq;
    int bloques_nuevos = nuevoTamanio / tam_bloq;

    t_tag* tag = buscar_Tag_Arch(nombreArch, nombreTag);
    if (!tag) {
        log_info(loggerStorage, "##%d - Tag inexistente %s:%s", query_id,nombreArch, nombreTag);
        destruir_metadata(meta);
        return ERROR_TAG_INEXISTENTE;
    }
    if (tag->estado == COMMITED) {
        log_info(loggerStorage, "##%d - File:Tag %s:%s ya está COMMITED. No hay cambios", query_id, nombreArch, nombreTag);
        destruir_metadata(meta);
        return ERROR_ESCRITURA_NO_PERMITIDA; 
    }
    int ancho = calcularAncho();
    // creo que faltan mutex de tag en esta funcion !!!!!!!!!!!
    if (bloques_nuevos > bloques_actuales) {
        char *path_block0 = obtener_path_bloque_fisico(0);
        for (int i = bloques_actuales; i < bloques_nuevos; i++) {
            t_motivo resultado = agrandarArchivo(meta, tag->pathTag, i, path_block0);
            if(resultado != RESULTADO_OK) {
                free(path_block0);
                destruir_metadata(meta);
                return resultado;
            }
            tag->logBlocks++;
            //list_add(tag->physicalBlocks, (void*)0); //decimos que tiene asociado el bloque 0

            int *bloqInicial = malloc(sizeof(int));
            *bloqInicial = 0;
            list_add(tag->physicalBlocks, bloqInicial);
            // LOG OBLIGATORIO //
            log_info(loggerStorage, "##%d - %s:%s Se agregó el hard link del bloque lógico %06d al bloque físico 0", query_id, nombreArch, nombreTag, i);
        }
        free(path_block0);
    } else if (bloques_nuevos < bloques_actuales) {
        for (int i = bloques_actuales - 1; i >= bloques_nuevos; i--) {

            int *pbloqfis = (int*)list_get(meta->blocks, i);
            int bloque_fisico = *pbloqfis;

            achicarArchivo(meta, tag->pathTag, ancho, i, bloque_fisico, query_id);
            tag->logBlocks--;
            // LOG OBLIGATORIO //
            log_info(loggerStorage, "##%d - %s:%s Se eliminó el hard link del bloque lógico %06d al bloque físico %d", query_id, nombreArch, nombreTag, i, bloque_fisico);
        }
    }

    tag->tamanio = nuevoTamanio;
    meta->tamanio = nuevoTamanio;
    guardar_metadata(meta, nombreArch, nombreTag);
    //esto se tiene que hacer recien en commit ??????????????????????
    // LOG OBLIGATORIO //
    log_info(loggerStorage, "##%d - File Truncado %s:%s - Tamaño: %d", query_id, nombreArch, nombreTag, nuevoTamanio);
    destruir_metadata(meta);
    return RESULTADO_OK;
}

t_motivo agrandarArchivo (t_metadata* meta, char* pathTag, int nro, char* path_block0) {
    char *path_logical = crear_bloq_log(pathTag, meta, nro);
        if (link(path_block0, path_logical) == -1) {
            //log_info(loggerStorage, "Link a block0 falló: %s", strerror(errno));
            //destruir_metadata(meta);
            return ERROR_LINK_FALLIDO;
        }
    free(path_logical);
    return RESULTADO_OK;
}


void achicarArchivo (t_metadata* meta, char* pathTag, int ancho, int nro, int bloque_fisico, int query_id) {
    
    eliminar_bloq_log(pathTag, nro);

    // revisar nlink del físico
    char path_fisico[512];
    sprintf(path_fisico, "%s/block%0*d.dat", path_blocks, ancho, bloque_fisico);
    //stat devuelve las estadisticas de un file mediante el struct st,
    //siendo st_link la cantidad de hardlinks que lo referencian
    //si ya se hizo unlink antes, la cantidad de links deberia ser 0
    struct stat st;
    if (stat(path_fisico, &st) == 0) {
        if (st.st_nlink <= 1 && bloque_fisico != 0) {
            marcar_libre_en_bitmap(bloque_fisico, query_id);
        }
    }

    int* pfis = list_remove(meta->blocks, nro);
    if (pfis)
        free(pfis);
}

t_motivo op_tag(char* nombreArch, char *nombreTagOrigen, char* nombreArchDestino,char *nombreNuevoTag, int query_id){
    usleep(retardo_operacion  * 1000);
    //log_info(loggerStorage, "Operación TAG de %s:%s a %s:%s", nombreArch, nombreTagOrigen, nombreArchDestino, nombreNuevoTag);
    //log_info(loggerStorage, "buscando file: %s", nombreArchDestino);
    t_fcb* fcbDestino = dictionary_get(diccionario_archivos, nombreArchDestino);
    if(!fcbDestino){
        return ERROR_FILE_INEXISTENTE;
    }
    t_tag *tagOrigen = buscar_Tag_Arch(nombreArch,nombreTagOrigen);
    if(tagOrigen){
        char pathArch[512];
        sprintf(pathArch,"%s/%s/%s",path_files,nombreArchDestino,nombreNuevoTag);
        //log_info(loggerStorage, "nuevo path : %s",pathArch);
        char cmd[520];
        sprintf(cmd,"cp -r %s %s",tagOrigen->pathTag,pathArch);
        //log_info(loggerStorage, "comando : %s",cmd);
        system(cmd);
        crear_copia_tag(nombreArchDestino,tagOrigen,nombreNuevoTag);
        return RESULTADO_OK;
    }
    return ERROR_TAG_INEXISTENTE;
}

t_motivo op_commit(char* nombreArch, char *nombreTag, int query_id){
    usleep(retardo_operacion  * 1000);
    t_tag *tag = buscar_Tag_Arch(nombreArch,nombreTag);
    if(tag){
        pthread_mutex_lock(&tag->mutexTag);
        if (tag->estado == COMMITED) {
            pthread_mutex_unlock(&tag->mutexTag);
            log_info(loggerStorage, "##%d - File:Tag %s:%s ya está COMMITED. No hay cambios", query_id,nombreArch, nombreTag);
            return ERROR_ESCRITURA_NO_PERMITIDA; 
        }
        for(int i = 0; i < tag->logBlocks; i++){
            char *bloqLogPath = obtener_path_bloq_logico(tag, i);
            char *contenido = leer_contenido_bloque(bloqLogPath);
            if(contenido == NULL) {
                pthread_mutex_unlock(&tag->mutexTag);
                return ERROR_LECTURA_FALLIDA;
            }
            char *hash = crypto_md5 (contenido, tam_bloq);
            free(contenido);

            int *pbloqfis = (int*)list_get(tag->physicalBlocks, i);            
            int bloqActual = *pbloqfis;
            
            pthread_mutex_lock(&mutex_hash_index);
            if(config_has_property(configHash, hash)){
                // CASO DE DUPLICADO ENCONTRADO
                char *bloqFis = config_get_string_value(configHash, hash);
                int *nroFis = malloc(sizeof(int));
                *nroFis = atoi(bloqFis + 5); //bloqFis siempre sera del mismo formato "blockNRO" por lo que a partir del 6to caracter esta el nro Fisico
                //log_info(loggerStorage, "Mismo contenido leido en bloque %d", *nroFis);
                if(bloqActual != *nroFis){
                    list_replace(tag->physicalBlocks, i, nroFis);
                    free(pbloqfis);
                    char*nuevoBloqPath = obtener_path_bloque_fisico(*nroFis);
                    unlink(bloqLogPath);
                    if(link(nuevoBloqPath, bloqLogPath) == -1) {
                        //log_info(loggerStorage, "Fallo al crear link canónico: %s", strerror(errno));
                        free(nuevoBloqPath);
                        pthread_mutex_unlock(&mutex_hash_index);
                        return ERROR_LINK_FALLIDO;
                    }
                    // LOG OBLIGATORIO //
                    log_info(loggerStorage, "##%d - %s:%s Bloque Lógico %06d se reasigna de %d a %d", query_id, nombreArch, nombreTag, i, bloqActual, *nroFis);
                    liberar_bloque_si_no_referenciado(bloqActual, query_id);
                    free(nuevoBloqPath);
                    
                }else
                    free(nroFis); //si son iguales liberamos la referencia dee nroFis
                //free(bloqFis);
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
                    char nuevaEntrada[512];
                    sprintf(nuevaEntrada, "%s=%s\n",hash,Bloque);
                    fputs(nuevaEntrada, archHash);
                    fclose(archHash);
                } else {
                    log_error(loggerStorage, "Fallo al abrir blocks_hash_index.config en modo append");
                }
                cargar_config_hashIndex();
            }
            pthread_mutex_unlock(&mutex_hash_index);
            free(bloqLogPath);
            free(hash);
        }
        tag->estado = COMMITED;
        
        t_metadata *meta = leer_metadata(nombreArch, nombreTag);

        meta->tamanio = tag->tamanio;
        free(meta->estado);
        meta->estado = strdup("COMMITED");
        //meta->blocks = list_duplicate(tag->physicalBlocks);
        list_destroy_and_destroy_elements(meta->blocks, free);
        meta->blocks = list_create();
        for(int i = 0; i < list_size(tag->physicalBlocks); i++) {
            int* original = list_get(tag->physicalBlocks, i);
            int* copia = malloc(sizeof(int));
            *copia = *original;
            list_add(meta->blocks, copia);
        }

        guardar_metadata(meta, nombreArch, nombreTag);
        destruir_metadata(meta); //elimina memory leak?
        // LOG OBLIGATORIO //
        log_info(loggerStorage, "##%d - Commit de File:Tag %s:%s", query_id, nombreArch, nombreTag);
        
        pthread_mutex_unlock(&tag->mutexTag);
        return RESULTADO_OK;
    }
    return ERROR_TAG_INEXISTENTE;
}

char* leer_contenido_bloque(char* path_bloque_logico) {
    // Reservamos memoria para el contenido del bloque
    usleep(retardo_acceso_bloque  * 1000);
    char* contenido = calloc(1, tam_bloq + 1);
    if (contenido == NULL) {
        log_info(loggerStorage, "Error al reservar memoria para el contenido del bloque.");
        return NULL;
    }

    // Abrimos el hardlink que referencia al bloq fis

    FILE* arcBloque = fopen(path_bloque_logico, "r");
    if (arcBloque == NULL) {
        log_info(loggerStorage, "Error al abrir bloque lógico %s", path_bloque_logico);
        free(contenido);
        return NULL;
    }
    size_t bytes_leidos = fread(contenido, 1, tam_bloq, arcBloque);
    fclose(arcBloque);

    // Verificación opcional, pero puede servir si el archivo no está lleno
    if (bytes_leidos != tam_bloq) {
        log_warning(loggerStorage, "Lectura parcial. Leídos %zu de %d bytes en %s.", bytes_leidos, tam_bloq, path_bloque_logico);
        // Lo de zu es z por el tipo de size_t y u porque es unsigned!
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
            marcar_libre_en_bitmap(bloque_fisico, query_id);
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

t_motivo op_write_block(char* nombreArch, char *nombreTag, int nroBloque, void *contenido, int query_id){
    usleep(retardo_operacion  * 1000);
    t_tag *tag = buscar_Tag_Arch(nombreArch,nombreTag);
    if (!tag) {
        log_info(loggerStorage, "##%d - Tag inexistente %s:%s", query_id, nombreArch, nombreTag);
        return ERROR_TAG_INEXISTENTE;
    }
    if(nroBloque > tag->logBlocks){
        log_info(loggerStorage, "##%d - Fallo : Escritura fuera de rango %s:%s", query_id, nombreArch, nombreTag);
        return ERROR_FUERA_DE_LIMITE;
    }
    if(tag->estado != COMMITED) {
        //log_info(loggerStorage, "## BLOQ LOGICO A ESCRIBIR %d", nroBloque);
        pthread_mutex_lock(&tag->mutexTag);
        
        int *bloqActual = (int*)list_get(tag->physicalBlocks, nroBloque);            
        int bloqFis = *bloqActual;
        pthread_mutex_unlock(&tag->mutexTag);

        //log_info(loggerStorage, "## LE CORRESPONDE EL BLOQFIS %d", nroBloque);
        char *pathBloqFis = obtener_path_bloque_fisico(bloqFis);
        char *pathBloqLog = obtener_path_bloq_logico(tag, nroBloque);
        //log_info(loggerStorage, "## SE ESCRIBE SOBRE EL bloqLog %s", pathBloqLog);
            //buscar bloque fisico al que esta asociado el link del bloque logico
        struct stat st;
        if (stat(pathBloqFis, &st) == 0) {
            if (st.st_nlink == 2 && bloqFis != 0) {
                //marcar_libre_en_bitmap(bloque_fisico);
                FILE *bloqL = fopen(pathBloqLog, "r+");
                if(!bloqL){
                    log_info(loggerStorage, "##%d - Error al abrir el bloque lógico '%s' ", query_id,pathBloqLog);
                    free(pathBloqLog);
                    free(pathBloqFis);
                    return ERROR_NO_PUDO_ABRIR_ARCHIVO;
                }
                fwrite(contenido, 1, tam_bloq, bloqL);
                fclose(bloqL);
                free(pathBloqFis);
                free(pathBloqLog);
                // LOG OBLIGATORIO //
                log_info(loggerStorage, "##%d - Bloque Lógico Escrito %s:%s - Número de Bloque: %d",query_id, nombreArch, nombreTag, nroBloque);
                return RESULTADO_OK;
            }else{
                unlink(pathBloqLog);
                    //marcar_libre_en_bitmap(bloqFis);
                    //repensar esto
                int *nuevoBloqFis = malloc(sizeof(int));
                *nuevoBloqFis = buscar_bloque_libre(query_id);
                    //list_remove(tag->physicalBlocks, bloqLog);
                int* punteroBloq = list_replace(tag->physicalBlocks, nroBloque, nuevoBloqFis);
                if (punteroBloq)
                    free(punteroBloq);
            
                char *nuevoPathBloqFis = obtener_path_bloque_fisico(*nuevoBloqFis);
                if(link(nuevoPathBloqFis, pathBloqLog) == -1) {
                    log_info(loggerStorage, "Error en el linking");
                    free(pathBloqLog);
                    free(pathBloqFis);
                    free(nuevoPathBloqFis);
                    return ERROR_LINK_FALLIDO;
                }
                // LOG OBLIGATORIO //
                log_info(loggerStorage, "##%d - %s:%s Se agregó el hard link del bloque lógico %06d al bloque físico %06d", query_id, nombreArch, nombreTag, nroBloque, *nuevoBloqFis);
                usleep(retardo_acceso_bloque  * 1000);
                FILE *bloqL = fopen(pathBloqLog, "r+");
                if(!bloqL){
                    log_info(loggerStorage, "Error al abrir el bloque lógico '%s' : %s", pathBloqFis, strerror(errno));
                    free(pathBloqLog);
                    free(pathBloqFis);
                    free(nuevoPathBloqFis);
                    return ERROR_NO_PUDO_ABRIR_ARCHIVO;
                }
                //fseek(bloqL, offset, SEEK_SET);
                fwrite(contenido, 1, tam_bloq, bloqL);
                fclose(bloqL);
                free(pathBloqLog);
                free(pathBloqFis);
                free(nuevoPathBloqFis);
                //return RESULTADO_OK;
                //faltan frees y falta mejor implementacion de bloques logicos
            }
            
        } else {
            log_info(loggerStorage, "##%d - File:Tag %s:%s ya está COMMITED. No hay cambios", query_id ,nombreArch, nombreTag);
            free(pathBloqLog);
            free(pathBloqFis);
            return ERROR_ESCRITURA_NO_PERMITIDA; 
        }
    }
    log_info(loggerStorage, "##%d - Bloque Lógico Escrito %s:%s - Número de Bloque: %d",query_id, nombreArch, nombreTag, nroBloque);
    return RESULTADO_OK; //tendria que ir aca el return
}

t_motivo op_read_block(char* nombreArch, char *nombreTag, int nroBloque, char **contenido, int query_id){
    usleep(retardo_operacion  * 1000);
    t_tag *tag = buscar_Tag_Arch(nombreArch, nombreTag);
    
    char *pathBloq = obtener_path_bloq_logico(tag,nroBloque);
    if(nroBloque >= tag->logBlocks || nroBloque < 0){
        log_info(loggerStorage, "##%d - Fallo : Lectura fuera de rango %s:%s", query_id,nombreArch, nombreTag);
        free(pathBloq);
        return ERROR_FUERA_DE_LIMITE;
    }
    if((*contenido = leer_contenido_bloque(pathBloq)) != NULL){
        // LOG OBLIGATORIO //
        log_info(loggerStorage, "##%d - Bloque Lógico Leído %s:%s - Número de Bloque: %d", query_id, nombreArch, nombreTag, nroBloque);
        free(pathBloq);
        return RESULTADO_OK;
    }else{
        free(pathBloq);
        return ERROR_LECTURA_FALLIDA;
    }
}

t_motivo op_delete_tag(char* nombreArch, char *nombreTag, int query_id){
    usleep(retardo_operacion * 1000);
    t_tag *tag = buscar_Tag_Arch(nombreArch, nombreTag);
    if(!tag){
        return ERROR_TAG_INEXISTENTE;
    }
    for(int i = 0; i < tag->logBlocks; i++){
        int *bloqActual = (int*)list_get(tag->physicalBlocks, i);            
        int bloqFis = *bloqActual;
        //int bloqFis = list_get(tag->physicalBlocks,i);
        liberar_bloque_si_no_referenciado(bloqFis, query_id);
    }
    //log_info(loggerStorage, "Path a borrar : %s",tag->pathTag);
    char cmd[256];
    sprintf(cmd,"rm -rf %s",tag->pathTag);
    //log_info(loggerStorage, "comando : %s",cmd);
    system(cmd);
    eliminarStructTag(nombreArch, nombreTag);
    // LOG OBLIGATORIO //
    log_info(loggerStorage, "“##%d - Tag Eliminado %s:%s", query_id, nombreArch, nombreTag);
    return RESULTADO_OK;
}

//==========ADMINISTRACION DE ARCHIVOS Y TAGS==========

t_fcb *crear_fcb(char *nombreNuevoArch, char *nombreNuevoTag){
    t_fcb *fcb = malloc(sizeof(t_fcb));
    fcb->nombreArch = strdup(nombreNuevoArch);
    fcb->tags = dictionary_create();
    t_tag *nuevoTag = crear_tag(nombreNuevoTag,nombreNuevoArch, fcb->tags);
    nuevoTag->physicalBlocks = list_create();
    dictionary_put(diccionario_archivos,fcb->nombreArch,fcb);
    log_info(loggerStorage, "%s:%s : Registrado", fcb->nombreArch, nuevoTag->nombreTag);
    return fcb;
}

t_tag *crear_tag(char *nombreNuevoTag, char *nombreArch,t_dictionary *diccionarioTagsArch){
    t_tag *tag = malloc(sizeof(t_tag));
    char *pathNuevoTag = malloc(256); 
    sprintf(pathNuevoTag, "%s/%s/%s", path_files, nombreArch, nombreNuevoTag);
    tag->pathTag = pathNuevoTag;
    //log_info(loggerStorage,"Nuevo Path Tag = %s", tag->pathTag);
    tag->nombreTag = strdup (nombreNuevoTag);
    //log_info(loggerStorage,"Nuevo Nombre Tag = %s", tag->nombreTag);
    //tag->nombreTag = nombreNuevoTag;
    tag->tamanio = 0;
    //tag->physicalBlocks = list_create();
    tag->logBlocks = 0;
    tag->estado = WORK_IN_PROGRESS;
    pthread_mutex_init(&tag->mutexTag, NULL);
    dictionary_put(diccionarioTagsArch, tag->nombreTag, tag);
    return tag;
}

t_tag *buscar_Tag_Arch(char *Arch, char *Tag){
    t_fcb *fcb = dictionary_get(diccionario_archivos, Arch);
    if(!fcb){
        log_info(loggerStorage, "Error : Archivo No Encontrado : %s",Arch);
        return NULL;
    }
    t_tag *tag = dictionary_get(fcb->tags, Tag);
    //log_info(loggerStorage, "Enviaron %s:%s, encontramos %s:%s", Arch, Tag, fcb->nombreArch, tag->nombreTag);
    return tag;
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

void eliminarStructTag(char* nombreArch, char *nombreTag){
    t_fcb *fcb = dictionary_get(diccionario_archivos,nombreArch);
    t_tag *tag = dictionary_remove(fcb->tags,nombreTag);
    if(tag != NULL) {
        //log_info(loggerStorage, "Se eliminara %s:%s", nombreArch, tag->nombreTag);
        destruir_tag_item(tag);
    }
}

void crear_copia_tag(char* nombreArch,t_tag *tagOrigen, char *nombreNuevoTag){
    t_fcb *arch = dictionary_get(diccionario_archivos,nombreArch);
    t_tag *nuevoTag = crear_tag(nombreNuevoTag,nombreArch,arch->tags);
    //list_destroy(nuevoTag->physicalBlocks);
    //REVISAR ESTRUCTURAS ADMINISTRATIVAS PARA TAG Y METADATA
    nuevoTag->tamanio = tagOrigen->tamanio;
    nuevoTag->estado = WORK_IN_PROGRESS;
    nuevoTag->logBlocks = tagOrigen->logBlocks;
    
    //nuevoTag->physicalBlocks = list_duplicate (tagOrigen->physicalBlocks);
    nuevoTag->physicalBlocks = list_create();
    for(int i = 0; i < list_size(tagOrigen->physicalBlocks); i++) {
        int* original = (int*)list_get(tagOrigen->physicalBlocks, i);
        int* copia = malloc(sizeof(int));
        *copia = *original;
        list_add(nuevoTag->physicalBlocks, copia);
    }

    t_metadata *meta = leer_metadata(nombreArch, nombreNuevoTag);
    if (meta->blocks) list_destroy_and_destroy_elements(meta->blocks, free);
    meta->blocks = list_create();

    for(int i = 0; i < list_size(nuevoTag->physicalBlocks); i++) {
        int* original = (int*)list_get(nuevoTag->physicalBlocks, i);
        int* copia_meta = malloc(sizeof(int)); 
        *copia_meta = *original;
        list_add(meta->blocks, copia_meta);
    }
    //tuvimos que recurrir a copiar elemento por elemento ya que sino se liberaba los elementos de la lista de tag
    //meta->blocks = nuevoTag->physicalBlocks;
    if (meta->estado) {
        free(meta->estado);
    }
    meta->estado = strdup("WORK_IN_PROGRESS");
    guardar_metadata(meta, nombreArch, nombreNuevoTag);
    destruir_metadata(meta);

}

//==========METADATA==========

void crear_metadata (char* path, char* nuevoPath) {
    char pathConfig [256];
    sprintf(pathConfig, "%s/metadata.config", path);
    FILE* archivo = fopen(pathConfig, "w+");
    if(!archivo) {
        log_error(loggerStorage, "Error al crear el archivo 'metadata.config': %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    fputs("BLOCKS=[]\n",archivo);
    fputs("TAMAÑO=0\n",archivo);
    fputs("ESTADO=WORK_IN_PROGRESS\n",archivo);

    fclose(archivo);
    if(nuevoPath != NULL){
        strcpy(nuevoPath,pathConfig);
    }
}

void destruir_metadata(t_metadata* meta) {
    if (!meta)
        return;
    if (meta->estado){
        free(meta->estado);
    }
    if (meta->blocks) {
        list_destroy_and_destroy_elements(meta->blocks, free);
    }
    free(meta);
}

char *path_Metadata(char *nombreArch, char *nombreTag){
    char *metadata = malloc(256); // reservo memoria dinámica
    if (!metadata) return NULL;
    sprintf(metadata, "%s/%s/%s/metadata.config", path_files, nombreArch, nombreTag);
    return metadata;
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
    meta->estado = strdup (config_get_string_value(config, "ESTADO"));

    // 6. Leer el array de bloques
    char** array_blocks = config_get_array_value(config, "BLOCKS");

    meta->blocks = list_create();
    for (int i = 0; array_blocks[i] != NULL; i++) {
        int* block = malloc(sizeof(int));
        *block = atoi(array_blocks[i]);
        list_add(meta->blocks, block);
        free(array_blocks[i]);
    }
    free(array_blocks);
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

void guardar_metadata(t_metadata* meta, char* archivo, char* nombreTag) {
    char* path_meta = path_Metadata(archivo, nombreTag);

    t_config* config = config_create(path_meta);
    if (!config) {
        log_error(loggerStorage, "No se pudo abrir (para guardar) metadata: %s", path_meta);
        free(path_meta);
        exit(EXIT_FAILURE);
    }

    // TAMAÑO
    char tamanio_str[32];
    sprintf(tamanio_str, "%d", meta->tamanio);
    config_set_value(config, "TAMAÑO", tamanio_str);
    // ESTADO
    config_set_value(config, "ESTADO", meta->estado);

    // BLOCKS
    // Construir string tipo [17,2,5]
    char buf[4096];
    char tmp[64];
    buf[0] = '\0';
    strcat(buf, "[");

    int n = list_size(meta->blocks);
    for (int i = 0; i < n; i++) {

        int *bloqActual = (int*)list_get(meta->blocks, i);            
        int v = *bloqActual;

        //int v = list_get(meta->blocks, i);
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