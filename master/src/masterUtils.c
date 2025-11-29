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

pthread_mutex_t mutex_workers;
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
    pthread_mutex_init(&mutex_workers, NULL);
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
    //free(arg);
    op_code op = recibir_operacion(fd);
    switch (op) {
        case HANDSHAKE_QUERY:
            log_info(loggerMaster, "## Query Control Conectado");
            atender_QueryControl(fd);
            //close(fd);
            break;
        case HANDSHAKE_WORKER:
            log_info(loggerMaster, "## Worker Conectado");
            atender_Worker(fd);
            break;
        default:
            log_info(loggerMaster, "## Handshake inválido");
            close(fd); // NUEVO: cerrar el socket en caso de handshake inválido
            break;
    }
    free(arg);
    return NULL;
}


//DESCONEXION DE QUERY CONTROL
void atender_Worker(int fd){
    pthread_mutex_lock(&mutex_cant_workers);
    ++cant_workers;
    pthread_mutex_unlock(&mutex_cant_workers);
    //log_info(loggerMaster, "CONEXION EXITOSA CON WORKER");
    int id_worker;
    void* buffer = recibir_buffer(fd);
    memcpy(&id_worker, buffer, sizeof(int));
    free(buffer);
    pthread_mutex_lock(&mutex_cant_workers);
    // LOG OBLIGATORIO //
    log_info(loggerMaster,"## Se conecta el Worker <%d> - Cantidad total de Workers: <%d>",id_worker, cant_workers);
    pthread_mutex_unlock(&mutex_cant_workers);
    t_wcb *wcb = crear_wcb(id_worker, fd);
    if(strcmp(config_struct->algoritmo_planificacion, "FIFO") == 0){
        sem_post(&hay_worker_libre);
    }else{
        sem_post(&replanificar);
    }
    while(true){
        op_code op = recibir_operacion(fd);
        pthread_mutex_lock(&wcb->mutex_wcb);
        int qid_asig = wcb->qid_asig;
        pthread_mutex_unlock(&wcb->mutex_wcb);
        if(op == -1) {
            sem_post(&(wcb->sem_desalojo)); 
            pthread_mutex_lock(&mutex_cant_workers);
            --cant_workers;
            log_info(loggerMaster, "## Se desconecta el Worker <%d> - Se finaliza la Query <%d> - Cantidad total de Workers: <%d>", id_worker, qid_asig, cant_workers);
            pthread_mutex_unlock(&mutex_cant_workers);

            pthread_mutex_lock(&wcb->mutex_wcb);
            if(wcb->qid_asig >= 0){
                //podria traer errores si al wcb le quedo el qid de una query que termino REVISAR
                t_qcb *aDesalojar = buscar_qcb_por_ID(wcb->qid_asig);
                if(aDesalojar) {
                    pthread_mutex_lock(&(aDesalojar->mutex_qcb));
                    log_info(loggerMaster, "## Se desaloja la Query <%d> (<%d>) del Worker <%d> - Motivo: DESCONEXION", wcb->qid_asig, aDesalojar->prioridad, wcb->wid);
                    agregar_a_exit(aDesalojar);
                    enviar_mensaje_exit(aDesalojar->socket, DESCONEXION_WORKER);
                    pthread_mutex_unlock(&(aDesalojar->mutex_qcb));
                    remover_qcb_cola(wcb->qid_asig,cola_exec,&mutex_cola_exec);
                    sem_post(&hay_en_Exit);
                }
            }
            pthread_mutex_unlock(&wcb->mutex_wcb);
            eliminar_wcb(wcb);
            return;
        }
        switch(op) {
            case PC_ACTUALIZADO:
                //log_info(loggerMaster, "WORKER <%d>, devuelve PC de QID %d ", id_worker, qid_asig);
                
                pthread_mutex_lock(&(wcb->mutex_socket));
                void *bufferPC = recibir_buffer(wcb->socket);
                pthread_mutex_unlock(&(wcb->mutex_socket));
                
                int pc_actualizado;
                memcpy(&pc_actualizado, bufferPC, sizeof(int));
                free(bufferPC);
                
                //log_info(loggerMaster, "WORKER <%d>, PC Acualizado %d", id_worker, pc_actualizado);
                pthread_mutex_lock(&(wcb->mutex_wcb));
                t_qcb* qcbDesaloj = buscar_qcb_por_ID(wcb->qid_asig);
                int wid = wcb->wid;
                wcb->qid_asig = -1;
                wcb->esta_libre = true;
                pthread_mutex_unlock(&(wcb->mutex_wcb));
                
                pthread_mutex_lock(&(qcbDesaloj->mutex_qcb));
                qcbDesaloj->pc = pc_actualizado;
                pthread_mutex_unlock(&(qcbDesaloj->mutex_qcb));
                
                sem_post(&(wcb->sem_desalojo));
                
                log_info(loggerMaster, "Query %d desalojada del Worker %d, PC actualizado a %d", qid_asig, wid, pc_actualizado);
                if(strcmp(config_struct->algoritmo_planificacion, "FIFO") == 0){
                    sem_post(&hay_worker_libre);
                }else{
                    sem_post(&replanificar);
                }
                break;
            case MASTER_TO_QC_READ_RESULT:
                //pthread_mutex_lock(&wcb->mutex_wcb);
                log_info(loggerMaster, "## Se envía un mensaje de lectura de la Query %d en el Worker %d al Query Control", wcb->qid_asig, id_worker);
                //pthread_mutex_unlock(&wcb->mutex_wcb);
                //enviar a query resultado de read
                int offset = 0;
                pthread_mutex_lock(&(wcb->mutex_socket));
                void *buffer = recibir_buffer(fd);
                pthread_mutex_unlock(&(wcb->mutex_socket));
                char *contenido, *arch, *tag;
                int tamCont, tamArch, tamTag;
                
                memcpy(&tamArch, buffer + offset, sizeof(int));
                arch = malloc(tamArch + 1);
                offset += sizeof(int);
                memcpy(arch, buffer + offset,tamArch);
                arch[tamArch] = '\0';
                offset += tamArch;

                memcpy(&tamTag, buffer + offset, sizeof(int));
                tag = malloc(tamTag + 1);
                offset += sizeof(int);
                memcpy(tag, buffer + offset,tamTag);
                tag[tamTag] = '\0';
                offset += tamTag;

                memcpy(&tamCont, buffer + offset, sizeof(int));
                contenido = malloc(tamCont + 1);
                offset += sizeof(int);
                
                memcpy(contenido, buffer + offset,tamCont);
                contenido[tamCont] = '\0';
                free(buffer);
                t_qcb *qcb = buscar_qcb_por_ID(qid_asig);
                int socket_query = -1;
                if(qcb) {
                    pthread_mutex_lock(&(qcb->mutex_qcb));
                    socket_query = qcb->socket;
                    pthread_mutex_unlock(&(qcb->mutex_qcb));
                }
                t_paquete *paquete = crear_paquete(MASTER_TO_QC_READ_RESULT);
                agregar_a_paquete_string(paquete,arch, strlen(arch));
                agregar_a_paquete_string(paquete,tag,strlen(tag));
                agregar_a_paquete_string(paquete,contenido,strlen(contenido));
                if(socket_query != -1)
                    enviar_paquete(paquete,socket_query);
                else
                    log_info(loggerMaster, "No se pudo enviar el resultado de la lectura");
                eliminar_paquete(paquete);
                if(arch)
                    free(arch);
                if(tag)
                    free(tag);
                if(contenido)
                    free(contenido);
                break;

            case WORKER_TO_MASTER_END:
                //t_motivo motivoExit = recibir_operacion(fd);
                pthread_mutex_lock(&(wcb->mutex_socket));
                void *bufferMotivo = recibir_buffer(fd);
                pthread_mutex_unlock(&(wcb->mutex_socket));
                t_motivo motivoExit;
                memcpy(&motivoExit, bufferMotivo, sizeof(int));
                free(bufferMotivo);

                log_info(loggerMaster, "## Se terminó la Query %d en el Worker %d", qid_asig, id_worker);
                t_qcb *qcbExit = buscar_qcb_por_ID(qid_asig);
                remover_qcb_cola(qid_asig,cola_exec,&mutex_cola_exec);

                pthread_mutex_lock(&wcb->mutex_wcb);
                wcb->qid_asig = -1;
                wcb->esta_libre = true;
                pthread_mutex_unlock(&wcb->mutex_wcb);

                // Agregue este semaforo
                sem_post(&(wcb->sem_desalojo));
                if(qcbExit) {
                    agregar_a_exit(qcbExit);
                    pthread_mutex_lock(&qcbExit->mutex_qcb);
                    enviar_mensaje_exit(qcbExit->socket, motivoExit);
                    pthread_mutex_unlock(&qcbExit->mutex_qcb);
                    sem_post(&hay_en_Exit);
                } else {
                    log_info(loggerMaster, "WORKER %d devolvio END pero no se encontro QCB %d (posible corrupcion)", id_worker, qid_asig);
                }
                
                
                if(strcmp(config_struct->algoritmo_planificacion, "FIFO") == 0){
                    sem_post(&hay_worker_libre);
                }else{
                    sem_post(&replanificar);
                    
                }
                break;

            default:
                log_info(loggerMaster, "Operacion desconocida recibida del WORKER ID <%d> : %d", id_worker, op);
                return;
        }
    }
}

void enviar_mensaje_exit(int socketQuery, t_motivo motivo){
    t_paquete *paquete = crear_paquete(MASTER_TO_QC_END);
    agregar_a_paquete(paquete, &motivo, sizeof(int));
    enviar_paquete(paquete, socketQuery);
    //cierro el socket aca
    close(socketQuery);
    eliminar_paquete(paquete);
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
    pthread_mutex_lock(&(qcb->mutex_qcb));
    int id_query = qcb->qid;
    pthread_mutex_unlock(&(qcb->mutex_qcb));
    pthread_mutex_lock(&mutex_cant_workers);
    // LOG OBLIGATORIO //
    log_info(loggerMaster, "## Se conecta un Query Control para ejecutar la Query <%s> con prioridad <%d> - Id asignado: <%d>. Nivel multiprocesamiento <%d>", path_query, prioridad, id_query, cant_workers);
    pthread_mutex_unlock(&mutex_cant_workers);
    nuevo_a_ready(qcb);
    sem_post(&replanificar);

    while(true){
        op_code op = recibir_operacion(fd);
        if(op == -1) {

            //pthread_mutex_lock(&(qcb->mutex_qcb)); // Bloqueas para leer estado
            t_estado estado_actual = 2;  // Copias el estado
            if(qcb){
                pthread_mutex_lock(&(qcb->mutex_qcb));
                estado_actual = qcb->estado;
                pthread_mutex_unlock(&(qcb->mutex_qcb));  
            }
            switch(estado_actual) {
                case READY:
                    //actualizar_Estado(qcb, EXIT);
                    remover_qcb_cola(qcb->qid,cola_ready,&mutex_cola_ready);
                    agregar_a_exit(qcb);
                    sem_post(&hay_en_Exit);
                    return;
                case EXEC:
                    remover_qcb_cola(qcb->qid,cola_exec,&mutex_cola_exec);
                    mandar_a_desalojar(qcb);
                    agregar_a_exit(qcb);
                    sem_post(&hay_en_Exit);
                    return;
                default:
                    return;
            }
        }else{
            log_info(loggerMaster, "Operacion desconocida recibida del QUERY CONTROL ID <%d> : %d", id_query, op);
            return;
        } 
    }
}
//INTENTO IMPLEMENTAR DESAALOJOO
void mandar_a_desalojar(t_qcb* qcb) {
    
    
    //pthread_mutex_lock(&(qcb->mutex_qcb));
    if(qcb){
        int qid = qcb->qid;
        //pthread_mutex_unlock(&(qcb->mutex_qcb));
        t_wcb* worker = buscar_worker_por_qid(qid);
        if(worker) {
           
            pthread_mutex_lock(&(worker->mutex_wcb));
            int wid = worker->wid;
            int socket = worker->socket;
            pthread_mutex_unlock(&(worker->mutex_wcb));
            log_info(loggerMaster, "Enviando Desalojo a worker ID <%d>", wid);
            pthread_mutex_lock(&(worker->mutex_socket));
            enviar_operacion(socket, DESALOJO);
            pthread_mutex_unlock(&(worker->mutex_socket));
            sem_wait(&(worker->sem_desalojo));
            
            return;
        } else {
            log_info(loggerMaster, "NO SE ENCONTRO a worker del Query <%d>", qid);
        }
    }
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
    pthread_mutex_lock(&mutex_diccionario_qcb);
    dictionary_put(diccionario_qcb, (key), qcb);
    pthread_mutex_unlock(&mutex_diccionario_qcb);
    free(key);
    return qcb;
}

void actualizar_Estado(t_qcb* qcb, t_estado nuevo_estado){
    t_estado estado_anterior = qcb->estado;
    pthread_mutex_lock(&(qcb->mutex_qcb));
    qcb->estado = nuevo_estado;
    int qid = qcb->qid;
    pthread_mutex_unlock(&(qcb->mutex_qcb));
    log_info(loggerMaster, "Query ID <%d> cambio de estado de %s a %s", qid, estado_a_string(estado_anterior), estado_a_string(nuevo_estado));
}

const char* estado_a_string (t_estado estado) {
    switch(estado) {
        case READY:     return "READY";
        case EXEC:      return "EXEC";
        case EXIT:      return "EXIT";
    }
}

t_qcb* buscar_qcb_por_ID(int qid){
    char *key = malloc(sizeof(int)); //eso no seria muy chico?
    sprintf(key, "%d",qid);
    pthread_mutex_lock(&mutex_diccionario_qcb);
    t_qcb* qcb = dictionary_get(diccionario_qcb, key);
    pthread_mutex_unlock(&mutex_diccionario_qcb);
    free(key);
    return qcb;
}

t_qcb* buscar_qcb_mayor_prio(){
    log_info(loggerMaster, "Buscando QCB de mayor prioridad");
    pthread_mutex_lock(&mutex_cola_ready);
    t_qcb* qcb_prio = list_get(cola_ready, 0);
    for(int i = 1; i < list_size(cola_ready); i++){

        //log_info(loggerMaster, "Buscando LISTA");
        t_qcb* qcb_actual = list_get(cola_ready,i);
        pthread_mutex_lock(&(qcb_actual->mutex_qcb));
        if(qcb_prio->prioridad > qcb_actual->prioridad){
            qcb_prio = qcb_actual;
        }
        pthread_mutex_unlock(&(qcb_actual->mutex_qcb));
    }
    log_info(loggerMaster, "Query de menor prioridad : %d", qcb_prio->qid);
    pthread_mutex_unlock(&mutex_cola_ready);
    return qcb_prio;
}

void eliminar_qcb_diccionario(int qid) {
    char* key = malloc(sizeof(int));
    sprintf(key, "%d",  qid);
    pthread_mutex_lock(&mutex_diccionario_qcb);
    t_qcb *aElim = dictionary_remove(diccionario_qcb, key);
    pthread_mutex_unlock(&mutex_diccionario_qcb);
    eliminar_qcb(aElim);
    free(key); 
}

void eliminar_qcb(void* element){
    t_qcb* qcb = (t_qcb*) element;

    //pthread_mutex_lock(&(qcb->mutex_qcb));
    if (qcb->ruta_arch) free(qcb->ruta_arch);
    //pthread_mutex_unlock(&(qcb->mutex_qcb));
    pthread_mutex_destroy(&(qcb->mutex_qcb));
    free(qcb);
}
    //===============WORKER===============


t_wcb *crear_wcb(int id, int socket) {
    t_wcb* wcb = malloc (sizeof(t_wcb));
    wcb->wid = id;
    wcb->esta_libre = true;
    wcb->qid_asig = -1; //-1 si no tiene query asignada
    wcb->socket = socket;
    pthread_mutex_init(&(wcb->mutex_wcb), NULL);
    //MUTEX PARA LISTA DE WORKERS
    pthread_mutex_init(&(wcb->mutex_socket),NULL);
    sem_init(&(wcb->sem_desalojo), 0, 0);
    pthread_mutex_lock(&mutex_workers);
    list_add(lista_workers, wcb);
    pthread_mutex_unlock(&mutex_workers);
    return wcb;
}

t_wcb *buscar_worker_por_qid(int qid) {
    pthread_mutex_lock(&mutex_workers);
    for(int i = 0; i < list_size(lista_workers); i++){
        t_wcb *candidato = list_get(lista_workers, i);
        //pthread_mutex_lock(&candidato->mutex_wcb);
        if(candidato->qid_asig == qid){
            //pthread_mutex_unlock(&candidato->mutex_wcb);
            pthread_mutex_unlock(&mutex_workers);
            return candidato;
        }
    }
    pthread_mutex_unlock(&mutex_workers);
    return NULL;
}

t_wcb *buscar_worker_libre(){
    pthread_mutex_lock(&mutex_workers);
    for(int i = 0; i < list_size(lista_workers); i++){
        t_wcb *candidato = list_get(lista_workers, i);
        if(candidato) {
            //pthread_mutex_lock(&(candidato->mutex_wcb));
            if(candidato->esta_libre){
                //pthread_mutex_unlock(&(candidato->mutex_wcb));
                pthread_mutex_unlock(&mutex_workers);
                return candidato;
            } else {
                //pthread_mutex_unlock(&(candidato->mutex_wcb));
            }
        }
    }
    pthread_mutex_unlock(&mutex_workers);
    return NULL;
}

t_wcb* buscar_wcb_menor_prio() {
    //ESTA FUNCION SE LLAMABA EN UN INSTANTE ERRONEO
    pthread_mutex_lock(&mutex_workers);
    t_wcb* wcb_prio = list_get(lista_workers,0);
    t_qcb* qcb_prio = buscar_qcb_por_ID(wcb_prio->qid_asig);

    for(int i = 1; i < list_size(lista_workers); i++){
        t_wcb* wcb_actual = list_get(lista_workers,i);
       // pthread_mutex_lock(&(wcb_actual->mutex_wcb));
        int qid_actual = wcb_actual->qid_asig;
        //pthread_mutex_unlock(&(wcb_actual->mutex_wcb));
        t_qcb* qcb_actual = buscar_qcb_por_ID(qid_actual);
        if(qcb_actual) {
            pthread_mutex_lock(&(qcb_actual->mutex_qcb));
            if(qcb_prio->prioridad < qcb_actual->prioridad){
                wcb_prio = wcb_actual;
                qcb_prio = qcb_actual;
            }
            pthread_mutex_unlock(&(qcb_actual->mutex_qcb));
        }
    }
    pthread_mutex_unlock(&mutex_workers);
    return wcb_prio;
}

void eliminar_wcb(t_wcb *aEliminar){
    sem_destroy(&aEliminar->sem_desalojo);
    pthread_mutex_lock(&mutex_workers);
    list_remove_element(lista_workers,aEliminar);
    pthread_mutex_unlock(&mutex_workers);
    close(aEliminar->socket);
    free(aEliminar);
}

//===============PLANIFICACION===============

void* planificar_exit(void *arg){
    //log_info(loggerMaster, "Hilo exit esperando queries a eliminar");
    while(true){
        sem_wait(&hay_en_Exit);
        pthread_mutex_lock(&mutex_cola_exit);
        t_qcb *qcb_elim = list_remove(cola_exit,0);
        pthread_mutex_unlock(&mutex_cola_exit);
        pthread_mutex_lock(&(qcb_elim->mutex_qcb));
        int qid = qcb_elim->qid;
        int prioridad = qcb_elim->prioridad;
        pthread_mutex_unlock(&(qcb_elim->mutex_qcb));
        //pthread_mutex_lock(&mutex_cant_workers);
        log_info(loggerMaster, "## Se desconecta un Query Control. Se finaliza la Query <%d> con prioridad <%d>. Nivel multiprocesamiento <%d>", qid, prioridad, cant_workers);
        //pthread_mutex_unlock(&mutex_cant_workers);
        eliminar_qcb_diccionario(qid);
    }
    return NULL;
}

void* inicializar_planificador(void* arg){
    //log_info(loggerMaster, "Planificador %s", config_struct->algoritmo_planificacion);
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
        //log_info(loggerMaster, "Hay proceso en READY");
        sem_wait(&hay_worker_libre);
        if(list_is_empty(cola_ready)){
            sem_post(&hay_worker_libre);
            continue;
        }
        t_qcb* qcb_exec = list_get(cola_ready,0); // no sería list_remove? y tene un mutex protegiendo la cola
        pthread_mutex_lock(&(qcb_exec->mutex_qcb));
        //log_info(loggerMaster, "Se encontro la Query <%d> con prioridad <%d>", qcb_exec->qid, qcb_exec->prioridad);
        pthread_mutex_unlock(&(qcb_exec->mutex_qcb));
        t_wcb* wcb_elegido = buscar_worker_libre();
        if(wcb_elegido){
            list_remove(cola_ready,0);
            agregar_a_exec(qcb_exec);
            mandar_a_ejecutar(qcb_exec, wcb_elegido);
        }else{
            sem_post(&replanificar);
            continue;
        }
    }
}

void planificador_prioridades(){
    while(true){
        sem_wait(&replanificar);
        if(list_size(cola_ready) > 0){
            //log_info(loggerMaster, "Hay proceso en READY");
            t_qcb* qcb_exec = buscar_qcb_mayor_prio();
            pthread_mutex_lock(&mutex_cant_workers);
            int workers_totales = cant_workers;
            pthread_mutex_unlock(&mutex_cant_workers);
            if(workers_totales > 0) {
                //t_wcb* wcb_elegido = buscar_wcb_menor_prio();
                pthread_mutex_lock(&(qcb_exec->mutex_qcb));
                int qid_exec = qcb_exec->qid;
                log_info(loggerMaster, "Se encontro la Query <%s> con prioridad <%d> - Id asignado: <%d>", qcb_exec->ruta_arch, qcb_exec->prioridad, qid_exec);
                pthread_mutex_unlock(&(qcb_exec->mutex_qcb));
                t_wcb* wcb_elegido = buscar_worker_libre();
                if(wcb_elegido){
                    remover_qcb_cola(qid_exec, cola_ready, &mutex_cola_ready);
                    agregar_a_exec(qcb_exec);
                    mandar_a_ejecutar(qcb_exec, wcb_elegido);
                }else{
                    wcb_elegido = buscar_wcb_menor_prio();
                    if(!wcb_elegido) continue;
                    pthread_mutex_lock(&(wcb_elegido->mutex_wcb)); 
                    int wid = wcb_elegido->wid;
                    int qid_actual = wcb_elegido->qid_asig;
                    pthread_mutex_unlock(&(wcb_elegido->mutex_wcb));
                    t_qcb *qcb_actual = buscar_qcb_por_ID(qid_actual);
                    if(qcb_actual) {
                        bool desalojar = false;
                        pthread_mutex_lock(&(qcb_exec->mutex_qcb));
                        pthread_mutex_lock(&(qcb_actual->mutex_qcb));
                        if(qcb_exec->prioridad < qcb_actual->prioridad)
                            desalojar = true;
                        int prio_exec = qcb_exec->prioridad;
                        pthread_mutex_unlock(&(qcb_actual->mutex_qcb));
                        pthread_mutex_unlock(&(qcb_exec->mutex_qcb));
                        if(desalojar){
                            // LOG OBLIGATORIO
                            log_info(loggerMaster, "## Se desaloja la Query <%d> (<%d>) del Worker <%d> - Motivo: PRIORIDAD", qid_actual, prio_exec, wid);
                            mandar_a_desalojar(qcb_actual);
                            pthread_mutex_lock(&(qcb_actual->mutex_qcb));
                            bool murio = (qcb_actual->estado == EXIT); 
                            pthread_mutex_unlock(&(qcb_actual->mutex_qcb));
                            if(!murio) {
                               // Desalojo exitoso: Mover víctima a Ready y Ejecutar nueva
                                remover_qcb_cola(qid_actual, cola_exec, &mutex_cola_exec);
                                agregar_a_ready(qcb_actual);
                                remover_qcb_cola(qid_exec, cola_ready, &mutex_cola_ready);
                                agregar_a_exec(qcb_exec);
                                mandar_a_ejecutar(qcb_exec, wcb_elegido);
                            } else {
                                sem_post(&replanificar);
                                // Camino Triste: La query terminó sola. No hacemos nada.
                                // El hilo atender_Worker ya se encargó de moverla a Exit y liberar el Worker.
                                log_info(loggerMaster, "La Query <%d> terminó su ejecución durante el intento de desalojo. No se mueve a Ready.", qid_actual);
                            }
                            //log_info(loggerMaster, "REMUEVO DE COLA READY");
                            //remover_qcb_cola(qid_exec, cola_ready, &mutex_cola_ready);
                            //log_info(loggerMaster, "AGREGO A EXEC");
                            ///agregar_a_exec(qcb_exec);
                            //log_info(loggerMaster, "MANDO A EJECUTAR");
                            //mandar_a_ejecutar(qcb_exec, wcb_elegido);
                            
                            // COMENTE ESTO!!!!!!!!!!!
                            //remover_qcb_cola(qid_actual, cola_exec, &mutex_cola_exec);
                            //agregar_a_ready(qcb_actual);
                        }
                    }
                }
            }else{
                log_info(loggerMaster, "No hay workers disponibles");
            }
        }else{
            log_info(loggerMaster, "No hay queries en READY");
        }
    }
}

void* hilo_aging(void* arg){
    t_qcb* qcb = (t_qcb*) arg;

    while(qcb->estado == READY && qcb->prioridad > 0){
        
        usleep(tiempo_aging * 1000);
        pthread_mutex_lock(&(qcb->mutex_qcb));
        if(qcb->estado != READY){
            pthread_mutex_unlock(&(qcb->mutex_qcb));
            break;
        }
        int prioridadAntes = qcb->prioridad;
        qcb->prioridad -= 1;
        log_info(loggerMaster, "##%d Cambio de prioridad: %d - %d", qcb->qid, prioridadAntes, qcb->prioridad);
        pthread_mutex_unlock(&(qcb->mutex_qcb));
        sem_post(&replanificar);
    }
    return NULL;
}


void mandar_a_ejecutar(t_qcb* qcb, t_wcb* worker) {
    //log_info(loggerMaster,"ENVIANDO A EJECUTAR");
    pthread_mutex_lock(&worker->mutex_wcb);
    worker->esta_libre = false;
    pthread_mutex_lock(&(qcb->mutex_qcb));
    worker->qid_asig = qcb->qid;
    // LOG OBLIGATORIO //
    log_info(loggerMaster, "Se envía la Query <%d> (<%d>) al Worker <%d>", qcb->qid, qcb->prioridad, worker->wid);
    t_paquete* paquete = crear_paquete(EJECUTAR);
    agregar_a_paquete(paquete, &(qcb->qid), sizeof(int));
    agregar_a_paquete(paquete, &(qcb->pc), sizeof(int));
    agregar_a_paquete_string(paquete, qcb->ruta_arch, strlen(qcb->ruta_arch));
    pthread_mutex_lock(&(worker->mutex_socket));
    enviar_paquete(paquete, worker->socket);
    pthread_mutex_unlock(&(worker->mutex_socket));
    pthread_mutex_unlock(&(qcb->mutex_qcb));
    pthread_mutex_unlock(&worker->mutex_wcb);
    eliminar_paquete(paquete);
}

//===============COLAS===============
//ACTUALICE ESTADOS ACA
void agregar_a_ready(t_qcb* qcb){
    actualizar_Estado(qcb, READY);
    nuevo_a_ready(qcb);
}

void nuevo_a_ready(t_qcb* qcb) {
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

void remover_qcb_cola(int qid, t_list *cola, pthread_mutex_t* mutexCola){
    pthread_mutex_lock(mutexCola);
    for(int i = 0; i < list_size(cola); i++){
        t_qcb *candidato = list_get(cola,i);
        //pthread_mutex_lock(&(candidato->mutex_qcb));
        if(candidato->qid == qid){
        //pthread_mutex_unlock(&(candidato->mutex_qcb));
            log_info(loggerMaster, "Remuevo la Query %d de la cola correspondiente", qid);
            list_remove(cola,i);
            pthread_mutex_unlock(mutexCola);
            return;
        }
        //pthread_mutex_unlock(&(candidato->mutex_qcb));
    }
    pthread_mutex_unlock(mutexCola);
    log_info(loggerMaster, "No se encontro el qid %d en la cola a remover", qid);
    return;
}

//FUNCION PARA MOSTRAR NRO DE ESTADO COMO STRING
char *mostrar_Estado(t_estado estado){
    switch(estado){
        case READY:
            return "READY";
        case EXEC:
            return "EXEC";
        case EXIT:
            return "EXIT";
    }
}