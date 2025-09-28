/*
 * servidor.c - Servidor de Chat usando Colas de Mensajes System V
 * 
 * Este programa implementa un servidor de chat multi-sala que utiliza
 * colas de mensajes System V para la comunicación entre clientes.
 * 
 * Funcionalidades:
 * - Creación automática de salas de chat
 * - Gestión de usuarios en múltiples salas
 * - Distribución de mensajes a todos los usuarios de una sala
 * - Limpieza automática de recursos al terminar
 * 
 * Protocolo de mensajes:
 * - Tipo 1 (JOIN): Cliente se une a una sala
 * - Tipo 2 (RESP): Respuesta del servidor al cliente
 * - Tipo 3 (MSG): Mensaje de cliente a sala
 * - Tipo 4 (CHAT): Mensaje distribuido por el servidor
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>    // tipos de datos del sistema
#include <sys/ipc.h>      // comunicación entre procesos
#include <sys/msg.h>      // colas de mensajes
#include <unistd.h>       // funciones estándar de Unix
#include <signal.h>       // manejo de señales
#include <errno.h>        // códigos de error

/* ==================== CONSTANTES Y CONFIGURACIÓN ==================== */
#define MAX_SALAS 10                    // Número máximo de salas de chat simultáneas
#define MAX_USUARIOS_POR_SALA 20        // Límite de usuarios por sala
#define MAX_TEXTO 256                   // Longitud máxima de un mensaje de texto
#define MAX_NOMBRE 50                   // Longitud máxima para nombres de usuario y salas

/* ==================== ESTRUCTURAS DE DATOS ==================== */

/**
 * Estructura de mensaje para comunicación cliente-servidor
 * Esta estructura se utiliza tanto para enviar mensajes del cliente al servidor
 * como para las respuestas del servidor a los clientes.
 */
struct mensaje {
    long mtype;                     // Tipo de mensaje (1=JOIN, 2=RESP, 3=MSG, 4=CHAT)
    int reply_qid;                  // ID de cola privada del cliente (para respuestas)
    char remitente[MAX_NOMBRE];     // Nombre del usuario que envía el mensaje
    char texto[MAX_TEXTO];          // Contenido del mensaje o texto de respuesta
    char sala[MAX_NOMBRE];          // Nombre de la sala de destino
};

/**
 * Estructura que representa una sala de chat en memoria del servidor
 * Mantiene información sobre usuarios conectados y su cola de mensajes asociada
 */
struct sala {
    char nombre[MAX_NOMBRE];                            // Nombre identificador de la sala
    int cola_id;                                        // ID de cola System V asociada a la sala
    int num_usuarios;                                   // Contador actual de usuarios en la sala
    char usuarios[MAX_USUARIOS_POR_SALA][MAX_NOMBRE];  // Array de nombres de usuarios conectados
    int usuarios_qid[MAX_USUARIOS_POR_SALA];           // Array de IDs de colas privadas de usuarios
};

/* ==================== VARIABLES GLOBALES ==================== */
struct sala salas[MAX_SALAS];       // Array de todas las salas de chat disponibles
int num_salas = 0;                  // Contador actual de salas activas
int cola_global = -1;               // ID de la cola global donde llegan todos los mensajes

/* ==================== PROTOTIPOS DE FUNCIONES ==================== */
int crear_sala(const char *nombre);                                        // Crea una nueva sala de chat
int buscar_sala(const char *nombre);                                       // Busca una sala por nombre
int agregar_usuario_a_sala(int indice_sala, const char *nombre_usuario, int qid_usuario);  // Agrega usuario a sala
void enviar_a_todos_en_sala(int indice_sala, struct mensaje *msg);        // Distribuye mensaje a todos los usuarios
void limpiar_colas_y_salir(int signo);                                    // Limpia recursos y termina el servidor

/* ==================== IMPLEMENTACIÓN DE FUNCIONES ==================== */

/**
 * Crear una nueva sala de chat
 * 
 * Genera una clave única usando ftok() y crea una cola de mensajes System V
 * asociada a la sala. Inicializa la estructura de datos en memoria.
 * 
 * @param nombre Nombre de la sala a crear
 * @return Índice de la sala creada, o -1 si hay error
 */
int crear_sala(const char *nombre) {
    // Verificar que no excedamos el límite máximo de salas
    if (num_salas >= MAX_SALAS) {
        return -1;
    }

    // Generar clave única para la cola de mensajes de esta sala
    // Usamos un proj_id distinto por sala para evitar colisiones
    key_t key = ftok("/tmp", 100 + num_salas);
    if (key == (key_t)-1) {
        perror("ftok");
        return -1;
    }
    
    // Crear la cola de mensajes System V para esta sala
    int cola_id = msgget(key, IPC_CREAT | 0666);
    if (cola_id == -1) {
        perror("msgget crear sala");
        return -1;
    }

    // Inicializar la estructura de sala en memoria
    strncpy(salas[num_salas].nombre, nombre, MAX_NOMBRE);
    salas[num_salas].nombre[MAX_NOMBRE-1] = '\0';  // Asegurar terminación nula
    salas[num_salas].cola_id = cola_id;
    salas[num_salas].num_usuarios = 0;
    
    // Incrementar contador de salas y retornar índice de la nueva sala
    num_salas++;
    return num_salas - 1;
}

/**
 * Buscar una sala por su nombre
 * 
 * Recorre el array de salas buscando una con el nombre especificado
 * 
 * @param nombre Nombre de la sala a buscar
 * @return Índice de la sala si existe, -1 si no se encuentra
 */
int buscar_sala(const char *nombre) {
    // Recorrer todas las salas existentes
    for (int i = 0; i < num_salas; i++) {
        // Comparar nombres de manera exacta
        if (strcmp(salas[i].nombre, nombre) == 0) {
            return i;  // Sala encontrada, retornar índice
        }
    }
    return -1;  // Sala no encontrada
}

/**
 * Agregar un usuario a una sala específica
 * 
 * Registra al usuario en la sala guardando su nombre y el ID de su cola privada
 * para poder enviarle mensajes posteriormente. Verifica duplicados y límites.
 * 
 * @param indice_sala Índice de la sala en el array de salas
 * @param nombre_usuario Nombre del usuario a agregar
 * @param qid_usuario ID de la cola privada del usuario
 * @return 0 si éxito, -1 si error (sala inválida, llena, o usuario duplicado)
 */
int agregar_usuario_a_sala(int indice_sala, const char *nombre_usuario, int qid_usuario) {
    // Validar que el índice de sala sea válido
    if (indice_sala < 0 || indice_sala >= num_salas) {
        return -1;
    }
    
    struct sala *s = &salas[indice_sala];
    
    // Verificar que la sala no esté llena
    if (s->num_usuarios >= MAX_USUARIOS_POR_SALA) {
        return -1;
    }

    // Verificar que el usuario no esté ya en la sala (evitar duplicados)
    for (int i = 0; i < s->num_usuarios; i++) {
        if (strcmp(s->usuarios[i], nombre_usuario) == 0) {
            return -1; // Usuario ya existe en la sala
        }
    }

    // Agregar el nuevo usuario a la sala
    strncpy(s->usuarios[s->num_usuarios], nombre_usuario, MAX_NOMBRE);
    s->usuarios[s->num_usuarios][MAX_NOMBRE-1] = '\0';  // Asegurar terminación nula
    s->usuarios_qid[s->num_usuarios] = qid_usuario;     // Guardar ID de cola para mensajes
    s->num_usuarios++;
    
    return 0;  // Éxito
}

/**
 * Distribuir mensaje a todos los usuarios de una sala
 * 
 * Toma un mensaje recibido de un usuario y lo reenvía a todos los demás
 * usuarios de la misma sala, usando sus colas privadas. El remitente
 * original no recibe una copia de su propio mensaje.
 * 
 * @param indice_sala Índice de la sala donde distribuir el mensaje
 * @param msg Mensaje original recibido del cliente
 */
void enviar_a_todos_en_sala(int indice_sala, struct mensaje *msg) {
    // Validar índice de sala
    if (indice_sala < 0 || indice_sala >= num_salas) {
        return;
    }
    
    struct sala *s = &salas[indice_sala];

    // Construir mensaje de salida tipo CHAT (4) para distribución
    struct mensaje out;
    out.mtype = 4;  // Tipo CHAT para mensajes distribuidos
    out.reply_qid = 0;  // No necesario para mensajes de difusión
    
    // Copiar datos del mensaje original, asegurando terminación nula
    strncpy(out.remitente, msg->remitente, MAX_NOMBRE);
    out.remitente[MAX_NOMBRE-1] = '\0';
    strncpy(out.texto, msg->texto, MAX_TEXTO);
    out.texto[MAX_TEXTO-1] = '\0';
    strncpy(out.sala, msg->sala, MAX_NOMBRE);
    out.sala[MAX_NOMBRE-1] = '\0';

    // Enviar mensaje a todos los usuarios de la sala
    for (int i = 0; i < s->num_usuarios; i++) {
        // Excluir al remitente original (no enviarse el mensaje a sí mismo)
        if (strcmp(s->usuarios[i], msg->remitente) == 0) {
            continue;
        }

        // Obtener ID de cola privada del usuario destinatario
        int qid_dest = s->usuarios_qid[i];
        
        // Intentar enviar mensaje a la cola del usuario
        if (msgsnd(qid_dest, &out, sizeof(struct mensaje) - sizeof(long), 0) == -1) {
            // En caso de error, registrarlo pero continuar con otros usuarios
            fprintf(stderr, "Error al enviar mensaje a usuario '%s' (qid=%d): %s\n", 
                    s->usuarios[i], qid_dest, strerror(errno));
        }
    }
}

/**
 * Función de limpieza llamada al recibir señales de terminación
 * 
 * Esta función se ejecuta cuando el servidor recibe SIGINT (Ctrl+C) o SIGTERM.
 * Se encarga de eliminar todas las colas de mensajes System V creadas para
 * evitar que queden recursos huérfanos en el sistema.
 * 
 * @param signo Número de la señal recibida (no utilizado)
 */
void limpiar_colas_y_salir(int signo) {
    printf("\nServidor: recibida señal de terminación, limpiando recursos...\n");
    
    // Eliminar cola global si existe
    if (cola_global != -1) {
        if (msgctl(cola_global, IPC_RMID, NULL) == 0) {
            printf("Cola global eliminada correctamente\n");
        } else {
            perror("Error al eliminar cola global");
        }
    }
    
    // Eliminar todas las colas de salas creadas
    for (int i = 0; i < num_salas; i++) {
        if (salas[i].cola_id != -1) {
            if (msgctl(salas[i].cola_id, IPC_RMID, NULL) == 0) {
                printf("Cola de sala '%s' eliminada correctamente\n", salas[i].nombre);
            } else {
                fprintf(stderr, "Error al eliminar cola de sala '%s': %s\n", 
                        salas[i].nombre, strerror(errno));
            }
        }
    }
    
    printf("Servidor terminado correctamente.\n");
    exit(0);
}

/* ==================== FUNCIÓN PRINCIPAL ==================== */

/**
 * Función principal del servidor de chat
 * 
 * Inicializa el servidor, crea la cola global de mensajes, instala manejadores
 * de señales para limpieza, y entra en el bucle principal de procesamiento
 * de mensajes de los clientes.
 */
int main() {
    /* Configuración inicial del servidor */
    
    // Instalar manejadores de señal para limpieza automática de recursos
    signal(SIGINT, limpiar_colas_y_salir);   // Ctrl+C
    signal(SIGTERM, limpiar_colas_y_salir);  // Terminación solicitada

    // Crear la cola global de mensajes usando una clave conocida
    // Esta cola recibirá todos los mensajes iniciales de los clientes
    key_t key_global = ftok("/tmp", 'A');
    if (key_global == (key_t)-1) {
        perror("Error generando clave para cola global");
        exit(1);
    }
    
    // Crear o conectar a la cola global (permisos 0666 para lectura/escritura)
    cola_global = msgget(key_global, IPC_CREAT | 0666);
    if (cola_global == -1) {
        perror("Error creando cola global");
        exit(1);
    }

    printf("=== Servidor de Chat Iniciado ===\n");
    printf("Cola global ID: %d\n", cola_global);
    printf("Esperando conexiones de clientes...\n\n");

    /* Bucle principal de procesamiento de mensajes */
    struct mensaje msg;
    while (1) {
        // Recibir cualquier tipo de mensaje de la cola global
        // El parámetro msgtyp=0 significa "aceptar cualquier tipo"
        ssize_t r = msgrcv(cola_global, &msg, sizeof(struct mensaje) - sizeof(long), 0, 0);
        
        // Manejar errores de recepción
        if (r == -1) {
            if (errno == EINTR) {
                // Interrupción por señal (normal), continuar
                continue;
            }
            perror("Error recibiendo mensaje de cola global");
            continue;
        }

        /* Procesamiento de mensaje tipo JOIN (1) */
        if (msg.mtype == 1) {
            // Cliente solicita unirse a una sala
            printf("[JOIN] Usuario '%s' solicita unirse a sala '%s' (cola_cliente=%d)\n", 
                   msg.remitente, msg.sala, msg.reply_qid);
            
            // Buscar si la sala ya existe
            int idx = buscar_sala(msg.sala);
            
            if (idx == -1) {
                // La sala no existe, intentar crearla
                idx = crear_sala(msg.sala);
                
                if (idx == -1) {
                    // Error al crear la sala (límite alcanzado)
                    printf("[ERROR] No se pudo crear la sala '%s'\n", msg.sala);
                    
                    struct mensaje resp;
                    resp.mtype = 2;  // Tipo RESP
                    resp.reply_qid = 0;
                    snprintf(resp.texto, MAX_TEXTO, 
                            "Error: no se pudo crear la sala '%s' (límite de salas alcanzado)", 
                            msg.sala);
                    
                    // Enviar error a la cola privada del cliente
                    msgsnd(msg.reply_qid, &resp, sizeof(struct mensaje) - sizeof(long), 0);
                    continue;
                }
                printf("[INFO] Nueva sala creada: '%s' (ID=%d)\n", msg.sala, salas[idx].cola_id);
            }

            // Intentar agregar el usuario a la sala
            if (agregar_usuario_a_sala(idx, msg.remitente, msg.reply_qid) == 0) {
                // Usuario agregado exitosamente
                printf("[JOIN] Usuario '%s' agregado a sala '%s' (%d/%d usuarios)\n", 
                       msg.remitente, msg.sala, salas[idx].num_usuarios, MAX_USUARIOS_POR_SALA);
                
                // Enviar confirmación de éxito al cliente
                struct mensaje resp;
                resp.mtype = 2;  // Tipo RESP
                resp.reply_qid = 0;
                snprintf(resp.texto, MAX_TEXTO, "Bienvenido a la sala '%s'!", msg.sala);
                msgsnd(msg.reply_qid, &resp, sizeof(struct mensaje) - sizeof(long), 0);
                
            } else {
                // Error al agregar usuario (duplicado o sala llena)
                printf("[ERROR] No se pudo agregar usuario '%s' a sala '%s'\n", 
                       msg.remitente, msg.sala);
                
                struct mensaje resp;
                resp.mtype = 2;  // Tipo RESP
                resp.reply_qid = 0;
                snprintf(resp.texto, MAX_TEXTO, 
                        "Error: no se pudo unir a la sala '%s' (usuario duplicado o sala llena)", 
                        msg.sala);
                        
                // Enviar mensaje de error al cliente
                msgsnd(msg.reply_qid, &resp, sizeof(struct mensaje) - sizeof(long), 0);
            }
        } else if (msg.mtype == 3) {
            /* Procesamiento de mensaje tipo MSG (3) */
            // Cliente envía un mensaje de chat a su sala
            printf("[MSG] Sala='%s', Usuario='%s': %s\n", 
                   msg.sala, msg.remitente, msg.texto);
            
            // Buscar la sala de destino
            int idx = buscar_sala(msg.sala);
            
            if (idx != -1) {
                // Sala encontrada, distribuir mensaje a todos los usuarios
                printf("[DISTRIBUCIÓN] Enviando mensaje a %d usuarios en sala '%s'\n", 
                       salas[idx].num_usuarios, msg.sala);
                enviar_a_todos_en_sala(idx, &msg);
                
            } else {
                // Sala no existe, notificar error al remitente
                printf("[ERROR] Sala '%s' no existe, notificando a '%s'\n", 
                       msg.sala, msg.remitente);
                
                struct mensaje resp;
                resp.mtype = 2;  // Tipo RESP
                resp.reply_qid = 0;
                snprintf(resp.texto, MAX_TEXTO, 
                        "Error: la sala '%s' no existe o fue eliminada", msg.sala);
                        
                // Enviar notificación de error a la cola privada del cliente
                msgsnd(msg.reply_qid, &resp, sizeof(struct mensaje) - sizeof(long), 0);
            }
            
        } else {
            /* Mensaje de tipo desconocido */
            printf("[WARNING] Mensaje de tipo desconocido recibido: %ld\n", msg.mtype);
            printf("          Remitente: '%s', Sala: '%s', Texto: '%s'\n", 
                   msg.remitente, msg.sala, msg.texto);
        }
    }

    // Este punto nunca debería alcanzarse debido al bucle infinito
    // pero se incluye por completitud
    return 0;
}
