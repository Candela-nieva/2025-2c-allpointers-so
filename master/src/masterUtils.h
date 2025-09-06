#ifndef MASTERUTILS_H
#define MASTERUTILS_H

#define TRUE 1

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

// =================== MAIN Y BASIC =========================
void inicializar_config(void);
void crear_logger ();
void cargar_config ();
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle);


#endif