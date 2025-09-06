#ifndef QUERYCTRUTILS_H
#define QUERYCTRUTILS_H

#define TRUE 1
#include <utils/hello.h>
#include <utils/chiches.h>
#include <sys/socket.h>
#include <commons/log.h>
#include <commons/config.h>
typedef struct {
    char* modulo;
    char* ip;
    char* puerto_master;
    char* log_level;
} t_config_queryctrl;

extern t_log* loggerQueryCTRL;
extern t_config* config;
extern t_config_queryctrl* config_struct; 
extern char* config_queryCTRL;
// =================== MAIN Y BASIC =========================
void inicializar_config(void);
void crear_logger ();
void cargar_config ();
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle);


#endif