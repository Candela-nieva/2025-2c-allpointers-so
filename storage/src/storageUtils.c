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
    cargar_config_superBlock();
    freshStart();
    initialFile();
    log_info(loggerStorage, "SE ABRIO EL DIRECTORIO RAIZ : FS SIZE = %d ; BLOCK SIZE = %d",fs_size,tam_bloq);
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

char *buscar_bloque_fisico(int nroBloque){
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
    char nroBloque[256];
    for(int i=0; i < cantBloq; i++){
        sprintf(nroBloque,"%0*d", anchoEntrada, i);
        sprintf(nombreArch, "%s/block%s.dat", path_blocks, nroBloque);
        FILE *archBloque = fopen(nombreArch, "w+");
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
    op_trunc("initial_file","BASE",tam_bloq);
    /*int bloqueInicial = buscar_bloque_libre();
    bitarray_set_bit(bitarray,bloqueInicial);
    char *path_bloq = buscar_bloque_fisico(bloqueInicial);
    FILE *bloqFis = fopen(path_bloq, "w");
    for(int i = 0; i < tam_bloq;i++){
        fputc(0,bloqFis);
    }
    free(path_bloq);*/
}

//==========BITMAP==========
int buscar_bloque_libre(){
    for(int i = 0; i < cantBloq; i++){
    
        if(bitarray_test_bit (bitarray, i)==0){
            log_info(loggerStorage, "BLOQUE LIBRE ENCONTRADO %d",i);
            return i;
        }else{
            log_info(loggerStorage, "BLOQUE NO LIBRE %d",i);
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

metadata_t* leer_metadata(char* archivo, char* nombreTag) {
    t_tag *tag = buscar_Tag_Arch(archivo, nombreTag);
    // 1. Armar el path del archivo metadata
    char* path_metadata = path_Metadata(archivo, nombreTag);

    // 2. Crear estructura config
    t_config* config = config_create(path_metadata);
    if (config == NULL) {
        log_error(loggerStorage, "No se pudo abrir metadata %s", path_metadata);
        return NULL;
    }

    // 3. Crear estructura para devolver
    metadata_t* meta = malloc(sizeof(metadata_t));

    // 4. Leer el tamaño
    meta->tamanio = config_get_int_value(config, "TAMAÑO");

    // 5. Leer el estado
    meta->estado = strdup(config_get_string_value(config, "ESTADO"));

    // 6. Leer el array de bloques
    char** array_blocks = config_get_array_value(config, "BLOCKS");

    meta->blocks = list_create();
    for (int i = 0; array_blocks[i] != NULL; i++) {
        list_add(meta->blocks, (void*) atoi(array_blocks[i]));
    }

    // 7. Cerrar el config
    config_destroy(config);
    free(path_metadata);
    return meta;
}

bool op_truncate(char* nombreArch, char *nombreTag, int nuevoTamanio) {
    metadata_t* meta = leer_metadata(nombreArch, nombreTag);
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

    if (bloques_nuevos > bloques_actuales) {
        char path_block0[512];
        sprintf(path_block0, "%s/block%0*d.dat", path_blocks, ancho, 0);

        for (int i = bloques_actuales; i < bloques_nuevos; i++) {
            // agregar bloque lógico apuntando al bloque físico 0
            int* cero = malloc(sizeof(int));
            *cero = 0;
            list_add(meta->blocks, cero);

            char nro[32];
            sprintf(nro, "%0*d.dat", ancho, i);
            char path_logical[256];
            sprintf(path_logical, "%s/logical_blocks/%s", tag->pathTag, nro);
            unlink(path_logical);

            if (link(path_block0, path_logical) == -1) {
                log_error(loggerStorage, "Link a block0 falló: %s", strerror(errno));
                destruir_metadata(meta);
                return false;
            }
            log_info(loggerStorage, "## Hard Link Agregado - %s:%s - Lógico %d -> Físico 0", nombreArch, nombreTag, i);
        }
    } else if (bloques_nuevos < bloques_actuales) {
        for (int i = bloques_actuales - 1; i >= bloques_nuevos; i--) {
            int* pbloqfis = list_get(meta->blocks, i);
            int bloque_fisico = pbloqfis ? *pbloqfis : 0;

            // eliminar hardlink lógico, se puede hacer una funcion xq se repite
            char nro[32];
            sprintf(nro, "%0*d.dat", ancho, i);
            char path_logical[256];
            sprintf(path_logical, "%s/logical_blocks/%s", tag->pathTag, nro);
            unlink(path_logical);

            // revisar nlink del físico
            char path_fisico[256];
            sprintf(path_fisico, "%s/block%0*d.dat", path_blocks, ancho, bloque_fisico);
            
            struct stat st;
            if (stat(path_fisico, &st) == 0) {
                if (st.st_nlink == 1 && bloque_fisico != 0) {
                    marcar_libre_en_bitmap(bloque_fisico);
                }
            }

            int* pfis = list_remove(meta->blocks, i);
            if (pfis)
                free(pfis);

            log_info(loggerStorage, "## Hard Link Eliminado - %s:%s - Lógico %d -> Físico %d", nombreArch, nombreTag, i, bloque_fisico);
        }
    }

    meta->tamanio = nuevoTamanio;
    guardar_metadata(meta, nombreArch, nombreTag);
    log_info(loggerStorage, "## File Truncado %s:%s - Tamaño: %d", nombreArch, nombreTag, nuevoTamanio);
    destruir_metadata(meta);
    return true;
}

void marcar_libre_en_bitmap(int nro_fisico) {
    if (nro_fisico <= 0 || nro_fisico >= cantBloq)
        return;
    bitarray_clean_bit(bitarray, nro_fisico);
    size_t bytes_bitmap = (cantBloq + 7) / 8;
    msync(mappeo, bytes_bitmap, MS_SYNC);
    log_info(loggerStorage, "## Bloque Físico Liberado - Número de Bloque: %d", nro_fisico);
}

void guardar_metadata(metadata_t* meta, char* archivo, char* nombreTag) {
    char* path_meta = path_Metadata(archivo, nombreTag);

    // Asegurar que existe el archivo
    FILE* f = fopen(path_meta, "r");
    if (!f)
        f = fopen(path_meta, "w");
    if (f)
        fclose(f);

    t_config* config = config_create(path_meta);
    if (!config) {
        log_error(loggerStorage, "No se pudo abrir (para guardar) metadata: %s", path_meta);
        free(path_meta);
        return;
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
        int* v = list_get(meta->blocks, i);
        sprintf(tmp, "%d", *v);
        strcat(buf, tmp);
        if (i < n - 1) strcat(buf, ",");
    }
    strcat(buf, "]");
    config_set_value(config, "BLOCKS", buf);

    config_save(config);
    config_destroy(config);
    free(path_meta);
}

void destruir_metadata(metadata_t* meta) {
    if (!meta)
        return;
    if (meta->estado)
        free(meta->estado);
    if (meta->blocks) {
        void liberar_int(void* x) { free(x); }
        list_destroy_and_destroy_elements(meta->blocks, liberar_int);
    }
    free(meta);
}

/*bool op_trunc(char *nombreArch, char *nombreTag, int size){
    t_tag *tag = buscar_Tag_Arch(nombreArch, nombreTag);
    if(size > tag->tamanio){
        int aux = size;
        while(aux > 0){
            int bloqueInicial = buscar_bloque_libre();
            bitarray_set_bit(bitarray,bloqueInicial);
            list_add(tag->physicalBlocks,bloqueInicial);
            char *path_bloq = buscar_bloque_fisico(bloqueInicial);
            crear_bloq_log(tag,path_bloq);
            aux -= tam_bloq;
        }
        return true;
    }else{
        return true;
    }
}
*/
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
    t_tag *nuevoTag = crear_tag(nombreNuevoTag,nombreNuevoArch, fcb->tags);
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
    tag->logBlocks = list_create();
    tag->estado = WIP;
    dictionary_put(diccionarioTagsArch, tag->nombreTag, tag);
    return tag;
}

t_tag *buscar_Tag_Arch(char *Arch, char *Tag){
    t_fcb *fcb = dictionary_get(diccionario_archivos, Arch);
    t_tag *tag = dictionary_get(fcb->tags, Tag);
    return tag;
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

bool op_trunc(char *nombreArch, char *nombreTag, int size){
    t_tag *tag = buscar_Tag_Arch(nombreArch, nombreTag);
    int cantBloques = ceil(size / tam_bloq); //cant de celdas a asignar / liberar
    if(size > tag->tamanio){
        //int aux = size;
        while(cantBloques > 0){
            int bloqueLibre = buscar_bloque_libre();
            bitarray_set_bit(bitarray,bloqueLibre);
            list_add(tag->physicalBlocks,bloqueLibre);
            char *path_bloqLib = buscar_bloque_fisico(bloqueLibre);
            crear_bloq_log(tag,path_bloqLib);
            cantBloques--;
        }
        tag->tamanio = size;
        return true;
    }else{
        int cantBloquesElim = ceil(tag->tamanio / tam_bloq) - size;
        while(cantBloquesElim > 0){
            eliminar_bloq_log(tag);
            cantBloquesElim--;
        }
        tag->tamanio = size;
        return true;
    }
}
//elimina su ultimo bloq_log
void eliminar_bloq_log(t_tag *tag){
    if(list_size(tag->logBlocks) > 0){
    int nroBloqLog = (list_size(tag->logBlocks) - 1); //representa el ultimo bloque logico
    char *pathBlockLog  = buscar_bloq_logico(tag, nroBloqLog);
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

void crear_bloq_log(t_tag *tag,char *bloq_fis){
    int nroBloqLog = list_size(tag->logBlocks); //representa el siguiente bloque logico a crear
    char *pathBlockLog  = buscar_bloq_logico(tag, nroBloqLog);
    log_info(loggerStorage,"Nuevo Bloq Log = %s", pathBlockLog);
    FILE *bloqL = fopen(pathBlockLog, "w+");
    ftruncate(fileno(bloqL), tam_bloq);
    link(bloq_fis,pathBlockLog);
    fclose(bloqL);
    free(pathBlockLog);
    list_add(tag->logBlocks,nroBloqLog);
}

char *buscar_bloq_logico(t_tag *tag, int nroBloqLog){
    char bloqLog[256];
    sprintf(bloqLog, "%06d.dat",nroBloqLog);
    char *pathBlockLog = malloc(256);
    sprintf(pathBlockLog, "%s/logical_blocks/%s", tag->pathTag,bloqLog);
    return pathBlockLog;
}