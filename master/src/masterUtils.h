#ifndef MASTERUTILS_H
#define MASTERUTILS_H

#define TRUE 1
#include <utils/protocolo.h>
#include <pthread.h>
#include <sys/socket.h>
#include <commons/log.h>
#include <commons/config.h>
#include <semaphore.h>
#include <stdbool.h>
#include <utils/chiches.h>
#include <commons/collections/dictionary.h>
#include <commons/collections/list.h>
#include <commons/collections/queue.h>

typedef struct {
    char* modulo;
    char* puerto_escucha;
    char* algoritmo_planificacion;
    char* tiempo_aging;
    char* log_level;
} t_config_master;

typedef enum {
    READY,
    EXEC,
    EXIT
}t_estado;

typedef struct {
    int qid;
    int pc;
    t_estado estado;
    char* ruta_arch;
    int prioridad;
    int socket;  //NUEVO: socket del QueryControl
    pthread_mutex_t mutex_qcb;
} t_qcb;

typedef struct {
    int wid;
    bool esta_libre;
    int qid_asig;
    int socket;
    pthread_mutex_t mutex_wcb;
} t_wcb;


//===============ESTRUCTURAS===============
extern t_log* loggerMaster;
extern t_config* config;
extern t_config_master* config_struct; 
extern char* config_master;
extern int cant_workers;
extern t_dictionary* diccionario_qcb;
extern int qid;
//===============INICIALIZACION===============
void inicializar_master();
void inicializar_listas();
void inicializar_config();
void inicializar_diccionario();
void inicializar_semaforos();
void cargar_config ();
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle);
void crear_logger ();
//===============CONEXIONES===============
void* inicializar_servidor_multihilo(void* arg);
void* atender_conexion(void* arg);
void atender_Worker(int fd);
void atender_QueryControl(int fd);
void mandar_a_desalojar(t_qcb* qcb);
//===============ESTRUCTURAS ADMIN.===============

    //===============QUERY_CONTROL===============
t_qcb* crear_query_control(char* path, int prioridad, int fd);
void actualizar_Estado(t_qcb* qcb, t_estado nuevo_estado);
t_qcb* buscar_qcb_por_ID(int qid);
t_qcb* buscar_qcb_mayor_prio();
void eliminar_qcb_diccionario(int qid);
void eliminar_qcb(void* element);
    //===============WORKER===============
//void crear_wcb (int id, int socket);
t_wcb *crear_wcb(int id, int socket);
t_wcb *buscar_worker_por_qid(int qid);
t_wcb* buscar_worker_libre();
t_wcb* buscar_wcb_menor_prio();
void eliminar_wcb(t_wcb *aEliminar);
//===============PLANIFICACION===============
void *planificar_exit(void *arg);
void* inicializar_planificador(void* arg);
void planificador_fifo();
void planificador_prioridades();
void* hilo_aging(void* arg);
void mandar_a_ejecutar(t_qcb* qcb, t_wcb* worker);
//===============COLAS===============
void agregar_a_ready(t_qcb* qcb);
void agregar_a_exec(t_qcb* qcb);
void agregar_a_exit(t_qcb* qcb);
void remover_qcb_cola(int qid, t_list *cola, pthread_mutex_t mutexCola);
//===============MENSAJES===============
void enviar_mensaje_exit(int socketQuery, t_motivo motivo);

#endif