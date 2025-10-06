#include "masterUtils.h"
//===============ESTRUCTURAS===============
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
sem_t replanificar;
sem_t hay_en_Exit;
sem_t hay_en_Exec;

t_dictionary *diccionario_qcb = NULL;
//t_dictionary *diccionario_Workers = NULL;

//===============INICIALIZACION===============

void inicializar_master() {
    //HILO AGING ?
    inicializar_config();
    inicializar_semaforos();
    //si creamos diccionario en esta linea explot a
    cargar_config();
    crear_logger();
    
    /*t_qcb *qcb1 = crear_query_control("path1", 5);
    t_qcb *qcb2 = crear_query_control("path2", 3);
    agregar_a_ready(qcb1);
    agregar_a_ready(qcb2);*/
    pthread_t hilo_planificador, hilo_servidor, hilo_exit;
    pthread_create(&hilo_planificador, NULL, inicializar_planificador, NULL);
    pthread_detach(hilo_planificador);
    pthread_create(&hilo_exit, NULL, planificar_exit, NULL);
    pthread_detach(hilo_exit);
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
    sem_init(&replanificar, 0, 0);
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


t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle){
	t_log* nuevo_logger;
	nuevo_logger = log_create( nombreArchivoLog, nombreLog, seMuestraEnConsola, nivelDetalle);
    if (nuevo_logger == NULL) {
		perror("Error en el logger "); 
		exit(EXIT_FAILURE);
	}
	return nuevo_logger;
}

void crear_logger () {
    loggerMaster=iniciar_logger("master.log","MASTER",true, log_level_from_string(config_struct->log_level));
}

//===============CONEXIONES===============

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

void* atender_conexion(void* arg){
    int fd = *(int *)arg;
    free(arg);
    op_code op = recibir_operacion(fd);
    switch (op) {
        case HANDSHAKE_QUERY:
            log_info(loggerMaster, "## Query Control Conectado - FD del socket: %d", fd);
            atender_QueryControl(fd);
            close(fd);
            break;
        case HANDSHAKE_WORKER:
            log_info(loggerMaster, "## Worker Conectado - FD del socket: %d", fd);
            atender_Worker(fd);
            break;
        default:
            log_info(loggerMaster, "## Handshake inválido (%d) en fd %d", op, fd);
            close(fd); // NUEVO: cerrar el socket en caso de handshake inválido
            break;
    }
   // close(fd);   // no cerrar fd aqui, lo mantiene QCB/WCB
    return NULL;
}


//DESCONEXION DE QUERY CONTROL
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
    t_wcb *wcb = crear_wcb(id_worker, fd);
    if(strcmp(config_struct->algoritmo_planificacion, "FIFO") == 0){
        sem_post(&hay_worker_libre);
    }else{
        sem_post(&replanificar);
    }
        while(true){
        op_code op = recibir_operacion(fd);
        switch(op) {
            case -1:
                log_info(loggerMaster, "DESCONEXION del WORKER <%d>", id_worker);
                if(wcb->qid_asig >= 0){
                    //podria traer errores si al wcb le quedo el qid de una query que termino REVISAR
                    log_info(loggerMaster, "Mato la query ejecutando en worker, QUERY <%d>", wcb->qid_asig);
                    t_qcb *aDesalojar = buscar_qcb_por_ID(wcb->qid_asig);
                    agregar_a_exit(aDesalojar);
                    sem_post(&hay_en_Exit);
                    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
                    // COMENTARIO DANTE: Habría que eliminar tmb al wcb de la lista de wcbs y su estructura
                    // hacerle el free para que deje de ocupar memoria. Fijarse eso en memory leaks despues
                    //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
                }
                return;
            default:
                log_info(loggerMaster, "Operacion desconocida recibida del WORKER ID <%d>", id_worker);
                return;
        }
    }
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

    t_qcb* qcb = crear_query_control(path_query, prioridad, fd);
    log_info(loggerMaster, "## Se conecta un Query Control para ejecutar la Query <%s> con prioridad <%d> - Id asignado: <%d>. Nivel multiprocesamiento <%d>", path_query, prioridad, qcb->qid, cant_workers);
    agregar_a_ready(qcb);
    sem_post(&replanificar);

    while(true){
        op_code op = recibir_operacion(fd);
        switch(op) {
            case -1:
                switch(qcb->estado) {
                    case READY:
                        log_info(loggerMaster, "## Query Control ID <%d> se desconecto en estado READY", qcb->qid);
                        //actualizar_Estado(qcb, EXIT);
                        agregar_a_exit(qcb);
                        sem_post(&hay_en_Exit);
                        return;
                    case EXEC:
                        log_info(loggerMaster, "## Query Control ID <%d> se desconecto en estado EXEC", qcb->qid);
                        mandar_a_desalojar(qcb);
                        agregar_a_exit(qcb);
                        sem_post(&hay_en_Exit);
                        return;
                    default:
                        log_info(loggerMaster, "## Query Control ID <%d> se desconecto en estado EXIT", qcb->qid);
                        return;
                }
            default:
                log_info(loggerMaster, "Operacion desconocida recibida del Query Control ID <%d>, %d", qcb->qid, op);
                return;
        }
    }
}
//INTENTO IMPLEMENTAR DESAALOJOO
void mandar_a_desalojar(t_qcb* qcb) {
    t_wcb* worker = buscar_worker_por_qid(qcb->qid);
    log_info(loggerMaster, "Enviando Desalojo a worker ID <%d>", worker->wid);
    if(worker){
        enviar_operacion(worker->socket,DESALOJO);
        /*t_paquete* paquete = crear_paquete(DESALOJO);
        enviar_paquete(paquete,worker->socket);
        eliminar_paquete(paquete);*/
        /*int op = recibir_operacion(worker->socket);
        if(op != PC_ACTUALIZADO){
            log_error(loggerMaster, "Error al recibir confirmacion de desalojo del Worker %d para la Query %d", worker->wid, qcb->qid);
            return;
        }
        void *buffer = recibir_buffer(worker->socket);
        int pc_actualizado;
        memcpy(&pc_actualizado, buffer, sizeof(int));
        free(buffer);
        qcb->pc = pc_actualizado;*/
        log_info(loggerMaster, "Query %d desalojada del Worker %d, PC actualizado a %d", qcb->qid, worker->wid, qcb->pc);
        return;
        //actualizar_Estado(qcb, READY);
        //agregar_a_ready(qcb);
        //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        // COMENTARIO DANTE: NO SÉ SI DEBERÍA PASAR A READY, SI CUANDO TERMINA LA LLAMADA A ESTA
        // FUNCION VAS A PASARLO A EXIT PORQUE SE DESCONECTO?!
        // También habría que decir que en el wcb del worker que está libre con su bool de la estructura
        //!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
        //sem_post(&replanificar);
    }
    log_info(loggerMaster, "NO SE ENCONTRO a worker ID <%d>", worker->wid);
    return;
}


//===============ESTRUCTURAS ADMIN.===============

    //===============QUERY_CONTROL===============

t_qcb* crear_query_control(char* path, int prioridad, int fd){
    t_qcb* qcb = malloc(sizeof(t_qcb));
    qcb->qid = qid;
    qcb->pc = 0; //program counter
    qcb->estado = READY;
    qcb->ruta_arch = path;
    qcb->prioridad = prioridad;
    qcb->socket = fd; //NUEVO: guardo el socket del QC en el QCB para futuras comunicaciones; //NUEVO: inicializo en -1, se setea luego en atender_QueryControl
    pthread_mutex_init(&(qcb->mutex_qcb), NULL);
    
    pthread_mutex_lock(&mutex_qid);
    qid++;
    pthread_mutex_unlock(&mutex_qid);

    char *key = malloc(sizeof(int)); //eso no seria muy chico?
    sprintf(key, "%d",qcb->qid);
    log_info(loggerMaster, "SE GUARDO LA LLAVE %s",key);
    pthread_mutex_lock(&mutex_diccionario_qcb);
    dictionary_put(diccionario_qcb, strdup(key), qcb);
    pthread_mutex_unlock(&mutex_diccionario_qcb);
    free(key);
    return qcb;
}

void actualizar_Estado(t_qcb* qcb, t_estado nuevo_estado){
    t_estado estado_anterior = qcb->estado;
    pthread_mutex_lock(&(qcb->mutex_qcb));
    qcb->estado = nuevo_estado;
    pthread_mutex_unlock(&(qcb->mutex_qcb));
    log_info(loggerMaster, "Query ID <%d> cambio de estado de <%d> a <%d>", qcb->qid, estado_anterior, nuevo_estado);
}


t_qcb* buscar_qcb_por_ID(int qid){
    char *key = malloc(sizeof(int)); //eso no seria muy chico?
    sprintf(key, "%d",qid);
    t_qcb* qcb = dictionary_get(diccionario_qcb, key);
    free(key);
    return qcb;
}

t_qcb* buscar_qcb_mayor_prio(){
    log_info(loggerMaster, "Buscando QCB de mayor prioridad");
    t_qcb*  qcb_prio = list_get(cola_ready,0);
    int indice = 0;
    //ESTOS LOCKS ESTABAN COMPLICADOS
    for(int i = 1; i < list_size(cola_ready); i++){
        //pthread_mutex_unlock(&mutex_cola_ready);
        log_info(loggerMaster, "Buscando LISTA");
        t_qcb* qcb_actual = list_get(cola_ready,i);
        //pthread_mutex_lock(&(qcb_actual->mutex_qcb));
        //pthread_mutex_lock(&(qcb_prio->mutex_qcb));
        if(qcb_prio->prioridad > qcb_actual->prioridad){
            //pthread_mutex_unlock(&(qcb_prio->mutex_qcb));
            //pthread_mutex_unlock(&(qcb_actual->mutex_qcb));
            qcb_prio = qcb_actual;
            indice = i;
        }
    }
    log_info(loggerMaster, "ENCONTRADO, QCB ID %d", qcb_prio->qid);
    pthread_mutex_lock(&mutex_cola_ready);
    t_qcb* qcb_aExec = list_remove(cola_ready,indice);
    pthread_mutex_unlock(&mutex_cola_ready);
    return  qcb_aExec;

}
void eliminar_qcb_diccionario(int qid) {
    char* key = malloc(sizeof(int));
    sprintf(key, "%d",  qid);
    pthread_mutex_lock(&mutex_diccionario_qcb);
    //dictionary_remove_and_destroy(diccionario_qcb, key, free);
    t_qcb *aElim = dictionary_remove(diccionario_qcb, key);
    pthread_mutex_unlock(&mutex_diccionario_qcb);
    eliminar_qcb(aElim);
    free(key);
}

void eliminar_qcb(void* element){
    t_qcb* qcb = (t_qcb*) element;
    close(qcb->socket);
    free(qcb->ruta_arch);
    free(qcb);
}
    //===============WORKER===============

/*void crear_wcb(int id, int socket) {
    t_wcb* wcb = malloc (sizeof(t_wcb));
    wcb->wid = id;
    wcb->esta_libre = true;
    wcb->qid_asig = -1; //-1 si no tiene query asignada
    wcb->socket = socket;
    pthread_mutex_init(&wcb->mutex_wcb, NULL);
    list_add(lista_workers, wcb);
}*/

t_wcb *crear_wcb(int id, int socket) {
    t_wcb* wcb = malloc (sizeof(t_wcb));
    wcb->wid = id;
    wcb->esta_libre = true;
    wcb->qid_asig = -1; //-1 si no tiene query asignada
    wcb->socket = socket;
    pthread_mutex_init(&wcb->mutex_wcb, NULL);
    list_add(lista_workers, wcb);
    return wcb;
}

t_wcb *buscar_worker_por_qid(int qid) {
    for(int i = 0; i < list_size(lista_workers); i++){
        t_wcb *candidato = list_get(lista_workers, i);
        if(candidato->qid_asig == qid){
            return candidato;
        }
    }
    return NULL;
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

t_wcb* buscar_wcb_menor_prio() {
    //ESTA FUNCION SE LLAMABA EN UN INSTANTE ERRONEO
    t_wcb* wcb_prio = list_get(lista_workers,0);
    t_qcb* qcb_prio = buscar_qcb_por_ID(wcb_prio->qid_asig);
    for(int i = 1; i < list_size(lista_workers); i++){
        t_wcb* wcb_actual = list_get(lista_workers,i);
        t_qcb* qcb_actual = buscar_qcb_por_ID(wcb_prio->qid_asig);
        if(qcb_prio->prioridad < qcb_actual->prioridad){
            wcb_prio = wcb_actual;
            qcb_prio = qcb_actual;
        }
    }
    return wcb_prio;
}



//===============PLANIFICACION===============

void* planificar_exit(void *arg){
    log_info(loggerMaster, "Hilo exit esperando queries a eliminar");
    while(true){
        sem_wait(&hay_en_Exit);
        t_qcb *qcb_elim = list_remove(cola_exit,0);
        log_info(loggerMaster, "Query en Exit - ID asignado <%d>",qcb_elim->qid);
        //t_paquete* paquete = crear_paquete(MASTER_TO_QC_END);
        //enviar_paquete(paquete, qcb_elim->socket);
        //eliminar_paquete(paquete);
        eliminar_qcb_diccionario(qcb_elim->qid);
    }
    return NULL;
}

void* inicializar_planificador(void* arg){
    log_info(loggerMaster, "Planificador %s", config_struct->algoritmo_planificacion);
    if(strcmp(config_struct->algoritmo_planificacion, "FIFO") == 0){
        planificador_fifo();
    } else if (strcmp(config_struct->algoritmo_planificacion, "PRIORIDADES") == 0){
        planificador_prioridades();
    }
    return NULL;
}

void planificador_fifo(){
    //es mejor hacerlo con while true y semaforos o llamar la funcion cada vez que tenga que replanificar?
    while(true){
        sem_wait(&replanificar);
        log_info(loggerMaster, "Hay proceso en READY");
        sem_wait(&hay_worker_libre);
        t_qcb* qcb_exec = list_get(cola_ready,0); // no sería list_remove? y tene un mutex protegiendo la cola
        log_info(loggerMaster, "Se encontro la Query <%s> con prioridad <%d> - Id asignado: <%d>", qcb_exec->ruta_arch, qcb_exec->prioridad, qcb_exec->qid);
        t_wcb* wcb_elegido = buscar_worker_libre();
        agregar_a_exec(qcb_exec);
        mandar_a_ejecutar(qcb_exec, wcb_elegido);
    }
}

void planificador_prioridades(){
    //es mejor hacerlo con while true y semaforos o llamar la funcion cada vez que tenga que replanificar?
    while(true){
    sem_wait(&replanificar);
        log_info(loggerMaster, "Hay proceso en READY");
        //sem_wait(&hay_worker_libre);
        t_qcb* qcb_exec = buscar_qcb_mayor_prio();
        log_info(loggerMaster, "SALI DE LA FUNCION");
        if(cant_workers > 0)
        {
            log_info(loggerMaster, "BUSCANDO WORKER");
            //t_wcb* wcb_elegido = buscar_wcb_menor_prio();
            log_info(loggerMaster, "Se encontro la Query <%s> con prioridad <%d> - Id asignado: <%d>", qcb_exec->ruta_arch, qcb_exec->prioridad, qcb_exec->qid);
            t_wcb* wcb_elegido = buscar_worker_libre();
            if(wcb_elegido){
            agregar_a_exec(qcb_exec);
            mandar_a_ejecutar(qcb_exec, wcb_elegido);
            }else{
                wcb_elegido = buscar_wcb_menor_prio();
            }
        }else{
            log_info(loggerMaster, "No hay workers disponibles");
            agregar_a_ready(qcb_exec);
        }
    }
}

void* hilo_aging(void* arg){
    t_qcb* qcb = (t_qcb*) arg;
    while(qcb->estado == READY && qcb->prioridad > 0){
        
        usleep(tiempo_aging * 1000);
        pthread_mutex_lock(&(qcb->mutex_qcb));
        qcb->prioridad -= 1;
        pthread_mutex_unlock(&(qcb->mutex_qcb));
        log_info(loggerMaster, "Aging aplicado a la Query <%s> - Id asignado: <%d>. Nueva prioridad <%d>", qcb->ruta_arch, qcb->qid, qcb->prioridad);
        if(buscar_worker_libre() == NULL){
        sem_post(&replanificar);
        }
    }
    return NULL;
}


void mandar_a_ejecutar(t_qcb* qcb, t_wcb* worker) {
    //BUSCAR UN WORKER LIBRE
    //MANDARLE LA QUERY
    
    //t_wcb* worker = buscar_worker_libre();
    // Control formal por si no hay workers libres (no deberia pasar)
    /*if(worker == NULL){
        log_error(loggerMaster, "No se encontro un Worker libre para ejecutar la Query <%s> con prioridad <%d> - Id asignado: <%d>", qcb->ruta_arch, qcb->prioridad, qcb->qid);
        return;
    }*/
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

//===============COLAS===============
//ACTUALICE ESTADOS ACA
void agregar_a_ready(t_qcb* qcb){
    actualizar_Estado(qcb, READY);
    pthread_mutex_lock(&mutex_cola_ready);
    list_add(cola_ready, qcb);
    pthread_mutex_unlock(&mutex_cola_ready);

    if(strcmp(config_struct->algoritmo_planificacion, "PRIORIDADES") == 0) {
        pthread_t hilo_age;
        pthread_create(&hilo_age, NULL, hilo_aging, (void*)qcb);
        pthread_detach(hilo_age);
    }
}
void agregar_a_exec(t_qcb* qcb){
    actualizar_Estado(qcb, EXEC);
    pthread_mutex_lock(&mutex_cola_exec);
    list_add(cola_exec, qcb);
    pthread_mutex_unlock(&mutex_cola_exec);
}
void agregar_a_exit(t_qcb* qcb){
    actualizar_Estado(qcb, EXIT);
    pthread_mutex_lock(&mutex_cola_exit);
    list_add(cola_exit, qcb);
    pthread_mutex_unlock(&mutex_cola_exit);
}
//PREGUNTAR SOBRE ESTO DESPUES
void remover_qcb_cola(int qid, t_list *cola, pthread_mutex_t mutexCola){
    for(int i = 0; i < list_size(cola); i++){
        t_qcb *candidato = list_get(cola,i);
        if(candidato->qid == qid){
            pthread_mutex_lock(&mutexCola);
            list_remove(cola,i);
            pthread_mutex_unlock(&mutexCola);
            return;
        } 
    }
    log_info(loggerMaster, "No se encontro el qid %d en la cola a remover", qid);
    return;
}