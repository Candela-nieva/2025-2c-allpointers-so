#include "masterUtils.h"

int tiempo_aging;
t_log* loggerMaster = NULL;
t_config* config = NULL;
t_config_master* config_struct = NULL;
char* config_master = NULL;
int cant_workers = 0;
int qid = 0;

t_list* cola_ready;
t_list* cola_exec;
t_list* cola_exit;
t_list* lista_workers;

pthread_mutex_t mutex_cant_workers;
pthread_mutex_t mutex_qid;
pthread_mutex_t mutex_diccionario_qcb;
pthread_mutex_t mutex_cola_ready;
pthread_mutex_t mutex_cola_exec;
pthread_mutex_t mutex_cola_exit;

sem_t hay_worker_libre;
sem_t hay_en_Ready;
sem_t hay_en_Exit;
sem_t hay_en_Exec;

t_dictionary *diccionario_qcb = NULL;
//t_dictionary *diccionario_Workers = NULL;


void inicializar_master() {
    //HILO AGING ?
    inicializar_config();
    inicializar_semaforos();
    //si creamos diccionario en esta linea explot a
    cargar_config();
    crear_logger();
    
    pthread_t hilo_planificador, hilo_servidor;
    pthread_create(&hilo_planificador, NULL, inicializar_planificador, NULL);
    pthread_detach(hilo_planificador);
    pthread_create(&hilo_servidor, NULL, inicializar_servidor_multihilo, NULL);
    pthread_join(hilo_servidor, NULL);
    
    
}

void inicializar_listas() {
    cola_ready = list_create();
    cola_exec = list_create();
    cola_exit = list_create();
    lista_workers = list_create();
}

void inicializar_config(void){
    config_struct = malloc(sizeof(t_config_master)); //Reserva memoria
    config_struct->modulo = NULL;
    config_struct->puerto_escucha = NULL;
    config_struct->algoritmo_planificacion = NULL;
    config_struct->tiempo_aging = NULL;
    config_struct->log_level = NULL;
    inicializar_diccionario();
    inicializar_listas();
}

void inicializar_diccionario() {
    diccionario_qcb = dictionary_create();
    //diccionario_Workers = dictionary_create();
}

void inicializar_semaforos() {
    sem_init(&hay_en_Ready, 0, 0);
    sem_init(&hay_en_Exec, 0, 0);
    sem_init(&hay_en_Exit, 0, 0);
    sem_init(&hay_worker_libre, 0, 0);

    pthread_mutex_init(&mutex_cant_workers, NULL);
    pthread_mutex_init(&mutex_qid, NULL);
    pthread_mutex_init(&mutex_diccionario_qcb, NULL);
    pthread_mutex_init(&mutex_cola_ready, NULL);
    pthread_mutex_init(&mutex_cola_exec, NULL);
    pthread_mutex_init(&mutex_cola_exit, NULL);
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
    agregar_a_ready(qcb);
    sem_post(&hay_en_Ready);
}

void atender_Worker(int fd){
    pthread_mutex_lock(&mutex_cant_workers);
    ++cant_workers;
    pthread_mutex_unlock(&mutex_cant_workers);
    log_info(loggerMaster, "CONEXION EXITOSA CON WORKER");
    int id_worker;
    void* buffer = recibir_buffer(fd);
    memcpy(&id_worker, buffer, sizeof(int));
    free(buffer);
    log_info(loggerMaster,"## Se conecta el Worker <%d> - Cantidad total de Workers: <%d>",id_worker, cant_workers);
    //COMO MANEJAMOS LOS SOCKETS??
    crear_wcb(id_worker, fd);

    sem_post(&hay_worker_libre);
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
    t_qcb* qcb = malloc(sizeof(t_qcb));
    qcb->qid = qid;
    qcb->pc = 0; //program counter
    qcb->estado = READY;
    qcb->ruta_arch = path;
    qcb->prioridad = prioridad;
    pthread_mutex_lock(&mutex_qid);
    qid++;
    pthread_mutex_unlock(&mutex_qid);
    char *key = malloc(2);
    sprintf(key, "%d",qcb->qid);
    log_info(loggerMaster, "SE GUARDO LA LLAVE %s",key);
    pthread_mutex_lock(&mutex_diccionario_qcb);
    dictionary_put(diccionario_qcb, key, qcb);
    pthread_mutex_unlock(&mutex_diccionario_qcb);
    
    return qcb;
}

void* inicializar_planificador(void* arg){
    log_info(loggerMaster, "Planificador %s", config_struct->algoritmo_planificacion);
    if(strcmp(config_struct->algoritmo_planificacion, "FIFO") == 0){
        planificador_fifo();
    } else if (strcmp(config_struct->algoritmo_planificacion, "PRIORIDADES") == 0){
        //planificador_prioridades();
    }
    return NULL;
}

void planificador_fifo(){
    //es mejor hacerlo con while true y semaforos o llamar la funcion cada vez que tenga que replanificar?
    while(true){
        sem_wait(&hay_en_Ready);
        log_info(loggerMaster, "Hay proceso en READY");
        sem_wait(&hay_worker_libre);
        t_qcb* qcb_exec = list_get(cola_ready,0);
        log_info(loggerMaster, "Se encontro la Query <%s> con prioridad <%d> - Id asignado: <%d>", qcb_exec->ruta_arch, qcb_exec->prioridad, qcb_exec->qid);
        agregar_a_exec(qcb_exec);
        mandar_a_ejecutar(qcb_exec);
    }
}


void mandar_a_ejecutar(t_qcb* qcb) {
    //BUSCAR UN WORKER LIBRE
    //MANDARLE LA QUERY
    
    t_wcb* worker = buscar_worker_libre();
    // Control formal por si no hay workers libres (no deberia pasar)
    if(worker == NULL){
        log_error(loggerMaster, "No se encontro un Worker libre para ejecutar la Query <%s> con prioridad <%d> - Id asignado: <%d>", qcb->ruta_arch, qcb->prioridad, qcb->qid);
        return;
    }
    // Actualizar WCB con proteccion de mutex
    pthread_mutex_lock(&worker->mutex_wcb);
    worker->esta_libre = false;
    worker->qid_asig = qcb->qid;
    pthread_mutex_unlock(&worker->mutex_wcb);
    log_info(loggerMaster, "Mandando a ejecutar la Query <%s> con prioridad <%d> - Id asignado: <%d>, Al worker %d", qcb->ruta_arch, qcb->prioridad, qcb->qid,worker->wid);
    t_paquete* paquete = crear_paquete(EJECUTAR);
    agregar_a_paquete(paquete, &(qcb->pc), sizeof(int));
    agregar_a_paquete_string(paquete, qcb->ruta_arch, strlen(qcb->ruta_arch));
    enviar_paquete(paquete, worker->socket);

    eliminar_paquete(paquete);
}    

t_wcb *buscar_worker_libre(){
    for(int i = 0; i < list_size(lista_workers); i++){
        t_wcb *candidato = list_get(lista_workers, i);
        if(candidato->esta_libre){
            return candidato;
        }
    }
    return NULL;
}

void crear_wcb(int id, int socket) {
    t_wcb* wcb = malloc (sizeof(t_wcb));
    wcb->wid = id;
    wcb->esta_libre = true;
    wcb->qid_asig = -1; //-1 si no tiene query asignada
    wcb->socket = socket;
    pthread_mutex_init(&wcb->mutex_wcb, NULL);
    /*char key[16];
    sprintf(key, "%d", wcb->wid);
    dictionary_put(diccionario_Workers, strdup(key), wcb);*/
    list_add(lista_workers, wcb);
}

void agregar_a_ready(t_qcb* qcb){
    pthread_mutex_lock(&mutex_cola_ready);
    list_add(cola_ready, qcb);
    pthread_mutex_unlock(&mutex_cola_ready);
}
void agregar_a_exec(t_qcb* qcb){
    pthread_mutex_lock(&mutex_cola_exec);
    list_add(cola_ready, qcb);
    pthread_mutex_unlock(&mutex_cola_exec);
}
void agregar_a_exit(t_qcb* qcb){
    pthread_mutex_lock(&mutex_cola_exit);
    list_add(cola_ready, qcb);
    pthread_mutex_unlock(&mutex_cola_exit);
}