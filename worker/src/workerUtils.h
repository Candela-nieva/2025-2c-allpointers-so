#ifndef WORKERUTILS_H
#define WORKERUTILS_H

#define TRUE 1
#include <utils/hello.h>
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

extern t_log* loggerMaster;
extern t_config* config;
extern t_config_master* config_struct; 

// =================== MAIN Y BASIC =========================
void inicializar_config(void);
void crear_logger ();
void cargar_config ();
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle);


#endif