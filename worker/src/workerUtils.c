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

void* iniciar_conexion_master(void* arg){
    
    if(arg == NULL){
        log_info(loggerWorker, "Error: El ID del Worker no fue proporcionado correctamente.");
        pthread_exit(NULL); // Terminar el hilo si no se proporciona un ID válido
    }

    int id_worker = *(int*)arg;
    free(arg); // Liberamos el puntero
    
    log_info(loggerWorker, "INTENTO HANDSHAKE CON MASTER");
    
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
    
    esperar_queries();

    // Cuando esperar_queries termine, cerramos la conexión y finalizamos el hilo
    close(socket_master);
    log_info(loggerWorker, "Conexión con Master cerrada. Hilo Master finalizado."); // LOG NO OBLIGATORIO
    pthread_exit(NULL); // Finaliza el hilo correctamente

    return NULL;
}

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
                manejar_ejecutar(buffer);
                free(buffer);
                log_info(loggerWorker, "Se recibio la operacion EJECUTAR");
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

        /*
        int offset = 0;
        int pc, tamarch;
        char *archivo;
        void *buffer = recibir_buffer(socket_master);
        memcpy(&(pc), buffer, sizeof(int));
        offset += sizeof(int);
        memcpy(&(tamarch), buffer + offset, sizeof(int));
        offset += sizeof(int);
        archivo = malloc(tamarch + 1);
        memcpy(archivo, buffer + offset, tamarch);
        archivo[tamarch] = '\0';
        free(buffer);

       log_info(loggerWorker, "Se recibio el PC %d correspondiente al archivo con path %s", pc, archivo);
       //log_info(loggerWorker, "## Query %d: Se recibe la Query. El path de operaciones es: %s", worker_id, archivo);
       */
       //aca va el ciclo de instruccion
       //free(archivo);
    }
}

// DESERIALIZAR EJECUTAR + LLAMADA A EJECUTAR_QUERY
void manejar_ejecutar(void* buffer) {
    int offset = 0;
    int pc, tamarch;
    char *archivo;
    
    memcpy(&(pc), buffer + offset, sizeof(int));
    offset += sizeof(int);
    
    memcpy(&(tamarch), buffer + offset, sizeof(int));
    offset += sizeof(int);
    
    archivo = malloc(tamarch + 1);
    memcpy(archivo, buffer + offset, tamarch);
    archivo[tamarch] = '\0';

    log_info(loggerWorker, "##Query %d: Se recibe la Query. El path de operaciones es: %s", pc, archivo); // LOG OBLIGATORIO
    //log_info(loggerWorker, "Manejando EJECUTAR: PC=%d, Archivo=%s", pc, archivo);

    // TODO: Aca va el ciclo de instrucciones
    //ejecutar_query(pc, archivo); ----->>>>>>>>> HACERRRRR!!!!!

    free(archivo);
}

//revisar si la vamos a necesitar
/*static void notificar_fin_query(int qid, const char* motivo) { 
    t_paquete* p = crear_paquete(WORKER_TO_MASTER_END);
    agregar_a_paquete(&qid, sizeof(int));
    agregar_a_paquete_string(p, motivo, 0);
    enviar_paquete(p, socket_master);
    eliminar_paquete(p);
}*/


// =================== CONEXION A STORAGE ======================= 

void* iniciar_conexion_storage(void* arg){ 
    (void)arg; // Evitar warning de variable no usada (porque no usamos argumento en este caso)

    socket_storage = crear_conexion(config_struct->ip_storage, config_struct->puerto_storage);
    if(socket_storage == -1) {
        log_info(loggerWorker, "Error al crear la conexión con Storage");
        pthread_exit(NULL); // Terminar el hilo si hay un error
    }
    
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
    int tamanio_pag = 0;
    void* buffer = recibir_buffer(socket_storage);
    memcpy(&tamanio_pag, buffer, sizeof(int));
    free(buffer);

    log_info(loggerWorker, "Tamanio de pagina recibido de Storage: %d", tamanio_pag);
    
    log_info(loggerWorker, "Worker listo para interactuar con Storage");
    
    return NULL; 
}
