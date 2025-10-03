#ifndef WORKERUTILS_H
#define WORKERUTILS_H

#define TRUE 1
#include <utils/protocolo.h>
#include <utils/chiches.h>
#include <pthread.h>
#include <sys/socket.h>
#include <commons/log.h>
#include <commons/config.h>
typedef struct {
    char* modulo;
    char* ip_master;
    char* puerto_master;
    char* ip_storage;
    char* puerto_storage;
    char* tam_memoria;
    char* retardo_memoria;
    char* algoritmo_reemplazo;
    char* path_scripts;
    char* log_level;
} t_config_worker;

extern t_log* loggerWorker;
extern t_config* config;
extern t_config_worker* config_struct; 
extern char* config_worker;

// =================== MAIN Y BASIC =========================
void inicializar_config(void);
void crear_logger ();
void cargar_config ();
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle);
void* iniciar_conexion_storage(void* arg);
void *manejar_ejecutar(void* buffer);
//void manejar_ejecutar(void* buffer);
void* iniciar_conexion_master(void* arg);
void esperar_queries();
#endif