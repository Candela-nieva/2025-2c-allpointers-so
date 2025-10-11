#ifndef STORAGEUTILS_H
#define STORAGEUTILS_H

#define TRUE 1
#include <utils/protocolo.h>
#include <pthread.h>
#include <sys/socket.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/bitarray.h>
#include <utils/chiches.h>
#include <sys/mman.h>
typedef struct {
    char* modulo;
    char* puerto_escucha;
    char* fresh_start;
    char* punto_montaje;
    char* retardo_operacion;
    char* retardo_acceso_bloque;
    char* log_level;
} t_config_storage;

typedef struct {
    char* fs_size;
    char* tam_bloq;
} t_config_superblock;

extern t_log* loggerStorage;
extern t_config* config;
extern t_config* config_SB;
extern t_config_storage* config_struct; 
extern t_config_superblock* config_superBlock;
extern char* config_storage;
extern t_dictionary* diccionario_qcb;
// =================== MAIN Y BASIC =========================
void inicializar_config(void);
void crear_logger();
void cargar_config();
void inicializar_montaje();
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle);
void iniciar_servidor_multihilo(void);
void cargar_config_superBlock();
void crear_bitmap();

#endif