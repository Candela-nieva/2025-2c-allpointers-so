#ifndef QUERYCTRUTILS_H
#define QUERYCTRUTILS_H

#define TRUE 1

typedef struct {
    char* modulo;
    char* ip;
    char* puerto_master;
} t_config_queryctrl;

extern t_log* loggerQueryCTRL;
extern t_config* config;
extern t_config_QueryCTRL* config_struct; 

// =================== MAIN Y BASIC =========================
void inicializar_config(void);
void crear_logger ();
void cargar_config ();
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle);


#endif