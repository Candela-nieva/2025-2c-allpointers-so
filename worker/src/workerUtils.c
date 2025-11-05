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
pthread_cond_t cond_storage_ready = PTHREAD_COND_INITIALIZER;
bool storage_ready = false; 

// ----------------------

#define LONG_MAX

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
    free(arg); // Liberamos el puntero
    
    // Esperamos a que el storage esté listo antes de proceder
    pthread_mutex_lock(&mutex_storage_ready);
    while(!storage_ready) {
        log_info(loggerWorker, "Esperando a que Storage esté listo antes de conectar con Master...");
        pthread_cond_wait(&cond_storage_ready, &mutex_storage_ready);
    }
    pthread_mutex_unlock(&mutex_storage_ready);

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
    eliminar_paquete(paquete);
    log_info(loggerWorker, "Handshake enviado a Master - Worker ID enviado: %d", id_worker);
    
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


        // ???????
        /*if(cod_op != EJECUTAR){
            log_info(loggerWorker, "Error al recibir cod_op de Master, se esperaba EJECUTAR"); // esta entrando aqui cuando lo compilamos, no sé si con lo que tenemos hasta el momento era lo esperado o si esta fallando
            return;
        }*/
        
        switch(cod_op) {
            case EJECUTAR: // Master avisa que hay una nueva query para que este worker ejecute
                void* buffer = recibir_buffer(socket_master);
                pthread_t ejecutar;
                pthread_create(&ejecutar, NULL, manejar_ejecutar, buffer);
                pthread_detach(ejecutar);
                log_info(loggerWorker, "Se recibio la operacion EJECUTAR");
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
    while(true){
        log_info(loggerWorker, "EJECUTANDO....EJECUTANDO....");
        usleep(3000000);
        ++pc;
        log_info(loggerWorker, "PC actualizado a %d.", pc);
        log_info(loggerWorker, "Cheque Interrupcion");
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

    }
    
    //ejecutar_query(pc, archivo, qid); ---> ESTO HAY QUE DESCOMENTARLO LUEGO!!
    log_info(loggerWorker, "FIN DE QUERY ACTUAL");
    free(archivo);

    return NULL;
}

//////////////////////////////// query interpreter ////////////////////////////

// Ejecuta las instrucciones de un archivo de query desde una línea específica (pc_inicial)

//revisar si la vamos a necesitar
/*static void notificar_fin_query(int qid, const char* motivo) { 
    t_paquete* p = crear_paquete(WORKER_TO_MASTER_END);
    agregar_a_paquete(&qid, sizeof(int));
    agregar_a_paquete_string(p, motivo, 0);
    enviar_paquete(p, socket_master);
    eliminar_paquete(p);
}*/
//prueba

static void notificar_fin_query_a_master(int qid, const char* motivo) { 
    t_paquete* p = crear_paquete(WORKER_TO_MASTER_END); // (Necesitas este op_code en protocolo.h)
    agregar_a_paquete(p, &qid, sizeof(int));
    agregar_a_paquete_string(p, (char*)motivo, strlen(motivo));
    enviar_paquete(p, socket_master);
    eliminar_paquete(p);
}


// Ejecuta las instrucciones de un archivo de query desde una línea específica (pc_inicial)
void ejecutar_query(int pc_inicial, const char* archivo_relativo, int qid) {
    
    bool es_end =false;

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
    
    char linea[2048];
    int pc = 0;
    
    // 1. Bucle para saltar hasta pc_inicial (para reanudar queries)
    // Si pc_inicial = 5, este bucle se ejecuta 5 veces (pc = 0, 1, 2, 3, 4)
    while (pc < pc_inicial) {
        if (fgets(linea, sizeof(linea), file) == NULL) {
            // El PC está fuera de los límites del archivo
            log_info(loggerWorker, "Query %d: PC %d fuera de rango (EOF).", qid, pc_inicial);
            notificar_fin_query_a_master(qid, "EOF");
            fclose(file);
            return;
        }
        pc++;
    }

    // 2. Bucle principal de ejecución
    // Leemos la primera línea a ejecutar (la que corresponde a pc_inicial)
    while (fgets(linea, sizeof(linea), file) != NULL) {
        
        trim_newline(linea);

        log_info(loggerWorker, "## Query %d: FETCH - Program Counter: %d - %s", qid, pc, linea);
        
        es_end = ejecutar_instruccion(linea, qid, pc);
        
        if (es_end) {
            log_info(loggerWorker, "##Query %d: Query finalizada (END)", qid);
            notificar_fin_query_a_master(qid, "OK");
            break; // Salir del bucle while
        }
        
        // El retardo ahora debe estar DENTRO de 'ejecutar_instruccion',
        // solo en los casos READ y WRITE.
        
        // Comprobar interrupción (desalojo)
        if (atomic_load(&hay_interrupt) == 1) {
            atomic_store(&hay_interrupt, 0);
            
    
            int pc_siguiente = pc + 1;
            
            t_paquete *paquete = crear_paquete(PC_ACTUALIZADO);
            agregar_a_paquete(paquete, &pc_siguiente, sizeof(int)); 
            enviar_paquete(paquete, socket_master);
            eliminar_paquete(paquete);
            
            log_info(loggerWorker, "## Query %d: Desalojada por pedido del Master. PC guardado: %d", qid, pc_siguiente);
            fclose(file);
            return; // Salir de la función (el hilo muere)
        }

        pc++; // Avanzar al siguiente Program Counter
    
    }

    // Si salimos del bucle y no fue por 'END' (es_end=false), fue por Fin de Archivo (EOF)
    if (!es_end) {
        log_info(loggerWorker, "##Query %d: Fin de archivo alcanzado (EOF).", qid);
        notificar_fin_query_a_master(qid, "EOF");
    }
    
    fclose(file);
}



// Limpia las líneas leídas de archivos para que no tengan saltos de línea al final, 
// y así procesarlas correctamente en el query interpreter.
static void trim_newline(char* s) {
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
static bool ejecutar_instruccion(const char* instruccion, int qid, int pc) {
    if(!instruccion) return false;
    
    char* copia = strdup(instruccion);
    trim_newline(copia);

    char* op = strtok(copia, " ");     
    if(!op) {
        log_info(loggerWorker, "Query %d: - Instrucción vacía en PC=%d", qid, pc);
        free(copia);
        return false;
    }

    // Guardamos la operación para el log obligatorio
    char instruccion_log[256];
    strncpy(instruccion_log, op, 255);

    bool fue_end = false;
    
    tipo_instruccion inst_tipo = obtener_instruccion(op);

    // 2. Usamos el enum en el switch
    switch (inst_tipo) {
        case CREATE: {
            char* file_tag = strtok(NULL, " "); // agarro el nombre del archivo
            ejecutar_create(file_tag);
            break;
        }
        case TRUNCATE: {
            char* file_tag = strtok(NULL, " "); // agarro el nombre del archivo
            char* tam_str = strtok(NULL, " "); // agarro el nuevo tamaño
            int nuevo_tam = (tam_str != NULL) ? atoi(tam_str) : 0;
            ejecutar_truncate(file_tag, nuevo_tam);
            break;
        }
        case WRITE: {
            char* file_tag = strtok(NULL, " "); // agarro el nombre del archivo
            char* dir_str = strtok(NULL, " "); // agarro la direccion
            char* contenido = strtok(NULL, ""); // agarro el contenido
            int direccion = (dir_str != NULL) ? atoi(dir_str) : 0;

            // RETARDO_MEMORIA: Solo se aplica en READ y WRITE
            usleep((useconds_t)retardo_memoria * 1000);
            //ejecutar_write(file_tag, direccion, contenido, qid); 
            break;
        }
        case READ: {
            char* file_tag = strtok(NULL, " ");
            char* dir_str = strtok(NULL, " ");
            char* tam_str = strtok(NULL, " ");
            int direccion = (dir_str != NULL) ? atoi(dir_str) : 0;
            int tam = (tam_str != NULL) ? atoi(tam_str) : 0;

            // RETARDO_MEMORIA: Solo se aplica en READ y WRITE
            usleep((useconds_t)retardo_memoria * 1000);
            //ejecutar_read(file_tag, direccion, tam, qid);
            break;
        }
        case TAG: {
            char* origen = strtok(NULL, " ");
            char* destino = strtok(NULL, ""); // El resto
            //ejecutar_tag(origen, destino, qid);
            break;
        }
        case COMMIT: {
            char* file_tag = strtok(NULL, " ");
            //ejecutar_commit(file_tag, qid);
            break;
        }
        case FLUSH: {
            char* file_tag = strtok(NULL, " ");
            //ejecutar_flush(file_tag, qid);
            break;
        }
        case DELETE: {
            char* file_tag = strtok(NULL, " ");
            //ejecutar_delete(file_tag, qid);
            break;
        }
        case END: {
            fue_end = true;
            break;
        }
        case DESCONOCIDA:
        default: {
            log_error(loggerWorker, "Query %d: - Instrucción desconocida: %s", qid, instruccion);
            // NOTA: Una instrucción desconocida podría ser un error fatal que termine la query.
            // notificar_fin_query_a_master(qid, "ERROR_INSTRUCCION_DESCONOCIDA");
            // fue_end = true; // Para que el bucle pare
            break;
        }
    }

    // LOG OBLIGATORIO: Instrucción realizada
    log_info(loggerWorker, "## Query %d: Instrucción realizada: %s", qid, instruccion_log);

    free(copia);
    return fue_end;
}

// ==== Funciones de ejecucion de instrucciones ====
// Existe la funcion de agregar a paquete un string tmb!! En protocolo

// CREATE
void ejecutar_create(char* file_tag){
    t_paquete* paquete = crear_paquete(CREATE);
    //int tam = strlen(file_tag) + 1;
    agregar_a_paquete_string(paquete, file_tag, strlen(file_tag));
    int tamanio = 0;
    agregar_a_paquete(paquete, &tamanio, sizeof(int));

    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);
    //log_info(loggerWorker, "Instrucción CREATE enviada a Storage para el tag: %s", file_tag);

    //Esperamos la confirmacion de storage (errores en las operaciones) 
    if(recibir_operacion(socket_storage) != CREATE_OK){
        log_info(loggerWorker, "Instrucción CREATE fallida para el tag: %s", file_tag);
    }
}

// TRUNCATE
void ejecutar_truncate(char* tag, int nuevo_tam) {
    log_info(loggerWorker, "TRUNCATE solicitado en tag: %s, nuevo tamaño: %d", tag, nuevo_tam); // LOG NO OBLIGATORIO
    
    if(tamanio_bloque_storage <= 0) {
        log_info(loggerWorker, "Error: Tamaño de bloque de Storage no válido. No se puede ejecutar TRUNCATE.");
        return;
    }
    //logica multiplo
    if(nuevo_tam % tamanio_bloque_storage != 0) {
        log_info(loggerWorker, "Error: El nuevo tamaño %d NO es múltiplo del tamaño de bloque %d. TRUNCATE abortado.", nuevo_tam, tamanio_bloque_storage);
        return;
    }

    t_paquete* paquete = crear_paquete(TRUNCATE);
    agregar_a_paquete_string(paquete, tag, strlen(tag));
    agregar_a_paquete(paquete, &nuevo_tam, sizeof(int));
    enviar_paquete(paquete, socket_storage);  // se envia a Storage
    eliminar_paquete(paquete);
 
    // Esperamos la confirmacion de storage (errores en las operaciones)
    if(recibir_operacion(socket_storage) != TRUNCATE_OK){
        log_info(loggerWorker, "Instrucción TRUNCATE fallida para el tag: %s", tag);
    }
    
    log_info(loggerWorker, "Instrucción TRUNCATE enviada a Storage para el tag: %s, nuevo tamaño: %d", tag, nuevo_tam); // LOG NO OBLIGATORIO
}

// WRITE
void ejecutar_write(char* tag, int direccion_base, char* contenido, int qid) {
    
    // Y QUE PASA SI NO ESTA CREADO ESE FILE TAG?

    t_tabla_paginas* tabla = obtener_o_crear_tabla_paginas(tag);
    
    int pagina_logica = direccion_base / tamanio_bloque_storage;
    int offset = direccion_base % tamanio_bloque_storage;
    int tamanio_contenido = strlen(contenido);

    t_pagina* pagina = buscar_pagina(tabla, pagina_logica);
    //t_bloque_memoria* bloque = buscar_bloque(tag, bloque_id);

    // Si no esta presente --> PAGE FAULT
    if(!pagina || !pagina->presente) {
        pagina = manejar_page_fault(tag, pagina_logica, tabla, qid);
    }

    int marco = pagina->marco;
    void* direccion_marco = memoria.buffer + (marco * tamanio_bloque_storage) + offset;
    void* direccion_marquitos = direccion_fisica_marco(marco) + offset;

    pthread_mutex_lock(&memoria.mutex);

    // Escrbiendo...... (limitando a lo que entra en el bloque)
    if(offset + tamanio_contenido > memoria.tamanio_marco)
        tamanio_contenido = memoria.tamanio_marco - offset;

    memcpy(direccion_marco, contenido, tamanio_contenido);

    // Actualizar metadata de la pagina...
    pagina.modificado = true;
    pagina.uso = true;
    pagina.ultima_ref = time(NULL);

    pthread_mutex_unlock(&memoria.mutex);

    
    // Si el bloque no está en memoria, hay que traerlo desde Storage
    if(!bloque) {
        //bloque = manejar_bloque_noencontrado(bloque, tag, bloque_id);
        
        if (!bloque) {
                log_error(loggerWorker, "Query %d: ERROR page fault al traer %s:%d", qid, tag, bloque_id);
                notificar_fin_query_a_master(qid, "ERROR_TRAER_BLOQUE");
                return;
        
        /*
        // Buscar victima o espacio libre
        int victima = seleccionar_bloque_victima();
        bloque = &memoria.bloques[victima]; // bloque a usar

        // Si el bloque victima esta modificado, escribirlo a Storage
        if(bloque->ocupado && bloque->modificado) {
            enviar_bloque_a_storage(bloque)
        }

        // Solcitar bloque a Storage (para tener su contenido actual)
        solicitar_bloque_a_storage(tag, bloque_id, bloque);
        */
        
        }
    }
    

    usleep((useconds_t)retardo_memoria * 1000); // Retardo por escritura en memoria

    log_info(loggerWorker, "Query %d: Accion: ESCRIBIR - Direccion Fisica: %d - Valor: %s", 
        qid, direccionBase, contenido); // LOG OBLIGATORIO
}

t_pagina* manejar_page_fault(char* file_tag, int pagina_logica, t_tabla_paginas* tabla, int qid) {
    log_info(loggerWorker, "Query %d: Page Fault en %s página %d", qid, file_tag, pagina_logica);

    int indice_marco_victima = seleccionar_victima(qid);

    t_marco* marco_victima = &memoria.marcos[indice_marco_victima];  

    if(marco_victima.ocupado) {
        char* file_tag_victima = marco_victima->file_tag;
        int num_pagina_victima = marco_victima->pagina_logica;
        // Buscar su tabla y entrada de página
        pthread_mutex_lock(&mutex_tablas_paginas);
        t_tabla_paginas* tabla_victima = dictionary_get(tablas_de_paginas, file_tag_victima);
        t_pagina* pagina_victima = buscar_pagina(tabla_victima, num_pagina_victima);

        if(pagina_victima) {
            if(pagina_victima->modificado) {
                log_info(loggerWorker, "Query %d: Guardando página modificada %s:%d en Storage antes de reemplazo",
                     qid, marco->file_tag, marco->pagina_logica);
                //enviar_pagina_a_storage(file_tag_victima, num_pagina_victima, marco_victima);
            }
            pagina_victima->presente = false;
        }
        pthread_mutex_unlock(&mutex_tablas_paginas);
    }

    // Pedimos a storage la nueva pagina
    //solicitar_pagina_a_storage(file_tag, pagina_logica, marco_victima);

    // Actualizamos metadata del marco
    strcpy(marco->file_tag, file_tag);
    marco->pagina_logica = pagina_logica;
    marco->ocupado = true;
    marco->modificado = false;
    marco->en_uso = true;
    marco->ultima_ref = time(NULL);

    // Creamos o actualizamos entrada de tabla de páginas
    t_pagina* nueva = buscar_pagina(tabla, pagina_logica);
    
    if(!nueva) {
        nueva = malloc(sizeof(t_pagina));
        nueva->num_pagina = pagina_logica;
        list_add(tabla->paginas, nueva);
    }

    nueva->marco = marco_victima;
    nueva->presente = true;
    nueva->modificado = false;
    nueva->uso = true;
    nueva->ultima_ref = time(NULL);

    return nueva;
}


// TODO: completar las demas instrucciones (con el tema de incorporar memoria interna y demas)

// Para cada referencia lectura o escritura del Query Interpreter a una página de la Memoria Interna, se 
// deberá esperar un tiempo definido por archivo de configuración (RETARDO_MEMORIA). 




// =================== CONEXION A STORAGE ======================= 
// 1ero) Iniciar conexion a storage

void* iniciar_conexion_storage(void* arg){ 
    (void)arg; // Evitar warning de variable no usada (porque no usamos argumento en este caso)

    socket_storage = crear_conexion(config_struct->ip_storage, config_struct->puerto_storage);
    if(socket_storage == -1) {
        log_info(loggerWorker, "Error al crear la conexión con Storage");
        pthread_exit(NULL); // Terminar el hilo si hay un error
    }
    
    // Enviar handshake a Storage
    enviar_operacion(socket_storage, HANDSHAKE_WORKER); // Enviar el handshake a Memoria
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
  //hola jaja
 // tuve que cambiar los loggers de cargar_config de qc y worker a fprintfs solo para testeo por ahora, estabamos usando loggers antes de seren creados 
    
    // TAMAÑO BLOQUE = TAMAÑO PAGINA
    //int tamanio_pag = 0; // ahora es global pq necesitabamos usarla en otras funciones (AHORA ES tamanio_bloque_storage)
    void* buffer = recibir_buffer(socket_storage);
    
    if (buffer == NULL) {
        log_error(loggerWorker, "Error al recibir buffer de tamaño de bloque.");
        close(socket_storage);
        return NULL;
    }
    
    memcpy(&tamanio_bloque_storage, buffer, sizeof(int));
    free(buffer);

    log_info(loggerWorker, "Tamanio de pagina recibido de Storage: %d", tamanio_bloque_storage);
    log_info(loggerWorker, "Worker listo para interactuar con Storage");
    
    // Señalamos que el storage ya está listo
    pthread_mutex_lock(&mutex_storage_ready); 
    storage_ready = true;
    pthread_cond_signal(&cond_storage_ready);
    pthread_mutex_unlock(&mutex_storage_ready);

    inicializar_memoria_interna();  // por el momento que este aca!!

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
    
    // Inicializar marcos
    for (int i = 0; i < memoria.cant_marcos; ++i) {
        memoria->marcos[i].ocupado = false;
        memoria->marcos[i].modificado = false;
        memoria->marcos[i].en_uso = false;
        memoria->marcos[i].pagina_logica = -1;
        memoria->marcos[i].ultima_ref = 0;
        memoria->marcos[i].file_tag[0] = '\0';
        //strcpy(memoria.marcos[i].file_tag, "");
    }

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
    return memoria.buffer + (marco_id * memoria.tamanio_marco);
}

// Si necesito escribir hago -->
//void* ptr = direccion_fisica_marco(marco_id) + offset_en_marco;
//memcpy(ptr, contenido, tamanio);

//REVISAR FUNCIÓN

int seleccionar_victima(int qid) {
    
}

int seleccionar_bloque_victima() {
    if( strcmp(memoria.algoritmo, "CLOCK-M") == 0) {
        return reemplazo_clock_modificado();
    }
    else if(strcmp(memoria.algoritmo, "LRU") == 0) {
        return reemplazo_lru();
    }
    return 0; // por defecto
}

/*
//HAY QUE REVISAR ESTA FUNCION
void solicitar_pagina_a_storage(char* tag, int bloque_id, t_marco* destino){
    t_paquete* paquete = crear_paquete(SOLICITAR_BLOQUE);
    agregar_a_paquete_string(paquete, tag, strlen(tag));
    agregar_a_paquete(paquete, &bloque_id, sizeof(int));
    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);
    //REVISAR EL CODE OP
    // Recibir contenido
    int op = recibir_operacion(socket_storage);
    if (op != ENVIAR_BLOQUE) {
        log_error(loggerWorker, "Error al recibir bloque solicitado de Storage para %s:%d", tag, bloque_id);
        return;
    }

    void* buffer = recibir_buffer(socket_storage);
    memcpy(destino->datos, buffer, memoria.tamanio_marco);
    free(buffer);

    strcpy(destino->file_tag, tag);
    destino->bloque_id = bloque_id;
    destino->ocupado = true;
    destino->modificado = false;
    destino->en_uso = true;
    destino->ultima_ref = time(NULL);

    log_info(loggerWorker, "Bloque [%s:%d] cargado en memoria desde Storage.", tag, bloque_id);
}
*/

/*
//HAY QUE REVISAR EL ENVIAR
void enviar_pagina_a_storage(t_marco* bloque){
    
    t_paquete* paquete = crear_paquete(ESCRIBIR_BLOQUE); // (Necesitas este op_code en protocolo.h)
    
    agregar_a_paquete_string(paquete, bloque->file_tag, strlen(bloque->file_tag) + 1);
    agregar_a_paquete(paquete, &bloque->bloque_id, sizeof(int));
    agregar_a_paquete(paquete, bloque->datos, tamanio_bloque_storage); 
    
    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);

    //bloque->modificado = false; // ya fue escrito

    // Esperamos la respuesta de Storage (errores en las operaciones)
    op_code respuesta = recibir_operacion(socket_storage);
    
    if (respuesta != STORAGE_OK) { // (Necesitas STORAGE_OK en protocolo.h)
        log_error(loggerWorker, "QuerY: Storage respondió ERROR (%d) al escribir bloque.", respuesta);
        //notificar_fin_query_a_master( "ERROR_ESCRITURA_STORAGE"); // Aborta la query
        return false; // Devuelve FRACASO
    }
    return true; // Devuelve ÉXITO
}
*/

// --- Algoritmos de reemplazo ---

/// Posible funcion de clock m
int reemplazo_clock_modificado() { // revisar 
    int vueltas = 0;
    int victima = -1;

    while (true) {
        t_marco* b = &memoria.marcos[memoria.puntero_clock];

        if (b->ocupado) {
            // Vuelta 1: buscar U=0, M=0
            if (vueltas == 0 && !b->en_uso && !b->modificado) {
                victima = memoria.puntero_clock;
                break;
            }
            // Vuelta 2: buscar U=0, M=1
            else if (vueltas == 1 && !b->en_uso && b->modificado) {
                victima = memoria.puntero_clock;
                break;
            }

            // Si U=1 → lo pongo en 0
            if (b->en_uso) {
                b->en_uso = false;
            }
        } else {
            // Bloque libre → lo usamos directamente
            victima = memoria.puntero_clock;
            break;
        }

        // Avanzar puntero circular
        memoria.puntero_clock = (memoria.puntero_clock + 1) % memoria.cant_marcos;

        // Si di una vuelta completa, paso a la segunda
        if (memoria.puntero_clock == 0) vueltas++;
        if (vueltas > 1) break; // si no encontró nada en dos vueltas
    }

    if (victima == -1)
        victima = memoria.puntero_clock; // fallback

    log_info(loggerWorker, "CLOCK-M seleccionó bloque víctima: %d (U=%d, M=%d)",
            victima,
            memoria.marcos[victima].en_uso,
            memoria.marcos[victima].modificado);

    // Avanzar el puntero para la próxima ejecución
    memoria.puntero_clock = (victima + 1) % memoria.cant_marcos;

    return victima;
}

// Posible funcion de lru
int reemplazo_lru() {
    // Buscar bloque con la menor ultima_ref
    time_t min_ref = time(NULL);
    int victima = 0;

    for(int i = 0; i < memoria.cant_marcos; i++) {
        if(!memoria.marcos[i].ocupado) {
            // Bloque libre --> lo usamos directamente
            log_info(loggerWorker, "LRU seleccionó bloque libre: %d", i);
            return i;
        }
        if(memoria.marcos[i].ultima_ref < min_ref) {
            min_ref = memoria.marcos[i].ultima_ref;
            victima = i;
        }
    }

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

/*
/**
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