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
#include <sys/stat.h>
#include <errno.h>


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

typedef enum {
    WORK_IN_PROGRESS,
    COMMITED
}t_estadoTag;

typedef struct {
    char *nombreArch;
    t_dictionary *tags;
}t_fcb;

typedef struct {
    char* nombreTag;
    char* pathTag;
    size_t tamanio;
    //usar listas para saber que bloques ocupa?
    t_list *physicalBlocks;
    t_list *logBlocks;
    t_estadoTag estado;
}t_tag;

typedef struct {
    int tamanio;
    t_list* blocks;  // lista de bloques (enteros)
    char* estado;    // "WIP" o "COMMIT"
} t_metadata;

extern t_log* loggerStorage;
extern t_config* config;
extern t_config* config_SB;
extern t_config_storage* config_struct; 
extern t_config_superblock* config_superBlock;
extern char* config_storage;
extern t_dictionary* diccionario_qcb;
//==========INICIALIZACION==========
void inicializar_config(void);
void cargar_config();
void crear_logger();
t_log* iniciar_logger(char* nombreArchivoLog, char* nombreLog, bool seMuestraEnConsola, t_log_level nivelDetalle);

//==========CONEXIONES==========
void iniciar_servidor_multihilo(void);

//==========FRESH_START==========
void inicializar_montaje();
void cargar_config_superBlock();
void freshStart();
void verificar_freshStart();
void formateo();
//==========ELIMINACION Y CREACION DE ESTRUCTURAS==========
void limpiar_fs();
void recrear_fs();
void crear_bitmap();
void crear_directorios();
void crear_directorio(char* path, char* nombreDirectorio, char *nuevoPath);
void crear_BlocksHashIndex();
void crear_physical_blocks();
void initialFile();
//char *completar_ceros(int aCompletar);
//==========BITMAP==========
int buscar_bloque_libre();
char *buscar_bloque_fisico(int nroBloque);
void marcar_libre_en_bitmap(int nro_fisico);
//==========FORMATO DE ENTRADAS==========
int calcularAncho();
//==========OPERACIONES==========
bool op_create(char *nombreArch, char *nombreTag);
bool op_truncate(char* nombreArch, char *nombreTag, int nuevoTamanio);
void crear_metadata(char* path, char* nuevoPath);
void destruir_metadata(t_metadata* meta);
bool op_tag(char* nombreArch, char *nombreTagOrigen, char *nombreNuevoTag);
t_metadata* leer_metadata(char* archivo, char* nombreTag);
void guardar_metadata(t_metadata* meta, char* archivo, char* nombreTag);
bool agrandarArchivo (t_metadata* meta, char* pathTag, int nro, char* path_block0);
void achicarArchivo (t_metadata* meta, char* pathTag, int ancho, int nro, int bloque_fisico);
//============================= FCB Y TAGS ==================================
t_fcb *crear_fcb(char *nombreNuevoArch, char *nombreNuevoTag);
t_tag *crear_tag(char *nombreNuevoTag, char *nombreArch,t_dictionary *diccionarioTagsArch);
t_tag *buscar_Tag_Arch(char *Arch, char *Tag);
char *path_Metadata(char *nombreArch, char *nombreTag);
//void eliminar_bloq_log(t_tag *tag);
char *buscar_bloq_logico(t_tag *tag, int nroBloqLog);
char* crear_bloq_log(char* pathTag, t_metadata *meta,int nro);
void eliminar_bloq_log (char* pathTag, int nro);
void crear_copia_tag(char* nombreArch, t_tag *tagOrigen, char *nombreNuevoTag);
#endif