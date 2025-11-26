#ifndef STORAGEUTILS_H
#define STORAGEUTILS_H

#define TRUE 1
#include <utils/protocolo.h>
#include <pthread.h>
#include <sys/socket.h>
#include <commons/log.h>
#include <commons/config.h>
#include <commons/crypto.h>
#include <commons/bitarray.h>
#include <utils/chiches.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <sys/types.h>

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
    int logBlocks;
    //t_list *logBlocks;
    t_estadoTag estado;
    pthread_mutex_t mutexTag;
}t_tag;

typedef struct {
    int tamanio;
    t_list* blocks;  // lista de bloques (enteros)
    char* estado;    // "WIP" o "COMMIT"
} t_metadata;

typedef struct {
    int socket;
    int ID_Worker;
} t_worker;


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
void recuperar_tags_file(char *nombre_archivo, t_fcb *file);
void terminar_programa(int signal);
//==========CONEXIONES==========
void iniciar_servidor_multihilo(void);
void* atender_worker(void* arg);
//==========ATENDER PETICIONES WORKER============
void atenderCreate(int fd_conexion, void* buffer);
void atenderTruncate(int fd_conexion, void* buffer);
void atenderTag(int fd_conexion, void* buffer);
void atenderCommit(int fd_conexion, void* buffer);
void atenderWrite(int fd_conexion, void* buffer);
void atenderRead(int fd_conexion, void* buffer);
void atenderDelete(int fd_conexion, void* buffer);
int recibir_QID_nombreArch_nombreTag(void* buffer, int* QID, char** nombreArch,char** nombreTag);
//==========FRESH_START==========
void inicializar_montaje();
void cargar_config_hashIndex();
void cargar_config_superBlock();
void freshStart();
void verificar_freshStart();
void formateo();
void inicializar_semaforos();
//==========ELIMINACION Y CREACION DE ESTRUCTURAS==========
void limpiar_fs();
void recrear_fs();
void crear_bitmap();
void crear_directorios();
void crear_directorio(char* path, char* nombreDirectorio, char *nuevoPath);
void crear_BlocksHashIndex();
void crear_physical_blocks();
void initialFile();
void recuperar_estructuras_FS();
void recargar_bitmap();
//char *completar_ceros(int aCompletar);
//==========BITMAP==========
int buscar_bloque_libre(int query_id);
char *obtener_path_bloque_fisico(int nroBloque);
void marcar_libre_en_bitmap(int nro_fisico);
void marcar_ocupado_en_bitmap(int nro_fisico);
//==========FORMATO DE ENTRADAS==========
int calcularAncho();
//==========OPERACIONES==========
t_motivo op_create(char *nombreArch, char *nombreTag, int query_id);
t_motivo op_truncate(char* nombreArch, char *nombreTag, int nuevoTamanio, int query_id);
t_motivo op_commit(char* nombreArch, char *nombreTag, int query_id);
t_motivo op_write_block(char* nombreArch, char *nombreTag, int nroBloque, void *contenido, int query_id);
t_motivo op_read_block(char* nombreArch, char *nombreTag, int nroBloque, char **contenido, int query_id);
t_motivo op_delete_tag(char* nombreArch, char *nombreTag, int query_id);
t_motivo op_tag(char* nombreArch, char *nombreTagOrigen, char* nombreArchDestino,char *nombreNuevoTag, int query_id);
void crear_metadata(char* path, char* nuevoPath);
void destruir_metadata(t_metadata* meta);
t_metadata* leer_metadata(char* archivo, char* nombreTag);
void guardar_metadata(t_metadata* meta, char* archivo, char* nombreTag);
t_motivo agrandarArchivo(t_metadata* meta, char* pathTag, int nro, char* path_block0);
void achicarArchivo (t_metadata* meta, char* pathTag, int ancho, int nro, int bloque_fisico);
char* leer_contenido_bloque(char* path_bloque_logico);
void liberar_bloque_si_no_referenciado(int bloque_fisico, int query_id);
//============================= FCB Y TAGS ==================================
t_fcb *crear_fcb(char *nombreNuevoArch, char *nombreNuevoTag);
t_tag *crear_tag(char *nombreNuevoTag, char *nombreArch,t_dictionary *diccionarioTagsArch);
t_tag *buscar_Tag_Arch(char *Arch, char *Tag);
char *path_Metadata(char *nombreArch, char *nombreTag);
//void eliminar_bloq_log(t_tag *tag);
char *obtener_path_bloq_logico(t_tag *tag, int nroBloqLog);
char* crear_bloq_log(char* pathTag, t_metadata *meta,int nro);
void eliminar_bloq_log (char* pathTag, int nro);
void crear_copia_tag(char* nombreArch, t_tag *tagOrigen, char *nombreNuevoTag);
bool tagRepetido(char *nombreArch, char *nombreTag);
bool archRepetido(char *nombreArch);
void eliminarStructTag(char* nombreArch, char *nombreTag);

#endif