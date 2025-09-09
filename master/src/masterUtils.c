#include "masterUtils.h"

int tiempo_aging;
t_log* loggerMaster = NULL;
t_config* config = NULL;
t_config_master* config_struct = NULL;
char* config_master = NULL;
int cant_workers = 0;
int qid = 0;
t_dictionary *diccionario_qcb = NULL;

void inicializar_master() {
    //HILO AGING ?
    inicializar_config();
    //si creamos diccionario en esta linea explota
    cargar_config();
    crear_logger();

    pthread_t hilo_planificador, hilo_servidor;
    pthread_create(&hilo_servidor, NULL, inicializar_servidor_multihilo, NULL);
    pthread_detach(hilo_servidor);
    pthread_create(&hilo_planificador, NULL, inicializar_planificador, NULL);
    pthread_join(hilo_planificador, NULL);
    
}

void inicializar_config(void){
    config_struct = malloc(sizeof(t_config_master)); //Reserva memoria
    config_struct->modulo = NULL;
    config_struct->puerto_escucha = NULL;
    config_struct->algoritmo_planificacion = NULL;
    config_struct->tiempo_aging = NULL;
    config_struct->log_level = NULL;
    inicializar_diccionario();
}

void inicializar_diccionario() {
    diccionario_qcb = dictionary_create();
}

void cargar_config() {
    config = config_create(config_master);
    config_struct->modulo = config_get_string_value (config, "MODULO");
    config_struct->puerto_escucha = config_get_string_value(config, "PUERTO_ESCUCHA");
    config_struct->algoritmo_planificacion = config_get_string_value(config, "ALGORITMO_PLANIFICACION");
    config_struct->tiempo_aging = config_get_string_value(config, "TIEMPO_AGING");
    config_struct->log_level = config_get_string_value(config, "LOG_LEVEL");

    tiempo_aging = atoi(config_struct->tiempo_aging);
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
    loggerMaster=iniciar_logger("master.log","MASTER",true, log_level_from_string(config_struct->log_level));
}

void atender_QueryControl(int fd){
    int length_path, prioridad;
    int offset = 0;
    char* path_query;
    void* buffer = recibir_buffer(fd);
    memcpy(&length_path,buffer + offset, sizeof(int));
    offset += sizeof(int);
    path_query = malloc(length_path + 1);

    memcpy(path_query, buffer + offset, length_path);
    path_query[length_path] = '\0';
    offset += length_path;

    memcpy(&prioridad, buffer + offset, sizeof(int));
    free(buffer);

    t_qcb* qcb = crear_query_control(path_query, prioridad);
    log_info(loggerMaster, "## Se conecta un Query Control para ejecutar la Query <%s> con prioridad <%d> - Id asignado: <%d>. Nivel multiprocesamiento <%d>", path_query, prioridad, qcb->qid, cant_workers);
}

void atender_Worker(int fd){
    //mutex_lock(&mutex_cant_workers);
    ++cant_workers;
    //mutex_unlock(&mutex_cant_workers);
    log_info(loggerMaster, "CONEXION EXITOSA CON WORKER");
    int id_worker;
    void* buffer = recibir_buffer(fd);
    memcpy(&id_worker, buffer, sizeof(int));
    free(buffer);
    log_info(loggerMaster,"## Se conecta el Worker <%d> - Cantidad total de Workers: <%d>",id_worker, cant_workers);
}

void* atender_conexion(void* arg){
    int fd = *(int *)arg;
    free(arg);
    op_code op = recibir_operacion(fd);
    switch (op) {
        case HANDSHAKE_QUERY:
            log_info(loggerMaster, "## Query Control Conectado - FD del socket: %d", fd);
            atender_QueryControl(fd);
            break;
        case HANDSHAKE_WORKER:
            log_info(loggerMaster, "## Worker Conectado - FD del socket: %d", fd);
            atender_Worker(fd);
            break;
        default:
            log_info(loggerMaster, "## Handshake inválido (%d) en fd %d", op, fd);
            break;
    }
    close(fd);
    return NULL;
}

//HACE FALTA AGREGAR HILO?
void* inicializar_servidor_multihilo(void* arg) {
    int fd_sv = crear_servidor(config_struct->puerto_escucha);
    log_info(loggerMaster, "Servidor MASTER escuchando Peticiones");
    while (1)
    {
        int *peticion = malloc(sizeof(int));
        *peticion = esperar_cliente(fd_sv, "MASTER", loggerMaster);
        pthread_t tid;
        pthread_create(&tid, NULL, atender_conexion, peticion);
        pthread_detach(tid);
    }
}

t_qcb* crear_query_control(char* path, int prioridad){
    //mutex_lock(&mutex_qid);
    qid++;
    //mutex_unlock(&mutex_qid);
    t_qcb* qcb = malloc(sizeof(t_qcb));
    qcb->qid = qid;
    qcb->estado = READY;
    qcb->ruta_arch = path;
    qcb->prioridad = prioridad;
    //mutex_lock(&mutex_diccionario_qcb);
    char key[16];
    sprintf(key, "%d", qcb->qid); //escribe el valor del pid en la key
    dictionary_put(diccionario_qcb, strdup(key), qcb);
    //mutex_unlock(&mutex_diccionario_qcb);
    //free(key);
    return qcb;
}

void* inicializar_planificador(void* arg){
    log_info(loggerMaster, "Planificador %s", config_struct->algoritmo_planificacion);
    if(strcmp(config_struct->algoritmo_planificacion, "FIFO") == 0){
        //planificador_fifo();
    } else if (strcmp(config_struct->algoritmo_planificacion, "PRIORIDADES") == 0){
        //planificador_prioridades();
    }
}