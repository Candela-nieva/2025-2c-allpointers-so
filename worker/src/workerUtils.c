// Inicializa la estructura de configuración
#include "workerUtils.h"
int tam_memoria;
int retardo_memoria;
t_log* loggerWorker = NULL;
t_config* config = NULL;
t_config_worker* config_struct = NULL;
int socket_storage;
int socket_master;
char* config_worker;
atomic_int hay_interrupt = ATOMIC_VAR_INIT(0);
int tamanio_bloque_storage = 0; // el antiguo tamanio_pag
t_memoria_interna memoria;
t_dictionary* tablas_de_paginas;
pthread_mutex_t mutex_tablas_paginas;

// Sincronización entre hilos
pthread_mutex_t mutex_storage_ready= PTHREAD_MUTEX_INITIALIZER;
//pthread_cond_t cond_storage_ready = PTHREAD_COND_INITIALIZER;
sem_t cond_storage_ready;
bool storage_ready = false; 

// ----------------------

void inicializar_config(void){
    config_struct = malloc(sizeof(t_config_worker)); //Reserva memoria
    config_struct->modulo = NULL;
    config_struct->ip_master = NULL;
    config_struct->puerto_master = NULL;
    config_struct->ip_storage = NULL;
    config_struct->puerto_storage = NULL;
    config_struct->tam_memoria = NULL;
    config_struct->retardo_memoria = NULL;
    config_struct->algoritmo_reemplazo = NULL;
    config_struct->path_scripts = NULL;
    config_struct->log_level = NULL;
}

void cargar_config() {

    if(!config_worker){ // solo para chequeo por ahora
        fprintf(stderr, "Ruta de config no establecida\n");
        return;
    }
    config = config_create(config_worker);

    if(!config){
        fprintf(stderr, "No se pudo abrir el archivo de config: %s\n", config_worker);
        return;
    }
    config_struct->modulo = config_get_string_value (config, "MODULO");
    config_struct->ip_master = config_get_string_value(config, "IP_MASTER");
    config_struct->puerto_master = config_get_string_value(config, "PUERTO_MASTER");
    config_struct->ip_storage = config_get_string_value(config, "IP_STORAGE");
    config_struct->puerto_storage = config_get_string_value(config, "PUERTO_STORAGE");
    config_struct->tam_memoria = config_get_string_value(config, "TAM_MEMORIA");
    config_struct->retardo_memoria = config_get_string_value(config, "RETARDO_MEMORIA");
    config_struct->algoritmo_reemplazo = config_get_string_value(config, "ALGORITMO_REEMPLAZO");
    config_struct->path_scripts = config_get_string_value(config, "PATH_SCRIPTS");
    config_struct->log_level = config_get_string_value(config, "LOG_LEVEL");

    tam_memoria = atoi(config_struct->tam_memoria);
    retardo_memoria = atoi(config_struct->retardo_memoria);
    sem_init(&cond_storage_ready, 0, 0);
}

// Función para iniciar el logger
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle){
	t_log* nuevo_logger;
	nuevo_logger = log_create( nombreArchivoLog, nombreLog, seMuestraEnConsola, nivelDetalle);
    if (nuevo_logger == NULL) {
		perror("Error en el logger"); // Maneja error si no se puede crear el logger
		exit(EXIT_FAILURE);
	}
	return nuevo_logger;
}

void crear_logger () {
    loggerWorker=iniciar_logger("worker.log","WORKER",true, log_level_from_string(config_struct->log_level));
}

// =================== CONEXION A MASTER =======================
// Tiene que esperar a que storage este listo...
// 2do) Iniciar conexion a master

void* iniciar_conexion_master(void* arg){
    
    if(arg == NULL){
        log_info(loggerWorker, "Error: El ID del Worker no fue proporcionado correctamente.");
        pthread_exit(NULL); // Terminar el hilo si no se proporciona un ID válido
    }

    int id_worker = *(int*)arg;
    
    // Esperamos a que el storage esté listo antes de proceder
    log_info(loggerWorker, "Esperando a que Storage esté listo antes de conectar con Master...");

    sem_wait(&cond_storage_ready);

    // Ahora podemos proceder con la conexión a Master
    log_info(loggerWorker, "Handshake con Storage completado. Iniciando conexión/handshake con Master...");
    
    socket_master = crear_conexion(config_struct->ip_master, config_struct->puerto_master);
    if(socket_master == -1) { 
        log_info(loggerWorker, "Error al crear la conexión con Master");
        pthread_exit(NULL); // Terminar el hilo si hay un error)
    }
    
    t_paquete* paquete = crear_paquete(HANDSHAKE_WORKER);
    agregar_a_paquete(paquete, &id_worker, sizeof(int));
    enviar_paquete(paquete, socket_master);
    log_info(loggerWorker, "Handshake enviado a Master - Worker ID enviado: %d", id_worker);
    eliminar_paquete(paquete);
    free(arg); // Liberamos el puntero
    
    
    // Despues de enviar el HANDSHAKE a Master, deberiamos recibir un HANDSHAKE_OK, antes de proceder a esperar queries??????
    // OPCIONAL: recibir HANDSHAKE_OK (si Master lo envía)
    // int op = recibir_operacion(socket_master);
    // if (op == HANDSHAKE_OK) { log_info(...) } else { ... }

    esperar_queries();

    // Cuando esperar_queries termine, cerramos la conexión y finalizamos el hilo
    close(socket_master);
    log_info(loggerWorker, "Conexión con Master cerrada. Hilo Master finalizado."); // LOG NO OBLIGATORIO
    pthread_exit(NULL); // Finaliza el hilo correctamente

    return NULL;
}
//CONVIENE UTILIZAR IN HILO PARA ESTO
void esperar_queries(){
    while(true){
        
        int cod_op = recibir_operacion(socket_master);
        //recv(socket_master, &cod_op, sizeof(int), NULL);
        log_info(loggerWorker, "Se recibio el codigo %d", cod_op);
        
        if(cod_op <= 0){
            log_info(loggerWorker, "El Master se ha desconectado. Finalizando espera de queries.");
            break; // Salir del bucle si Master se desconecta
        }

        log_info(loggerWorker, "Codigo de operacion recibido de Master: %d", cod_op);
        switch(cod_op) {
            case EJECUTAR: // Master avisa que hay una nueva query para que este worker ejecute
                void* buffer = recibir_buffer(socket_master);
                pthread_t ejecutar;
                // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
                // NO hace falta hilo para ejecutar porque es una query a la vez
                // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
                //pthread_create(&ejecutar, NULL, manejar_ejecutar, buffer);
                //pthread_detach(ejecutar);
                log_info(loggerWorker, "Se recibio la operacion EJECUTAR");
                manejar_ejecutar(buffer);
                break;
            //NUEVO!!!!!
            case DESALOJO: // Debido al algoritmo utilizado en el Master
                log_info(loggerWorker, "Master ha indicado DESALOJO de Query actual.");
                atomic_store(&hay_interrupt,1);
                break;

            // ESTO NO SE SI VA ACA!! por ahora queda acá, pero no tiene mucho sentido, en qué casos recibiriamos esto(?)    
            case FIN_QUERY: // Master avisa que NO hay mas queries para que este worker ejecute
                log_info(loggerWorker, "Master ha indicado FIN_QUERY. Finalizando espera de queries.");
                return; // Salir de la función para finalizar la espera de queries
            
            default:
                log_info(loggerWorker, "Operacion recibida desconocida: %d", cod_op);
                // NOSE CUAL DEJAR
                break; // Salir del switch para esperar la operación correcta
                continue; // Volver al inicio del bucle para esperar la operación correcta
        }
    }
}
// DESERIALIZAR EJECUTAR + LLAMADA A EJECUTAR_QUERY
void* manejar_ejecutar(void *buffer) {
    int offset = 0;
    int qid, pc, tamarch;
    char *archivo;
    
    memcpy(&(qid), buffer + offset, sizeof(int));
    offset += sizeof(int);

    memcpy(&(pc), buffer + offset, sizeof(int));
    offset += sizeof(int);
    
    memcpy(&(tamarch), buffer + offset, sizeof(int));
    offset += sizeof(int);
    
    archivo = malloc(tamarch + 1);
    memcpy(archivo, buffer + offset, tamarch);
    archivo[tamarch] = '\0';
    free(buffer);
    
    log_info(loggerWorker, "##Query %d: Se recibe la Query. El path de operaciones es: %s", qid, archivo); // LOG OBLIGATORIO
    //log_info(loggerWorker, "Manejando EJECUTAR: PC=%d, Archivo=%s", pc, archivo);
    //intento implementar cheque de interrupt
    //NUEVO!!!!!
    /*while(true){
        log_info(loggerWorker, "EJECUTANDO....EJECUTANDO....");
        usleep(3000000);
        ++pc;
        log_info(loggerWorker, "PC actualizado a %d.", pc);
        log_info(loggerWorker, "Chequeo Interrupcion");
        if(atomic_load(&hay_interrupt) == 1){
            atomic_store(&hay_interrupt,0);
            log_info(loggerWorker, "EL QUERY FUE DESALOJADO CON EXITO");
            t_paquete *paquete = crear_paquete(PC_ACTUALIZADO);
            agregar_a_paquete(paquete,&pc,sizeof(int));
            enviar_paquete(paquete,socket_master);
            eliminar_paquete(paquete);
            log_info(loggerWorker, "PC %d enviado a master.", pc);
            break;
        }

    }*/
    
    ejecutar_query(pc, archivo, qid);
    log_info(loggerWorker, "FIN DE QUERY ACTUAL");
    free(archivo);

    return NULL;
}

//////////////////////////////// query interpreter ////////////////////////////

void notificar_fin_query_a_master(int qid, int motivo_op_code) { 
    t_paquete* p = crear_paquete(WORKER_TO_MASTER_END); // (Necesitas este op_code en protocolo.h)
    agregar_a_paquete(p, &qid, sizeof(int));
    agregar_a_paquete(p, &motivo_op_code, sizeof(int));
    enviar_paquete(p, socket_master);
    eliminar_paquete(p);
}


// Ejecuta las instrucciones de un archivo de query desde una línea específica (pc_inicial)
void ejecutar_query(int pc_inicial, const char* archivo_relativo, int qid) {
    
    bool es_end = false;

    if(!archivo_relativo) {
        log_info(loggerWorker, "Error: Ruta de archivo de Query %d no proporcionada.", qid);
        return;
    }
    
    //Construir la ruta completa al archivo de query (PATH_SCRIPTS + "/" + archivo_relativo)
    char ruta_completa[1024];
    if (config_struct && config_struct->path_scripts) {
        snprintf(ruta_completa, sizeof(ruta_completa), "%s/%s", config_struct->path_scripts, archivo_relativo);
    } else {
        // fallback: usar archivo_relativo tal cual
        strncpy(ruta_completa, archivo_relativo, sizeof(ruta_completa)-1);
        ruta_completa[sizeof(ruta_completa)-1] = '\0';
    }

    FILE *file = fopen(ruta_completa, "r");
    if(!file){
        log_info(loggerWorker, "Query %d: Error al abrir el archivo de Query: %s", qid, ruta_completa);
        return;
    }
    
    // Inicializo lista de archivos abiertos en esta Query
    t_list* archivos_abiertos = list_create();

    char linea[2048];
    int pc = 0;
    
    // 1. Bucle para saltar hasta pc_inicial (para reanudar queries)
    // Si pc_inicial = 5, este bucle se ejecuta 5 veces (pc = 0, 1, 2, 3, 4)
    
    while (pc < pc_inicial) {
        if (fgets(linea, sizeof(linea), file) == NULL) {
            // El PC está fuera de los límites del archivo
            log_info(loggerWorker, "Query %d: PC %d fuera de rango (EOF).", qid, pc_inicial);
            // revisar porque no tenemos un int notificar_fin_query_a_master(qid, "EOF");
            list_destroy(archivos_abiertos);
            fclose(file);
            return;
        }
        pc++;
    }

    // 2. Bucle principal de ejecución
    // Leemos la primera línea a ejecutar (la que corresponde a pc_inicial)
    while (fgets(linea, sizeof(linea), file) != NULL) {
        
        trim_newline(linea);

        log_info(loggerWorker, "## Query %d: FETCH - Program Counter: %d - %s", qid, pc, linea); // fetch de la linea
        
        es_end = ejecutar_instruccion(linea, qid, pc, archivos_abiertos);
        
        if (es_end) {
            log_info(loggerWorker, "##Query %d: Query finalizada (END)", qid);
            // revisar para mandar bien por el code notificar_fin_query_a_master(qid, "OK");
            break; // Salir del bucle while
        }
        
        // El retardo ahora debe estar DENTRO de 'ejecutar_instruccion',
        // solo en los casos READ y WRITE.
        
        // Comprobar interrupción (desalojo)
        if (atomic_load(&hay_interrupt) == 1) {
            atomic_store(&hay_interrupt, 0);

            for(int i = 0; i < list_size(archivos_abiertos); i++) {
                char* file_tag = list_get(archivos_abiertos, i);
                // ejecutar_flush ya sabe página que hay que guardar en storage
                ejecutar_flush(file_tag, qid);
            }

            int pc_siguiente = pc + 1;
            
            t_paquete *paquete = crear_paquete(PC_ACTUALIZADO);
            agregar_a_paquete(paquete, &pc_siguiente, sizeof(int)); 
            enviar_paquete(paquete, socket_master);
            eliminar_paquete(paquete);
            
            log_info(loggerWorker, "## Query %d: Desalojada por pedido del Master. PC guardado: %d", qid, pc_siguiente);
            
            list_destroy_and_destroy_elements(archivos_abiertos, free);
            fclose(file);
            return; // Salir de la función (el hilo muere)
        }

        pc++; // Avanzar al siguiente Program Counter
    
    }

    // Si salimos del bucle y no fue por 'END' (es_end=false), fue por Fin de Archivo (EOF)
    if (!es_end) {
        log_info(loggerWorker, "##Query %d: Fin de archivo alcanzado (EOF).", qid);
        // revisar notificar_fin_query_a_master(qid, "EOF");
    }
    
    fclose(file);
}

void registrar_archivo_abierto(t_list* lista, char* file_tag) {
    if (!lista || !file_tag) return;

    bool es_mismo_archivo(void* elemento) {
    return strcmp((char*)elemento, file_tag) == 0;
    }

    // Si NO está en la lista, lo agregamos duplicando el string (dueños de la memoria)
    if (!list_any_satisfy(lista, es_mismo_archivo)) {
        list_add(lista, strdup(file_tag));
        log_info(loggerWorker, "Archivo %s registrado para control de flush.", file_tag);
    }
}



// Limpia las líneas leídas de archivos para que no tengan saltos de línea al final, 
// y así procesarlas correctamente en el query interpreter.
void trim_newline(char* s) {
    if(!s) return;
    size_t len = strlen(s);
    while(len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
        s[len - 1] = '\0';
        len--;
    }
}
// Para mapear las instrucciones
tipo_instruccion obtener_instruccion(const char* op) {
    if (strcmp(op, "CREATE") == 0) return CREATE;
    if (strcmp(op, "TRUNCATE") == 0) return TRUNCATE;
    if (strcmp(op, "WRITE") == 0) return WRITE;
    if (strcmp(op, "READ") == 0) return READ;
    if (strcmp(op, "TAG") == 0) return TAG;
    if (strcmp(op, "COMMIT") == 0) return COMMIT;
    if (strcmp(op, "FLUSH") == 0) return FLUSH;
    if (strcmp(op, "DELETE") == 0) return DELETE;
    if (strcmp(op, "END") == 0) return END;
    return DESCONOCIDA;
}

//Devuelve 'true' si la instrucción fue END, 'false' en cualquier otro caso.
// 
bool ejecutar_instruccion(const char* instruccion, int qid, int pc, t_list* archivos_abiertos) {
    if(!instruccion) return false;
    
    char* copia = strdup(instruccion);
    trim_newline(copia);

    char* op = strtok(copia, " "); // Obtenemos la operación (primera palabra)
    log_info(loggerWorker, "Operacion registrada : %s", op);    
    if(!op) {
        log_info(loggerWorker, "Query %d: - Instrucción vacía en PC=%d", qid, pc);
        free(copia);
        return false;
    }

    // Guardamos la operación para el log obligatorio
    char instruccion_log[256];
    strncpy(instruccion_log, op, 255);

    bool fue_end = false;
    char* dir_str = NULL;
    char* tam_str = NULL;
    char* contenido = NULL;
    char* origen = NULL;
    char* destino = NULL;
    int direccion = 0;
    int tam = 0;
    int nuevo_tam = 0;

    tipo_instruccion inst_tipo = obtener_instruccion(op);

    char* file_tag = strtok(NULL, " ");
    log_info(loggerWorker, "File_Tag a operar : %s", file_tag);
    // Si escribimos, leemos, creamos tag o truncamos, el archivo queda "abierto" o potencialmente modificado
    if (file_tag != NULL && archivos_abiertos != NULL)
        if (inst_tipo == WRITE || inst_tipo == TAG || inst_tipo == READ || inst_tipo == TRUNCATE)
            registrar_archivo_abierto(archivos_abiertos, file_tag);

    // 2. Usamos el enum en el switch e inicializamos motivo para todos los cases
    t_motivo motivo = ERROR_DESCONOCIDO;

    switch (inst_tipo) {
        case CREATE:
            log_info(loggerWorker, "EJECUCION DE CREATE");
            motivo = ejecutar_create(file_tag, qid);
            break;
        case TRUNCATE:
            tam_str = strtok(NULL, " ");
            nuevo_tam = (tam_str != NULL) ? atoi(tam_str) : 0;
            motivo = ejecutar_truncate(file_tag, nuevo_tam, qid);
            break;
        case WRITE:
            dir_str = strtok(NULL, " ");
            contenido = strtok(NULL, "");
            direccion = (dir_str != NULL) ? atoi(dir_str) : 0;
            // RETARDO_MEMORIA: Solo se aplica en READ y WRITE
            usleep((useconds_t)retardo_memoria * 1000);
            motivo = ejecutar_write(file_tag, direccion, contenido, qid);
            break;
        case READ:
            dir_str = strtok(NULL, " ");
            tam_str = strtok(NULL, " ");
            direccion = (dir_str != NULL) ? atoi(dir_str) : 0;
            tam = (tam_str != NULL) ? atoi(tam_str) : 0;

            // RETARDO_MEMORIA: Solo se aplica en READ y WRITE
            usleep((useconds_t)retardo_memoria * 1000);
            motivo = ejecutar_read(file_tag, direccion, tam, qid);
            break;
        case TAG:
            origen = strtok(NULL, " ");
            destino = strtok(NULL, ""); 
            motivo = ejecutar_tag(origen, destino, qid);
            break;
        case COMMIT:
            motivo = ejecutar_commit(file_tag, qid);
            break;
        case FLUSH:
            motivo = ejecutar_flush(file_tag, qid);
            break;
        case DELETE:
            motivo = ejecutar_delete(file_tag, qid);
            break;
        case END:
            log_info(loggerWorker, "Query %d: END recibido. Finalizando query.", qid);
            notificar_fin_query_a_master(qid, RESULTADO_OK);
            fue_end = true;
            break;
        default:
            log_error(loggerWorker, "Query %d: - Instrucción desconocida: %s", qid, instruccion);
            break;
    }
    if(!fue_end && motivo != RESULTADO_OK) {
        manejar_errores(motivo, qid);
        fue_end = true; // Debemos terminar la query por el error
    } else {
        if(!fue_end)
            log_info(loggerWorker, "## Query %d: Instrucción realizada: %s", qid, instruccion_log);
    }       // LOG OBLIGATORIO: Instrucción realizada
    free(copia);
    return fue_end;
}

// ==== Funciones de ejecucion de instrucciones ====

// CREATE
t_motivo ejecutar_create(char* file_tag, int qid){

    char* copia = strdup(file_tag);
    char* file_origen;
    char* tag_origen;
    deserializar_fileTag(copia, &file_origen, &tag_origen);

    t_paquete* paquete = crear_paquete(CREATE_FILE);
    agregar_a_paquete(paquete, &qid, sizeof(int));
    agregar_a_paquete_string(paquete, file_origen, strlen(file_origen));
    agregar_a_paquete_string(paquete,tag_origen,strlen(tag_origen));
    //int tamanio = 0;
    //agregar_a_paquete(paquete, &tamanio, sizeof(int));
    log_info(loggerWorker,"Se envia a Storage el CODOP %d , Qid %d , file : %s , tag : %s", CREATE_FILE, qid, file_origen, tag_origen);
    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);
    free(copia);
    //log_info(loggerWorker, "Instrucción CREATE enviada a Storage para el tag: %s", file_tag);
    
    int resultado = recibir_operacion(socket_storage);
    t_motivo motivo = (t_motivo) resultado;
    return motivo;
}

// TRUNCATE
t_motivo ejecutar_truncate(char* file_tag, int nuevo_tam, int qid) {
    char* copia = strdup(file_tag);
    char* file;
    char* tag;
    deserializar_fileTag(copia, &file, &tag);
    log_info(loggerWorker, "TRUNCATE solicitado en tag: %s, nuevo tamaño: %d", tag, nuevo_tam); // LOG NO OBLIGATORIO
    //logica multiplo
    if(nuevo_tam % tamanio_bloque_storage != 0) {
        log_info(loggerWorker, "Error: El nuevo tamaño %d NO es múltiplo del tamaño de bloque %d. TRUNCATE abortado.", nuevo_tam, tamanio_bloque_storage);
        return ERROR_TAMANIO_NO_MULTIPLO;
    }

    t_paquete* paquete = crear_paquete(TRUNCATE_FILE);
    agregar_a_paquete(paquete, &qid, sizeof(int));
    agregar_a_paquete_string(paquete, file, strlen(file));
    agregar_a_paquete_string(paquete, tag,strlen(tag));
    agregar_a_paquete(paquete, &nuevo_tam, sizeof(int));
    enviar_paquete(paquete, socket_storage);  // se envia a Storage
    eliminar_paquete(paquete);
    free(copia);
    // Esperamos la confirmacion de storage (errores en las operaciones)
    log_info(loggerWorker, "Instrucción TRUNCATE enviada a Storage para el tag: %s, nuevo tamaño: %d", tag, nuevo_tam); // LOG NO OBLIGATORIO
    int resultado = recibir_operacion(socket_storage);
    t_motivo motivo = (t_motivo) resultado;
    return motivo;
}

// Función para saber si un string ya está en la lista (para no repetir)
bool ya_esta_en_lista(t_list* lista, char* file_tag) {
    for(int i=0; i<list_size(lista); i++) {
        char* item = list_get(lista, i);
        if(strcmp(item, file_tag) == 0) return true;
    }
    return false;       // la funcion manejar_e
}

// WRITE
t_motivo ejecutar_write(char* tag, int direccion_base, char* contenido, int qid) {

    usleep((useconds_t)retardo_memoria * 1000);  // Retardo por escritura en memoria
    
    int tam_pagina= tamanio_bloque_storage; // tamaño de bloque de storage
    int offset = direccion_base % tam_pagina;
    int pagina_logica = direccion_base / tam_pagina;
    int bytes_restantes = strlen(contenido);
    
    char* ptr_contenido = contenido;
    
    /*char* file_origen;
    char* tag_origen ;
    deserializar_fileTag(strdup(file_tag), file_origen, tag_origen);;*/

    t_tabla_paginas* tabla = obtener_o_crear_tabla_paginas(tag);
    
      // PARA ESCRIBIR EN TODAS LAS PÁGINAS NECESARIAS ------
    while (bytes_restantes > 0){
        // 1. Verificar si existe entrada y si está presente
        t_pagina* pagina = buscar_pagina(tabla, pagina_logica);

        // Si no esta presente PAGE FAULT
        if(!pagina || !pagina->presente) {
            t_motivo motivo;
            pagina = manejar_page_fault(tag, pagina_logica, tabla, qid, &motivo);
            if (!pagina) {
                log_error(loggerWorker, "Query %d:  ejecutar_write: Falló el Page Fault. Abortando escritura.", qid);
                return motivo; // FRACASO (el manejador ya notificó al Master)
            }
        }
    
        // 2. Calcular lo que se puede escribir en esta página
        int espacio_en_pagina = tam_pagina - offset;
        int a_escribir;
        if (bytes_restantes < espacio_en_pagina)
        a_escribir = bytes_restantes;
        else
        a_escribir = espacio_en_pagina;

        // direccion fisica
        int marco = pagina->marco;
        char* direccion_marco = direccion_fisica_marco(marco);
        void* direccion_destino = (char*)direccion_marco + offset;

        // 3. Escribir protegido por mutex
        pthread_mutex_lock(&memoria.mutex);
        memcpy(direccion_destino, ptr_contenido, a_escribir);
        pthread_mutex_unlock(&memoria.mutex);

        // 4. Actualizar metadata de la página
        pagina->modificado = true;
        pagina->uso = true;
        pagina->ultima_ref = time(NULL);

        // 5. Avanzar punteros/contadores
        ptr_contenido += a_escribir;
        bytes_restantes -= a_escribir;
        pagina_logica++;
        offset = 0;  // luego de la primera página, offset siempre 0

    }

    log_info(loggerWorker, "Query %d: Accion: ESCRIBIR - Direccion Fisica: %d - Valor: %s", 
        qid, direccion_base, contenido); // LOG OBLIGATORIO

    return RESULTADO_OK;
}

// Maneja un page fault para la página lógica dada char* nombreArch, char* nombreTag,
t_pagina* manejar_page_fault(char* file_tag, int pagina_logica, t_tabla_paginas* tabla, int qid, t_motivo *motivo) {
    
    log_info(loggerWorker, "Query %d: Page Fault en %s página %d", qid, file_tag, pagina_logica);
    //posible semafoso lock

    int indice_marco_victima = seleccionar_victima(qid);

    t_marco* marco_victima = &memoria.marcos[indice_marco_victima];  

    if(marco_victima->ocupado) {
        char* file_tag_victima = marco_victima->file_tag;
        int num_pagina_victima = marco_victima->pagina_logica;
        // Buscar su tabla y entrada de página Seleccionar Marco Víctima (Bloqueamos RAM Física) pthread_mutex_lock(&memoria.mutex);
        pthread_mutex_lock(&mutex_tablas_paginas);
        t_tabla_paginas* tabla_victima = dictionary_get(tablas_de_paginas, file_tag_victima);
        t_pagina* pagina_victima = buscar_pagina(tabla_victima, num_pagina_victima);

        if(pagina_victima) {
            //justamente flush porque eestá modificada
            if(pagina_victima->modificado) {
                log_info(loggerWorker, "Query %d: Guardando página modificada %s:%d en Storage antes de reemplazo",
                    qid, marco_victima->file_tag, marco_victima->pagina_logica);
                    //unlock de memoria
                void* contenido_victima = memoria.buffer + (indice_marco_victima * memoria.tamanio_marco);
                void* buffer_temporal = malloc(tamanio_bloque_storage);
                memcpy(buffer_temporal, contenido_victima, tamanio_bloque_storage);
                *motivo = enviar_bloque_a_storage(qid, file_tag, marco_victima->pagina_logica, contenido_victima);
                if (*motivo != RESULTADO_OK) {
                    log_error(loggerWorker, "Query %d: Falló al persistir víctima. Motivo: %d", qid, *motivo);
                    pthread_mutex_unlock(&mutex_tablas_paginas);
                    return NULL; // Error al guardar la víctima
                }
            }
            pagina_victima->modificado = false;
        }
        pthread_mutex_unlock(&mutex_tablas_paginas);
    }

    // Pedimos a storage la nueva bloque
    *motivo = solicitar_bloque_a_storage(qid,file_tag, pagina_logica, marco_victima);
    if (*motivo != RESULTADO_OK) {
        log_error(loggerWorker, "Query %d: Falló al traer bloque de Storage. Motivo: %d", qid, *motivo);
        return NULL; // Error al traer el bloque (Storage dijo que no existe)
    }

    // Actualizamos metadata del marco ya está en solicitar

    // Creamos o actualizamos entrada de tabla de páginas
    marco_victima->ocupado = true;
    t_pagina* nueva = buscar_pagina(tabla, pagina_logica);
    
    if(!nueva) {
        pthread_mutex_lock(&mutex_tablas_paginas);
        nueva = malloc(sizeof(t_pagina));
        nueva->num_pagina = pagina_logica;
        list_add(tabla->paginas, nueva);
        pthread_mutex_unlock(&mutex_tablas_paginas);
    }

    nueva->marco = indice_marco_victima; // o marco_victima
    nueva->presente = true;
    nueva->modificado = false;
    nueva->uso = true;
    nueva->ultima_ref = time(NULL);
    log_info(loggerWorker, "Query %d: Se asigna el Marco: %d a la Página: %d", qid, indice_marco_victima, pagina_logica);
    log_info(loggerWorker, "Query %d: Memoria Add - File: %s - Pagina: %d Marco: %d", qid, file_tag, pagina_logica, indice_marco_victima);

    //pthread_mutex_unlock(&memoria.mutex);
    return nueva;
}

// READ
t_motivo ejecutar_read(char* file_tag, int direccion_base, int tam, int qid) {
    int pagina_inicial = direccion_base / memoria.tamanio_marco;
    int offset_inicial = direccion_base % memoria.tamanio_marco;
    int bytes_restantes = tam;
    
    char* file_origen;
    char* tag_origen;
    deserializar_fileTag(strdup(file_tag), &file_origen, &tag_origen);

    char* buffer_lectura = malloc(tam); // Buffer para almacenar los datos leídos
    int pos_buffer = 0;                 // Posición actual en el buffer de lectura

    while(bytes_restantes > 0) {
        // 1) Buscar el marco asociado a la página
        //int marco = obtener_marco_de_pagina(file_tag, pagina_inicial);
        int indice_marco = obtener_indice_marco_de_pagina(file_tag, pagina_inicial);

        // 2) ¿Page fault?
        if(indice_marco == -1) {
            //marco = solicitar_bloque_a_storage(qid, file_tag, pagina_inicial, marco); // CAMBIIAR ESTO
            log_info(loggerWorker, "Query %d: Page fault en READ: %s - Pagina %d", qid, file_tag, pagina_inicial);
            t_tabla_paginas* tabla_pf = obtener_o_crear_tabla_paginas(file_tag);
            t_motivo motivo;
            t_pagina* p = manejar_page_fault(file_tag, pagina_inicial, tabla_pf, qid, &motivo);

            if (!p) {
                free(buffer_lectura);
                return motivo;
            }
            indice_marco = p->marco;

        }

        //t_marco* frame = &memoria.marcos[marco];
        //frame->en_uso = true;
        //frame->ultima_ref = time(NULL);

        // 3) Establecer cuántos bytes leer en esta iteración
        int bytes_a_leer = memoria.tamanio_marco - offset_inicial;
        if (bytes_a_leer > bytes_restantes) bytes_a_leer = bytes_restantes;
        void* origen = memoria.buffer + (indice_marco * memoria.tamanio_marco) + offset_inicial;
        memcpy(buffer_lectura + pos_buffer, origen, bytes_a_leer);

        // 4) La actualizacion de los bits de estado de la página (NO DEL MARCO)
        pthread_mutex_lock(&mutex_tablas_paginas);
        t_tabla_paginas* tabla = dictionary_get(tablas_de_paginas, file_tag);
        t_pagina* pagina = buscar_pagina(tabla, pagina_inicial);
        if (pagina) {
            pagina->uso = true;
            pagina->ultima_ref = time(NULL);
        }
        pthread_mutex_unlock(&mutex_tablas_paginas);

        // 5) Retardo de acceso
        usleep(retardo_memoria * 1000);

        // 6) Avanzar en la lectura
        bytes_restantes -= bytes_a_leer;
        pos_buffer += bytes_a_leer;
        pagina_inicial++;
        offset_inicial = 0; // solo la primera página tiene offset
    }

    // 7) Enviar contenidos al Master
    t_paquete* paquete = crear_paquete(READ_BLOCK); //no sé si el nombre está bien
    agregar_a_paquete(paquete, &qid, sizeof(int));
    agregar_a_paquete(paquete, &tam, sizeof(int));
    enviar_paquete(paquete, socket_master);
    eliminar_paquete(paquete);

    int resultado = recibir_operacion(socket_master);
    t_motivo motivo = (t_motivo) resultado;

    if(motivo == RESULTADO_OK){
        void *buffer = recibir_buffer(socket_master);
        char *contenido;
        int tamContenido;
        int offset = 0;
        memcpy(&tamContenido, buffer + offset, sizeof(int));
        offset += sizeof(int);
        contenido = malloc(tamContenido + 1);
        memcpy(contenido, buffer + offset, tamContenido);
        contenido[tamContenido] = '\0';
        free(buffer);
        //se leyo el contenido
        t_paquete *paquete = crear_paquete(MASTER_TO_QC_READ_RESULT);
        agregar_a_paquete_string(paquete,file_origen, strlen(file_origen));
        agregar_a_paquete_string(paquete, tag_origen, strlen(tag_origen));
        agregar_a_paquete_string(paquete,contenido, strlen(contenido));
        enviar_paquete(paquete, socket_master);
        eliminar_paquete(paquete);
        free(contenido);
    } else {
        manejar_errores(motivo, qid);
    }
    log_info(loggerWorker, "Query %d: Accion: LEER - Direccion fisica: %d - Valor: %s",
             qid, direccion_base, buffer_lectura); // LOG OBLIGATORIO

    free(buffer_lectura);
    return RESULTADO_OK;
}

// TAG
t_motivo ejecutar_tag(char* origen, char* destino, int qid) {
    // Separo el origen
    char* file_origen;
    char* tag_origen;
    deserializar_fileTag(origen, &file_origen, &tag_origen);

    // Separo el destino
    char* file_destino;
    char* tag_destino;
    deserializar_fileTag(destino, &file_destino, &tag_destino);

    log_info(loggerWorker, "Query %d: Solicitando TAG en Storage -> Origen [%s:%s], Destino [%s:%s]",
        qid, file_origen, tag_origen, file_destino, tag_destino);

    // Envio el paquete a storage
    t_paquete* paquete = crear_paquete(FILE_TAG);
    agregar_a_paquete(paquete, &qid, sizeof(int));
    agregar_a_paquete_string(paquete, file_origen, strlen(file_origen));
    agregar_a_paquete_string(paquete, tag_origen, strlen(tag_origen));
    agregar_a_paquete_string(paquete, file_destino, strlen(file_destino));
    agregar_a_paquete_string(paquete, tag_destino, strlen(tag_destino));   
    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);

    free(origen);
    free(destino);

    // Espero respuesta..
    int resultado = recibir_operacion(socket_storage);
    t_motivo motivo = (t_motivo) resultado;
    return motivo;
}


// COMMIT
t_motivo ejecutar_commit(char* file_tag, int qid){
    char* copia = strdup(file_tag);
    char* nombreArch;
    char* nombreTag;
    deserializar_fileTag(copia, &nombreArch, &nombreTag);

    // --- 1. Ejecutar FLUSH implícito (Obligatorio) ---
    t_motivo motivo = ejecutar_flush(file_tag, qid);
    manejar_errores(motivo, qid);

    log_info(loggerWorker, "Query %d: Iniciando COMMIT para %s", qid, file_tag);
    t_paquete* paquete = crear_paquete(COMMIT);
    agregar_a_paquete(paquete, &qid, sizeof(int));
    agregar_a_paquete_string(paquete, nombreArch, strlen(nombreArch));
    agregar_a_paquete_string(paquete, nombreTag, strlen(nombreTag));
    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);
    free(copia);

    int resultado = recibir_operacion(socket_storage);
    t_motivo respuesta = (t_motivo) resultado;
    return respuesta;
}

// FLUSH
// Persiste todas las modificaciones de un File:Tag en Memoria Interna. Falta ejecutarlo en desalojo
t_motivo ejecutar_flush(char* file_tag, int qid){
    log_info(loggerWorker, "Query %d: Iniciando FLUSH para %s", qid, file_tag);

    pthread_mutex_lock(&mutex_tablas_paginas);
    t_tabla_paginas* tabla = dictionary_get(tablas_de_paginas, file_tag);
    pthread_mutex_unlock(&mutex_tablas_paginas);

    if (!tabla) return RESULTADO_OK; 

    int paginas_flusheadas = 0;
    int cantidad_paginas = list_size(tabla->paginas);

    for (int i = 0; i < cantidad_paginas; i++) {
        pthread_mutex_lock(&mutex_tablas_paginas);
        t_pagina* pagina = list_get(tabla->paginas, i);
        bool es_candidata = (pagina->presente && pagina->modificado);
        // int nro_marco = pagina->marco;
        // Por que esto aca del marco? no puede cambiar ponele?
        // En ese caso habria que extender el mutex?
        int nro_marco = pagina->marco;
        int nro_pagina_logica = pagina->num_pagina; // Para Storage
        t_marco *marcoPag = obtener_marco_de_pagina(file_tag, nro_pagina_logica);
        
        if (es_candidata) {
            void* buffer_temporal = malloc(tamanio_bloque_storage);
            void* contenido = memoria.buffer + (nro_marco * memoria.tamanio_marco);
            memcpy(buffer_temporal, contenido, tamanio_bloque_storage);
            pthread_mutex_unlock(&mutex_tablas_paginas);
            
            t_motivo resultado = enviar_bloque_a_storage(qid, file_tag, nro_pagina_logica, buffer_temporal);
            free(buffer_temporal);
            if (resultado != RESULTADO_OK) {
                log_error(loggerWorker, "FLUSH FALLIDO pág %d. Motivo: %d", nro_marco, resultado);
                return resultado; 
            }

            pthread_mutex_lock(&mutex_tablas_paginas);
            pagina->modificado = false;
            pthread_mutex_unlock(&mutex_tablas_paginas);
            
            paginas_flusheadas++;
        } else {
            pthread_mutex_unlock(&mutex_tablas_paginas);
        }
    }
    log_info(loggerWorker, "FLUSH exitoso %s. Páginas: %d", file_tag, paginas_flusheadas);
    return RESULTADO_OK;
}

// DELETE 
t_motivo ejecutar_delete(char* file_tag, int qid){
    
    // Parseo del file y tag
    char* copia = strdup(file_tag);
    char* file;
    char* tag;
    deserializar_fileTag(copia, &file, &tag);

    // Mando paquete a storage
    t_paquete* paquete = crear_paquete(DELETE);
    agregar_a_paquete(paquete, &qid, sizeof(int));
    agregar_a_paquete_string(paquete, file, strlen(file));
    agregar_a_paquete_string(paquete, tag, strlen(tag));
    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);
    free(copia);

    // Espero respuesta del storage..
    int resultado = recibir_operacion(socket_storage);
    t_motivo motivo = (t_motivo) resultado;
    return motivo;
}

// END
// No hace falta implementar nada, el manejo del END se hace en el query interpreter.

// Para cada referencia lectura o escritura del Query Interpreter a una página de la Memoria Interna, se 
// deberá esperar un tiempo definido por archivo de configuración (RETARDO_MEMORIA).

void deserializar_fileTag(char* fileTag, char **file, char **tag){
    *file = strtok(fileTag, ":");   // obtengo el file
    *tag = strtok(NULL, ":");       // obtengo el tag
}

void manejar_errores(t_motivo motivo, int qid) {
    switch(motivo) {
        case RESULTADO_OK:
            return; // No hay error
        case ERROR_FILE_INEXISTENTE:
            log_error(loggerWorker, "Query %d: Error - File inexistente.", qid);
            break;
        case ERROR_TAG_INEXISTENTE:
            log_error(loggerWorker, "Query %d: Error - Tag inexistente.", qid);
            break;
        case ERROR_FILE_PREEXISTENTE:
            log_error(loggerWorker, "Query %d: Error - File preexistente.", qid);
            break;
        case ERROR_TAG_PREEXISTENTE:
            log_error(loggerWorker, "Query %d: Error - Tag preexistente.", qid);
            break;
        case ERROR_PAGE_FAULT:  
            log_error(loggerWorker, "Query %d: Error - Fallo en Page Fault.", qid);
            break;
        case ERROR_LECTURA_NO_PERMITIDA:
            log_error(loggerWorker, "Query %d: Error - Lectura no permitida en Storage.", qid);
            break;
        case ERROR_ESCRITURA_NO_PERMITIDA:
            log_error(loggerWorker, "Query %d: Error - Escritura no permitida en Storage.", qid);
            break;
        case ERROR_LECTURA_FALLIDA:
            log_error(loggerWorker, "Query %d: Error - Fallo en lectura desde Storage.", qid);
            break;
        case ERROR_ESPACIO_INSUFICIENTE:
            log_error(loggerWorker, "Query %d: Error - Espacio insuficiente en Storage.", qid);
            break;
        case ERROR_FUERA_DE_LIMITE:
            log_error(loggerWorker, "Query %d: Error - Lectura o escritura fuera de límite.", qid);
            break;
        case ERROR_NO_PUDO_ABRIR_ARCHIVO:
            log_error(loggerWorker, "Query %d: Error - Fallo al abrir el archivo", qid);
            break;
        case ERROR_LINK_FALLIDO:
            log_error(loggerWorker, "Query %d: Error - Fallo al crear el link simbólico", qid);
            break;
        case ERROR_TAMANIO_NO_MULTIPLO:
            log_error(loggerWorker, "Query %d: Error - Tamaño no es multiplo del tamaño de pagina", qid);
            break;
        default:
            motivo = ERROR_DESCONOCIDO;
            break;
    }
    notificar_fin_query_a_master(qid, motivo);
}


// =================== CONEXION A STORAGE ======================= 
// 1ero) Iniciar conexion a storage

void* iniciar_conexion_storage(void* arg){ 
    //(void)arg; // Evitar warning de variable no usada (porque no usamos argumento en este caso)
    socket_storage = crear_conexion(config_struct->ip_storage, config_struct->puerto_storage);
    if(socket_storage == -1) {
        log_info(loggerWorker, "Error al crear la conexión con Storage");
        pthread_exit(NULL); // Terminar el hilo si hay un error
    }
    if(arg == NULL){
        log_info(loggerWorker, "Error: El ID del Worker no fue proporcionado correctamente.");
        pthread_exit(NULL); // Terminar el hilo si no se proporciona un ID válido
    }

    int id_worker = *(int*)arg;
    
    // Enviar handshake a Storage
    t_paquete *inicial = crear_paquete(HANDSHAKE_WORKER);
    agregar_a_paquete(inicial, &id_worker, sizeof(int));
    enviar_paquete(inicial, socket_storage);
    log_info(loggerWorker, "Handshake enviado a Storage - Worker ID enviado: %d", id_worker);
    eliminar_paquete(inicial);
    //free(arg); // 
    //enviar_operacion(socket_storage, HANDSHAKE_WORKER); // Enviar el handshake a Memoria
    //recibir tamanio de pags
    int op = recibir_operacion(socket_storage);
    
    if (op == -1) {
        log_error(loggerWorker, "Conexión perdida con Storage durante handshake.");
        close(socket_storage);
        return NULL;
    }

    if(op != ENVIAR_TAMANIO_BLOQUE) {
        log_info(loggerWorker, "Se esperaba ENVIAR_TAMANIO_BLOQUE, pero llego un ERROR AL RECIBIR COD DE OP, %d", op);
        close(socket_storage);
        return NULL;
    }
    //int tamanio_pag = 0; // ahora es global pq necesitabamos usarla en otras funciones (AHORA ES tamanio_bloque_storage)
    void* buffer = recibir_buffer(socket_storage);
    
    if (buffer == NULL) {
        log_error(loggerWorker, "Error al recibir buffer de tamaño de bloque.");
        close(socket_storage);
        return NULL;
    }
    
    memcpy(&tamanio_bloque_storage, buffer, sizeof(int));
    if(tamanio_bloque_storage <= 0) {
        log_error(loggerWorker, "El tamaño del bloque de Storage es invalido.");
        close(socket_storage);
        return NULL;
    }
    free(buffer);

    log_info(loggerWorker, "Tamanio de pagina recibido de Storage: %d", tamanio_bloque_storage);
    log_info(loggerWorker, "Worker listo para interactuar con Storage");
    
    inicializar_memoria_interna();  // por el momento que este aca!!
    sem_post(&cond_storage_ready);
    return NULL; 
}

// ======================== Memoria Interna ================================

void inicializar_memoria_interna() {
    memoria.tamanio_marco = tamanio_bloque_storage; // viene del handshake
    memoria.tamanio_total = tam_memoria;            // viene de config
    memoria.cant_marcos = memoria.tamanio_total / memoria.tamanio_marco;
    memoria.puntero_clock = 0;
    //memoria.bloques = calloc(memoria.cant_bloques, sizeof(t_bloque_memoria)); 
    
    strcpy(memoria.algoritmo, config_struct->algoritmo_reemplazo);
    
    // -- unico malloc para los datos --
    memoria.buffer = malloc(memoria.tamanio_total);
    if (!memoria.buffer) {
        log_info(loggerWorker, "Error al reservar memoria interna (%d bytes)", memoria.tamanio_total);
        exit(EXIT_FAILURE); // o manejar error
    }

    // -- array de metadatos (metadata por marco) -- 
    memoria.marcos = calloc(memoria.cant_marcos, sizeof(t_marco));
    if (!memoria.marcos) {
        log_info(loggerWorker, "Error al reservar metadatos de marcos");
        free(memoria.buffer);
        exit(EXIT_FAILURE);
    }

    // Inicializar marcos (solo los campos fisicos)
    for (int i = 0; i < memoria.cant_marcos; ++i) {
        memoria.marcos[i].ocupado = false;
        //memoria.marcos[i].modificado = false;
        //memoria.marcos[i].en_uso = false;
        memoria.marcos[i].pagina_logica = -1;
        //memoria.marcos[i].ultima_ref = 0;
        memoria.marcos[i].file_tag[0] = '\0';
        //strcpy(memoria.marcos[i].file_tag, "");
    }

    pthread_mutex_init(&memoria.mutex, NULL);
    inicializar_tablas_paginas();

    log_info(loggerWorker, "Memoria interna inicializada: %d marcos de %d bytes (total %d bytes) - algoritmo = %s",
            memoria.cant_marcos, memoria.tamanio_marco, memoria.tamanio_total, memoria.algoritmo);
}

// Para liberar la memoria interna
void liberar_memoria_interna() {
    
    if(memoria.marcos == NULL) {
        log_info(loggerWorker, "No hay bloques en memoria para liberar");
        return;
    }
    
    pthread_mutex_lock(&memoria.mutex);

    if (memoria.buffer) {
        free(memoria.buffer);
        memoria.buffer = NULL;
    }

    if (memoria.marcos) {
        free(memoria.marcos);
        memoria.marcos = NULL;
    }
    
    pthread_mutex_unlock(&memoria.mutex);
    pthread_mutex_destroy(&memoria.mutex);

    log_info(loggerWorker, "Memoria Interna liberada correctamente.");
}


// Funciones base

// Me devuelve la direccion de inicio del marco dentro del buffer global
void* direccion_fisica_marco(int marco_id) {
    return (char*)memoria.buffer + (marco_id * memoria.tamanio_marco);
}

// Si necesito escribir hago -->
//void* ptr = direccion_fisica_marco(marco_id) + offset_en_marco;
//memcpy(ptr, contenido, tamanio);

//REVISAR FUNCIÓN

// Objetivo --> elegir el indice del marco fisico donde se va a cargar la pagina solicitada
int seleccionar_victima(int qid) {
    pthread_mutex_lock(&memoria.mutex);

    // 1ero: Busco marcos libres 
    for(int i = 0; i < memoria.cant_marcos; i++) {
        if(!memoria.marcos[i].ocupado) {
            //log_info(loggerWorker, "Query %d: Se asigna el Marco: %d a la Página: <NUMERO_PAGINA> perteneciente al - File: <FILE> - Tag: <TAG>”") LOG OBLIGATORIO DONDE LO PUEDO METER?
            log_info(loggerWorker, "Query %d: Seleccionando marco libre %d para cargar página.", qid, i);
            pthread_mutex_unlock(&memoria.mutex);
            return i;
        }
    }

    // 2do: No hay marcos libres --> aplicar algoritmo de reemplazo
    int victima = 0;

    if( strcmp(memoria.algoritmo, "CLOCK-M") == 0) {
        victima = reemplazo_clock_modificado(qid);
    }
    else if(strcmp(memoria.algoritmo, "LRU") == 0) {
        victima = reemplazo_lru(qid);
    }
    else {
        log_info(loggerWorker, "Algoritmo desconocido '%s', se usará marco 0 por defecto", memoria.algoritmo);
    }
    
    pthread_mutex_unlock(&memoria.mutex);
    return victima;

}

/*
int seleccionar_bloque_victima() {
    if( strcmp(memoria.algoritmo, "CLOCK-M") == 0) {
        return reemplazo_clock_modificado();
    }
    else if(strcmp(memoria.algoritmo, "LRU") == 0) {
        return reemplazo_lru();
    }
    return 0; // por defecto
}
*/


//Lo mejor sería que se llame bloque, porque lo mque le pedimos es el vontenido de un bloque ya que storage utiliza eso, no paginasx
t_motivo solicitar_bloque_a_storage(int qid, char* file_tag, int pagina_logica, t_marco* destino) {
    
    log_info(loggerWorker, "Query %d: Solicitando bloque a Storage - File: %s, Bloque: %d", qid, file_tag, pagina_logica);
    char* copia = strdup(file_tag);
    char* nombreArch;
    char* nombreTag;
    deserializar_fileTag(copia, &nombreArch, &nombreTag);

    t_paquete* paquete = crear_paquete(READ_BLOCK); 
    agregar_a_paquete(paquete, &qid, sizeof(int));
    agregar_a_paquete_string(paquete, nombreArch, strlen(nombreArch));
    agregar_a_paquete_string(paquete,nombreTag,strlen(nombreTag));
    agregar_a_paquete(paquete, &pagina_logica, sizeof(int));
    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);
    free(copia);
    //REVISAR EL CODE OP
    // Recibir contenido
    int op = recibir_operacion(socket_storage);
    t_motivo resultado = (t_motivo) op;
    if (resultado != RESULTADO_OK) {
        log_error(loggerWorker, "Error al recibir bloque solicitado de Storage para %s", file_tag);
        return resultado;
    }

    // Worker se bloquea esperando el BUFFER de datos
    int tamCont;
    int offset = 0;
    char *contenido;
    void* buffer = recibir_buffer(socket_storage);
    memcpy(&tamCont, buffer + offset, sizeof(int));
    offset += sizeof(int);
    contenido = malloc(tamCont + 1);
    memcpy(contenido, buffer + offset, tamCont);
    contenido[tamCont] = '\0';
    free(buffer); 
    
    // (Calculamos el índice del marco restando punteros)
    int num_marco = (destino - memoria.marcos);
    // (Calculamos la dirección física real dentro de 'memoria.buffer')
    void* direccion_fisica_destino = (char*)memoria.buffer + (num_marco * memoria.tamanio_marco);
    //memcpy(direccion_fisica_destino, buffer, memoria.tamanio_marco);
    memcpy(direccion_fisica_destino, contenido, memoria.tamanio_marco);

    // Actualizamos metadatos físicos del marco (solo file_tag/pagina/ocupado)
    strcpy(destino->file_tag, file_tag);
    //memcpy(destino->file_tag, file_tag, sizeof(destino->file_tag)-1);
    destino->pagina_logica = pagina_logica;
    destino->ocupado = true;
    
    return resultado;
}

t_motivo enviar_bloque_a_storage(int qid, char* file_tag, int nro_pagina_logica, void* contenido) {
    log_info(loggerWorker, "Query %d: Enviando a Storage (Write) -> Archivo: %s, Pagina: %d", 
             qid, file_tag, nro_pagina_logica);
    
    // --- 1. PARSEO DEL FILE_TAG ---
    char* copia = strdup(file_tag);
    char* nombreArch;
    char* nombreTag;
    deserializar_fileTag(copia, &nombreArch, &nombreTag);

    // --- PAQUETE ---
    t_paquete* paquete = crear_paquete(WRITE_BLOCK);
    agregar_a_paquete(paquete, &qid, sizeof(int));
    agregar_a_paquete_string(paquete, nombreArch, strlen(nombreArch)); 
    agregar_a_paquete_string(paquete, nombreTag, strlen(nombreTag));   
    agregar_a_paquete(paquete, &nro_pagina_logica, sizeof(int));
    agregar_a_paquete(paquete, contenido, tamanio_bloque_storage);
    //agregar_a_paquete_string(paquete, contenido, strlen(contenido));
    // --- 3. Calcular dirección y serializar datos ---
    //int num_marco = (bloque - memoria.marcos);
    //void* direccion_datos = memoria.buffer + (num_marco * memoria.tamanio_marco);

    //agregar_a_paquete(paquete, direccion_datos, tamanio_bloque_storage); 
    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);
    free(copia);

    // --- 4. Esperar respuesta ---
    int resultado = recibir_operacion(socket_storage);
    t_motivo motivo = (t_motivo) resultado;
    t_tabla_paginas* tablas_de_paginas = obtener_o_crear_tabla_paginas(file_tag);
    t_pagina *pagina = buscar_pagina(tablas_de_paginas, nro_pagina_logica);
    pagina->modificado = false;
    
    return motivo;
}


// ==================== Algoritmos de reemplazo ====================

/// Posible funcion de clock m
// CLOCK-M: recorre dos vueltas maximo.
// 1ero busca (U=0, M=0)
// 2do busca (U=0, M=1)
// Si no encuentra, elige el marco actual.
int reemplazo_clock_modificado(int qid) { 
    int vueltas = 0;
    int victima = -1;
    int cantidad_marcos = memoria.cant_marcos;

    while (true) {
        int idx = memoria.puntero_clock;
        t_marco* m = &memoria.marcos[idx];

        if (!m->ocupado) {
            victima = idx;
            break;

        } else {
            // obtengo la entrada de pagina correspondiente
            t_tabla_paginas* tabla = dictionary_get(tablas_de_paginas, m->file_tag);
            t_pagina* pte = NULL;
            if (tabla) pte = buscar_pagina(tabla, m->pagina_logica);

            // si no hay PTE (raro), lo tratamos como candidato
            bool uso = false, mod = false;
            if (pte) { uso = pte->uso; mod = pte->modificado; }
            else { uso = false; mod = false; }

            // Vuelta 1: buscar U=0, M=0
            if(vueltas == 0 && !uso && !mod) {
                victima = idx;
                break;
            }

            // Vuelta 2: buscar U=0, M=1
            if(vueltas == 1 && !uso && mod) {
                victima = idx;
                break;
            }

            // Si U=1 -> ponerlo a 0 (en la entrada de pagina)
            if (pte && pte->uso) {
                pte->uso = false;
            }

        }

        // Avanzar puntero circular
        memoria.puntero_clock = (memoria.puntero_clock + 1) % cantidad_marcos;

        // Si di una vuelta completa, paso a la segunda
        if (memoria.puntero_clock == 0) vueltas++;
        if (vueltas > 1) break; // si no encontro nada en dos vueltas
    }

    //if (victima == -1) victima = memoria.puntero_clock; // fallback ( Si no encontro nada, usa la posicion actual)
    memoria.puntero_clock = (victima + 1) % memoria.cant_marcos;

    // Log: obtener info de la página seleccionada para el log
    t_marco* marco_victima = &memoria.marcos[victima];
    t_tabla_paginas* tabla_victima = dictionary_get(tablas_de_paginas, marco_victima->file_tag);
    t_pagina* pagina_victima = (tabla_victima ? buscar_pagina(tabla_victima, marco_victima->pagina_logica) : NULL);

    log_info(loggerWorker, "Query %d: CLOCK-M selecciono marco victima: %d (U=%d, M=%d)",
            qid, victima,
            (pagina_victima ? pagina_victima->uso : -1),
            (pagina_victima ? pagina_victima->modificado : -1));

    return victima;
}

// Buscar bloque con la menor ultima_ref
int reemplazo_lru(int qid) {
    
    //time_t min_ref = time(NULL);
    time_t min_ref = LONG_MAX;
    int victima = -1;

    for(int i = 0; i < memoria.cant_marcos; i++) {
        t_marco* m = &memoria.marcos[i];
        // Bloque libre --> lo usamos directamente
        if(!m->ocupado){
            victima = i;
            log_info(loggerWorker, "LRU selecciono marco libre: %d", i);
            break;
        }
        t_tabla_paginas* tabla = dictionary_get(tablas_de_paginas, m->file_tag);
        if (!tabla) continue; // debería existir siempre
        t_pagina* p = buscar_pagina(tabla, m->pagina_logica);
        if (!p) continue;
        if (p->ultima_ref < min_ref) {
            min_ref = p->ultima_ref;
            victima = i;
        }
    }

    //if (victima == -1) victima = 0;
    log_info(loggerWorker, "Query %d: LRU selecciono marco victima: %d", qid, victima);
    return victima;
}

// =================== Tablas de Paginas =======================

void inicializar_tablas_paginas() {
    tablas_de_paginas = dictionary_create();
    pthread_mutex_init(&mutex_tablas_paginas, NULL);
}

void liberar_tablas_paginas() {
    dictionary_destroy_and_destroy_elements(tablas_de_paginas, (void*)liberar_tablas_paginas);
}

t_tabla_paginas* obtener_o_crear_tabla_paginas(char * file_tag) {
    t_tabla_paginas* tabla = dictionary_get(tablas_de_paginas, file_tag);
    if(!tabla) {
        log_info(loggerWorker, "Creando nueva Tabla de Páginas para %s", file_tag);
        tabla = malloc(sizeof(t_tabla_paginas));
        strcpy(tabla->file_tag, file_tag);
        tabla->paginas = list_create();
        dictionary_put(tablas_de_paginas, strdup(file_tag), tabla);
    }
    return tabla;
}

t_pagina* buscar_pagina(t_tabla_paginas* tabla, int num_pagina) {
    for (int i = 0; i < list_size(tabla->paginas); i++) {
        t_pagina* p = list_get(tabla->paginas, i);
        if (p->num_pagina == num_pagina)
            return p;
    }
    return NULL;
}

// ¿Podriamos hacer que devuelva el indice del marco (int)?

t_marco* obtener_marco_de_pagina(char* file_tag, int num_pagina) {
    t_tabla_paginas* tabla = obtener_o_crear_tabla_paginas(file_tag);
    t_pagina* pagina = buscar_pagina(tabla, num_pagina);
    
    if(pagina && pagina->presente) {
        int num_marco = pagina->marco; // 1. Obtiene el NÚMERO de marco
        t_marco* marco_fisico = &memoria.marcos[num_marco]; // 2. Busca el marco FÍSICO

        // 3. Actualiza la pagina
        //marco_fisico->en_uso = true;
        //marco_fisico->ultima_ref = time(NULL);
        pagina->uso = true;
        pagina->ultima_ref = time(NULL);
        
        return marco_fisico; // 4. Devuelve el puntero al marco FÍSICO
    }
    return NULL; // Pagina no está en memoria
}

// Devuelve el indice del marco o -1 si no esta en memoria
int obtener_indice_marco_de_pagina(char* file_tag, int num_pagina) {
    t_tabla_paginas* tabla = obtener_o_crear_tabla_paginas(file_tag);
    if (!tabla) return -1;

    t_pagina* pagina = buscar_pagina(tabla, num_pagina);
    if (!pagina || !pagina->presente) return -1;

    // Actualizamos bits en la entrada de pagina (bit de uso y ultima_ref)
    pagina->uso = true;
    pagina->ultima_ref = time(NULL);

    // Devolvemos el numero de marco
    return pagina->marco;
}

/*
 * =========================================================================
 * FUNCIÓN PRINCIPAL DE ACCESO A MEMORIA (El "Traductor")
 * =========================================================================
 * 1. Busca la Tabla de Páginas.
 * 2. Busca la Entrada de Página (PTE).
 * 3. Si (presente == false) -> Llama a manejar_page_fault (que veremos después).
 * 4. Si (presente == true) -> Actualiza bits de uso y devuelve el marco.
 * Devuelve el NÚMERO DE MARCO o -1 si hay error.

int traducir_direccion(char* file_tag, int num_pagina, int qid) {
    
    // 1. Buscar la "Tabla" (el índice) del archivo
    t_tabla_paginas* tabla = obtener_o_crear_tabla_de_paginas(file_tag);

    // 2. Buscar la "Entrada" (la fila) para esa página
    t_pagina* pte = obtener_pte(tabla, num_pagina); // pte = Page Table Entry

    // 3. Chequear el Bit de Presencia
    if (pte->presente == false) {
        // ¡PAGE FAULT! La página no está en RAM.
        log_warning(loggerWorker, "Query %d: Memoria Miss - File: %s - Pagina: %d", qid, file_tag, num_pagina);
        
        // Llamamos al "especialista" (¡Esta es la próxima función a crear!)
        if (!manejar_page_fault(tabla, pte, qid)) {
            return -1; // -1 significa error
        }
    }

    // 4. ¡PAGE HIT! (Sea porque ya estaba, o porque el Page Fault la trajo)
    
    // Actualizamos los bits de uso (para CLOCK/LRU)
    pthread_mutex_lock(&memoria.mutex);
    
    int num_marco = pte->marco;
    memoria.marcos[num_marco].en_uso = true;
    memoria.marcos[num_marco].ultima_ref = time(NULL);
    
    pthread_mutex_unlock(&memoria.mutex);
    
    return num_marco;
}
*/