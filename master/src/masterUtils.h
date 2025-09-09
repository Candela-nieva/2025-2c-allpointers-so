#ifndef MASTERUTILS_H
#define MASTERUTILS_H

#define TRUE 1
#include <utils/protocolo.h>
#include <pthread.h>
#include <sys/socket.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/chiches.h>
#include <commons/collections/dictionary.h>

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
    t_estado estado;
    char* ruta_arch;
    int prioridad;
} t_qcb;

extern t_log* loggerMaster;
extern t_config* config;
extern t_config_master* config_struct; 
extern char* config_master;
extern int cant_workers;
extern t_dictionary* diccionario_qcb;
extern int qid;
// =================== MAIN Y BASIC =========================
void inicializar_master();
void inicializar_config();
void crear_logger ();
void cargar_config ();
void inicializar_diccionario();
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle);
t_qcb* crear_query_control(char* path, int prioridad);
void* atender_conexion(void* arg);
void atender_QueryControl(int fd);
void atender_Worker(int fd);
void* inicializar_servidor_multihilo(void* arg);
void* inicializar_planificador(void* arg);
#endif