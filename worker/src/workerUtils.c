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
void ejecutar_write(char* tag, int direccionBase, char* contenido, int qid) {
    int bloque_id = direccionBase / tamanio_bloque_storage;
    int offset_en_bloque = direccionBase % tamanio_bloque_storage;
    int tamanio_contenido = strlen(contenido);

    t_bloque_memoria* bloque = buscar_bloque(tag, bloque_id);

    // Si el bloque no está en memoria, hay que traerlo desde Storage
    if(!bloque) {
        bloque = manejar_bloque_noencontrado(bloque, tag, bloque_id);
        
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

    // Escribir contenido dentro del bloque (sin pasar los límites)
    if(offset_en_bloque + tamanio_contenido > tamanio_bloque_storage)
        tamanio_contenido = tamanio_bloque_storage - offset_en_bloque;

    pthread_mutex_lock(&memoria.mutex);
    
    memcpy(bloque->datos + offset_en_bloque, contenido, tamanio_contenido);
    bloque->modificado = true; // marcar como modificado
    bloque->en_uso = true; // marcar como en uso
    bloque->ultima_ref = time(NULL); // actualizar ultima referencia
    
    /*
    // La dirección física es el puntero al inicio del buffer del marco...
    void* puntero_al_marco = pagina_en_memoria->datos;
    // ...más el offset que calculamos.
    void* direccion_fisica_puntero = puntero_al_marco + offset_en_bloque;
    */

    pthread_mutex_unlock(&memoria.mutex);

    usleep((useconds_t)retardo_memoria * 1000); // Retardo por escritura en memoria

    log_info(loggerWorker, "Query %d: Accion: ESCRIBIR - Direccion Fisica: %d - Valor: %s", qid, direccionBase, contenido); // LOG OBLIGATORIO
}
}
/**
 * 3. WRITE <NOMBRE_FILE>:<TAG> <DIRECCIÓN BASE> <CONTENIDO>
 * Escribe en la Memoria Interna. Si la página no está, la trae de Storage.
 * Devuelve 'true' si fue exitoso, 'false' si hubo un error.
void ejecutar_write(char* file_tag, int direccionBase, char* contenido, int qid) {

    // --- 1. VALIDAR Y CALCULAR ---
    int bloque_id = direccionBase / tamanio_bloque_storage;
    int offset_en_bloque = direccionBase % tamanio_bloque_storage;
    int tamanio_contenido = strlen(contenido);

    // ERROR: Escritura fuera de límite
    // (Tu código anterior lo truncaba, pero el enunciado pide abortar)
    if (offset_en_bloque + tamanio_contenido > tamanio_bloque_storage) {
        log_error(loggerWorker, "Query %d: ERROR: Escritura excede el límite del bloque.", qid);
        notificar_fin_query_a_master(qid, "ERROR_ESCRITURA_FUERA_LIMITE");
        return false; // FRACASO: Aborta la query
    }

    usleep((useconds_t)retardo_memoria * 1000);

    // --- 3. ACCESO A MEMORIA (SEGURO) --
    pthread_mutex_lock(&memoria.mutex);

    t_bloque_memoria* pagina_en_memoria = buscar_bloque(file_tag, bloque_id);

    // --- 4. MANEJAR PAGE MISS (Lógica integrada) ---
    if (pagina_en_memoria == NULL) {

        // Pedimos los datos (LIBERAMOS EL LOCK para la I/O)
        pthread_mutex_unlock(&memoria.mutex);
        void* datos_de_storage = pedir_bloque_a_storage(qid, file_tag, bloque_id);
        pthread_mutex_lock(&memoria.mutex); // Volvemos a tomar el lock

        // Llamamos a tu algoritmo de reemplazo DIRECTAMENTE
        int indice_victima = seleccionar_bloque_victima(); 
        pagina_en_memoria = &memoria.bloques[indice_victima]; // bloque a usar

        // Si el bloque victima esta modificado, escribirlo a Storage
        if (pagina_en_memoria->ocupado && pagina_en_memoria->modificado) {
            
            // Liberamos lock para la I/O de escritura
            pthread_mutex_unlock(&memoria.mutex);
            bool ok = enviar_bloque_a_storage(pagina_en_memoria);
            pthread_mutex_lock(&memoria.mutex);

            if (!ok) {
                free(datos_de_storage);
                return false; // Error al guardar la víctima
            }
        }

        // Cargar los datos nuevos en el marco (como en tu función solicitar_bloque)
        memcpy(pagina_en_memoria->datos, datos_de_storage, tamanio_bloque_storage);
        free(datos_de_storage);

        // Actualizar metadata del marco
        strncpy(pagina_en_memoria->file_tag, file_tag, 127);
        pagina_en_memoria->file_tag[127] = '\0';
        pagina_en_memoria->bloque_id = bloque_id;
        pagina_en_memoria->ocupado = true;
        pagina_en_memoria->modificado = false; // "Limpio"
        
        // Log Obligatorio: Asignación
        log_info(loggerWorker, "Query %d: Se asigna el Marco: %d a la Página: %d perteneciente al File: %s Tag: <TAG>",
                 qid, indice_victima, bloque_id, file_tag);
    }

    // --- 5. ESCRIBIR EN MEMORIA (PAGE HIT o post-MISS) ---
    
    void* puntero_al_marco = pagina_en_memoria->datos;
    void* direccion_fisica_puntero = puntero_al_marco + offset_en_bloque;

    memcpy(direccion_fisica_puntero, contenido, tamanio_contenido);
    
    // Marcar flags
    pagina_en_memoria->modificado = true;
    pagina_en_memoria->en_uso = true;
    pagina_en_memoria->ultima_ref = time(NULL);

    // --- 6. LOG OBLIGATORIO Y DESBLOQUEO ---
    
    // Log Obligatorio: (Corregido para usar %p para el puntero)
    log_info(loggerWorker, "Query %d: Acción: ESCRIBIR Dirección Física: %p Valor: %s", 
             qid, 
             direccion_fisica_puntero, 
             contenido);
    
    pthread_mutex_unlock(&memoria.mutex);
    
    return true; // ÉXITO
}*/


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
    memoria.tamanio_bloque = tamanio_bloque_storage;
    memoria.tamanio_total = tam_memoria;
    memoria.cant_bloques = memoria.tamanio_total / memoria.tamanio_bloque;
    memoria.bloques = calloc(memoria.cant_bloques, sizeof(t_bloque_memoria)); 
    memoria.puntero_clock = 0;
    strcpy(memoria.algoritmo, config_struct->algoritmo_reemplazo);
    pthread_mutex_init(&memoria.mutex, NULL);

    for (int i = 0; i < memoria.cant_bloques; i++) {
        memoria.bloques[i].datos = malloc(memoria.tamanio_bloque);
        memoria.bloques[i].ocupado = false;
        memoria.bloques[i].modificado = false;
        memoria.bloques[i].en_uso = false;
    }

    log_info(loggerWorker, "Memoria Interna inicializada (%d bloques de %d bytes)",
            memoria.cant_bloques, memoria.tamanio_bloque);    
} 

// Para liberar la memoria interna
void liberar_memoria_interna() {
    if(memoria.bloques == NULL) {
        log_info(loggerWorker, "No hay bloques en memoria para liberar");
        return;
    }

    pthread_mutex_lock(&memoria.mutex);

    // Libero cada bloque
    for(int i = 0; i < memoria.cant_bloques; i++) {
        if (memoria.bloques[i].datos != NULL) {
            free(memoria.bloques[i].datos);
            memoria.bloques[i].datos = NULL;
        }
    }

    // Libero el array de bloques
    free(memoria.bloques);
    memoria.bloques = NULL;

    pthread_mutex_unlock(&memoria.mutex);
    pthread_mutex_destroy(&memoria.mutex);

    log_info(loggerWorker, "Memoria Interna liberada correctamente.");
}


// Funciones base

t_bloque_memoria* buscar_bloque(char* tag, int bloque_id) {
    for (int i = 0; i < memoria.cant_bloques; i++) {
        if (memoria.bloques[i].ocupado &&
            strcmp(memoria.bloques[i].file_tag, tag) == 0 &&
            memoria.bloques[i].bloque_id == bloque_id) {
            memoria.bloques[i].en_uso = true;
            memoria.bloques[i].ultima_ref = time(NULL);
            return &memoria.bloques[i];
        }
    }
    return NULL; // no encontrado el bloque
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

void solicitar_bloque_a_storage(char* tag, int bloque_id, t_bloque_memoria* destino){
    t_paquete* paquete = crear_paquete(SOLICITAR_BLOQUE);
    agregar_a_paquete_string(paquete, tag, strlen(tag));
    agregar_a_paquete(paquete, &bloque_id, sizeof(int));
    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);

    // Recibir contenido
    int op = recibir_operacion(socket_storage);
    if (op != ENVIAR_BLOQUE) {
        log_error(loggerWorker, "Error al recibir bloque solicitado de Storage para %s:%d", tag, bloque_id);
        return;
    }

    void* buffer = recibir_buffer(socket_storage);
    memcpy(destino->datos, buffer, memoria.tamanio_bloque);
    free(buffer);

    strcpy(destino->file_tag, tag);
    destino->bloque_id = bloque_id;
    destino->ocupado = true;
    destino->modificado = false;
    destino->en_uso = true;
    destino->ultima_ref = time(NULL);

    log_info(loggerWorker, "Bloque [%s:%d] cargado en memoria desde Storage.", tag, bloque_id);
}

void enviar_bloque_a_storage(t_bloque_memoria* bloque){
    
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

// --- Algoritmos de reemplazo ---

/// Posible funcion de clock m
int reemplazo_clock_modificado() { // revisar 
    int vueltas = 0;
    int victima = -1;

    while (true) {
        t_bloque_memoria* b = &memoria.bloques[memoria.puntero_clock];

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
        memoria.puntero_clock = (memoria.puntero_clock + 1) % memoria.cant_bloques;

        // Si di una vuelta completa, paso a la segunda
        if (memoria.puntero_clock == 0) vueltas++;
        if (vueltas > 1) break; // si no encontró nada en dos vueltas
    }

    if (victima == -1)
        victima = memoria.puntero_clock; // fallback

    log_info(loggerWorker, "CLOCK-M seleccionó bloque víctima: %d (U=%d, M=%d)",
            victima,
            memoria.bloques[victima].en_uso,
            memoria.bloques[victima].modificado);

    // Avanzar el puntero para la próxima ejecución
    memoria.puntero_clock = (victima + 1) % memoria.cant_bloques;

    return victima;
}

// Posible funcion de lru
int reemplazo_lru() {
    // Buscar bloque con la menor ultima_ref
    time_t min_ref = time(NULL);
    int victima = 0;

    for(int i = 0; i < memoria.cant_bloques; i++) {
        if(!memoria.bloques[i].ocupado) {
            // Bloque libre --> lo usamos directamente
            log_info(loggerWorker, "LRU seleccionó bloque libre: %d", i);
            return i;
        }
        if(memoria.bloques[i].ultima_ref < min_ref) {
            min_ref = memoria.bloques[i].ultima_ref;
            victima = i;
        }
    }

    return victima;
}
