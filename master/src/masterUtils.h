#ifndef MASTERUTILS_H
#define MASTERUTILS_H

#define TRUE 1
#include <utils/protocolo.h>
#include <pthread.h>
#include <sys/socket.h>
#include <commons/log.h>
#include <commons/config.h>
#include <utils/chiches.h>

typedef struct {
    char* modulo;
    char* puerto_escucha;
    char* algoritmo_planificacion;
    char* tiempo_aging;
    char* log_level;
} t_config_master;

extern t_log* loggerMaster;
extern t_config* config;
extern t_config_master* config_struct; 
extern char* config_master;
extern int cant_workers;
extern int qid;
// =================== MAIN Y BASIC =========================
void inicializar_config();
void crear_logger ();
void cargar_config ();
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle);
void* atender_conexion(void* arg);
void atender_QueryControl(int fd);
void atender_Worker(int fd);
void inicializar_servidor_multihilo();
#endif