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
            case DESALOJO :
                log_info(loggerWorker, "Master ha indicado DESALOJO de Query actual.");
                atomic_store(&hay_interrupt,1);
                break;

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

// Parsea y ejecuta una instrucción individual
static void ejecutar_instruccion(const char* instruccion, int qid, int pc) {
    if(!instruccion) return;
    
    char* copia = strdup(instruccion); // Hacemos una copia para no modificar la original
    trim_newline(copia);               // Limpiamos la copia

    char* op = strtok(copia, " ");     // Tokenizamos para obtener la operación
    if(!op) {
        log_info(loggerWorker, "Query %d: - Instrucción vacía en PC=%d", qid, pc);
        free(copia);
        return;
    }

    if(strcmp(op, "CREATE") == 0) {
        char* tag = strtok(NULL, " ");
        log_info(loggerWorker, "Query %d: - Instrucción realizada: CREATE", qid);
        ejecutar_create(tag);
    }

    else if (strcmp(op, "TRUNCATE") == 0) {
        char* archivo_tag = strtok(NULL, " ");
        char* tam = strtok(NULL, " ");
        log_info(loggerWorker, "Query %d: - Instrucción realizada: TRUNCATE", qid);
        // STUB
    }
    
    else if (strcmp(op, "WRITE") == 0) {
        char* archivo_tag = strtok(NULL, " ");
        char* direccion = strtok(NULL, " ");
        char* contenido = strtok(NULL, ""); // resto con espacios
        log_info(loggerWorker, "Query %d: - Instrucción realizada: WRITE", qid);
        // STUB: escribir en memoria (o solicitar páginas)
    }
    
    else if (strcmp(op, "READ") == 0) {
        char* archivo_tag = strtok(NULL, " ");
        char* direccion = strtok(NULL, " ");
        char* tam = strtok(NULL, " ");
        log_info(loggerWorker, "Query %d: - Instrucción realizada: READ", qid);
        log_info(loggerWorker, "## Se envía un mensaje de lectura de la Query %d al Master (simulado).", qid);
    }

    else if (strcmp(op, "TAG") == 0) {
        char* origen = strtok(NULL, " ");
        char* destino = strtok(NULL, "");
        log_info(loggerWorker, "Query %d: - Instrucción realizada: TAG", qid);
    }
    
    else if (strcmp(op, "COMMIT") == 0) {
        char* archivo_tag = strtok(NULL, " ");
        log_info(loggerWorker, "Query %d: - Instrucción realizada: COMMIT", qid);
    }
    
    else if (strcmp(op, "FLUSH") == 0) {
        char* archivo_tag = strtok(NULL, " ");
        log_info(loggerWorker, "Query %d: - Instrucción realizada: FLUSH", qid);
    }
    
    else if (strcmp(op, "DELETE") == 0) {
        char* archivo_tag = strtok(NULL, " ");
        log_info(loggerWorker, "Query %d: - Instrucción realizada: DELETE", qid);
    }
    
    else if (strcmp(op, "END") == 0) {
        log_info(loggerWorker, "Query %d: - Instrucción realizada: END", qid);
    }
    
    else {
        log_info(loggerWorker, "Query %d: - Instrucción desconocida: %s", qid, instruccion);
    }

    free(copia);

}


// ==== Funciones de ejecucion de instrucciones ====

void ejecutar_create(char* tag) {
    t_paquete* paquete = crear_paquete(CREATE);
    int tam = strlen(tag) + 1;
    agregar_a_paquete(paquete, tag, tam);
    enviar_paquete(paquete, socket_storage);  // se envia a Storage
    eliminar_paquete(paquete);

    log_info(loggerWorker, "Instrucción CREATE enviada a Storage para el tag: %s", tag); // LOG NO OBLIGATORIO

    // Ojo que es con tamaño 0 el nuevo archivo
}

void ejecutar_truncate(char* tag, int nuevo_tam) {
    log_info(loggerWorker, "TRUNCATE solicitad en tag: %s, nuevo tamaño: %d", tag, nuevo_tam); // LOG NO OBLIGATORIO
    
    if(tamanio_bloque_storage <= 0) {
        log_info(loggerWorker, "Error: Tamaño de bloque de Storage no válido. No se puede ejecutar TRUNCATE.");
        return;
    }

    if(nuevo_tam % tamanio_bloque_storage != 0) {
        log_info(loggerWorker, "Error: El nuevo tamaño %d NO es múltiplo del tamaño de bloque %d. TRUNCATE abortado.", nuevo_tam, tamanio_bloque_storage);
        return;
    }

    t_paquete* paquete = crear_paquete(TRUNCATE);
    int tam = strlen(tag) + 1;
    agregar_a_paquete(paquete, tag, tam);
    agregar_a_paquete(paquete, &nuevo_tam, sizeof(int));
    enviar_paquete(paquete, socket_storage);  // se envia a Storage
    eliminar_paquete(paquete);

    log_info(loggerWorker, "Instrucción TRUNCATE enviada a Storage para el tag: %s, nuevo tamaño: %d", tag, nuevo_tam); // LOG NO OBLIGATORIO
}

void ejecutar_write(char* tag, int direccion, char* contenido) {
    // TODO
}


// TODO: completar las demas instrucciones (con el tema de incorporar memoria interna y demas)

// Para cada referencia lectura o escritura del Query Interpreter a una página de la Memoria Interna, se 
// deberá esperar un tiempo definido por archivo de configuración (RETARDO_MEMORIA). 







// Ejecuta las instrucciones de un archivo de query desde una línea específica (pc_inicial)
void ejecutar_query(int pc_inicial, const char* archivo_relativo, int qid) {
    
    if(!archivo_relativo) {
        log_info(loggerWorker, "Error: Ruta de archivo de Query %d no proporcionada.", qid);
        return;
    }
    
    // Construir la ruta completa al archivo de query (PATH_SCRIPTS + "/" + archivo_relativo)
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
    // Saltar hasta pc_inicial
    while (fgets(linea, sizeof(linea), file) != NULL) {
        if (pc < pc_inicial) {
            ++pc;
            continue;
        }

        trim_newline(linea);

        // LOG OBLIGATORIO
        log_info(loggerWorker, "## Query %d: FETCH - Program Counter: %d - %s", qid, pc, linea);
        //Fetch Instrucción: “## Query <QUERY_ID>: FETCH - Program Counter: <PROGRAM_COUNTER> - <INSTRUCCIÓN>3”. 
        
        // Ejecutar la instrucción
        ejecutar_instruccion(linea, qid, pc);
        
        // Retardo simulado
        if (retardo_memoria > 0) { // retardoo segun instruc
            usleep((useconds_t)retardo_memoria * 1000);
        }

        ++pc;

        // Comprobar interrupción (desalojo)
        if (atomic_load(&hay_interrupt) == 1) {
            atomic_store(&hay_interrupt, 0);
            t_paquete *paquete = crear_paquete(PC_ACTUALIZADO);
            agregar_a_paquete(paquete, &pc, sizeof(int));
            enviar_paquete(paquete, socket_master);
            eliminar_paquete(paquete);
            log_info(loggerWorker, "PC %d enviado a master tras desalojo.", pc);
            fclose(file);
            return;
        }

        // Si era END terminamos la Query
        if (strncmp(linea, "END", 3) == 0) {
            log_info(loggerWorker, "##Query %d: Query finalizada (END)", qid);
            fclose(file);
            return;
        }
    }

    log_info(loggerWorker, "##Query %d: Fin de archivo alcanzado (EOF).", qid);
    fclose(file);
}

// ==== Funciones de ejecucion de instrucciones ====
void ejecutar_create(char* file_tag){
    t_paquete* paquete = crear_paquete(CREATE);
    int tam = strlen(file_tag) + 1;
    agregar_a_paquete(paquete, file_tag, tam);
    int tamanio = 0;
    agregar_a_paquete(paquete, &tamanio, sizeof(int));

    enviar_paquete(paquete, socket_storage);
    eliminar_paquete(paquete);
    log_info(loggerWorker, "Instrucción CREATE enviada a Storage para el tag: %s", file_tag);
}

//revisar si la vamos a necesitar
/*static void notificar_fin_query(int qid, const char* motivo) { 
    t_paquete* p = crear_paquete(WORKER_TO_MASTER_END);
    agregar_a_paquete(&qid, sizeof(int));
    agregar_a_paquete_string(p, motivo, 0);
    enviar_paquete(p, socket_master);
    eliminar_paquete(p);
}*/
//prueba

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


// --- Algoritmos de reemplazo ---

/// posible funcion de clock m
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
