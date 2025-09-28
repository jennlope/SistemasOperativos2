/*
 * cliente.c - Cliente de Chat Multi-Sala con Comandos Avanzados
 * 
 * Este programa implementa un cliente completo de chat que se conecta al servidor
 * de chat multi-sala. Utiliza colas de mensajes System V para la comunicación
 * bidireccional con el servidor y soporta múltiples comandos avanzados.
 * 
 * Funcionalidades principales:
 * - Conexión automática al servidor de chat
 * - Unión y abandono de salas de chat
 * - Envío y recepción de mensajes en tiempo real
 * - Listado de salas disponibles
 * - Visualización de usuarios en sala actual
 * - Manejo multi-hilo para recepción asíncrona
 * - Limpieza automática de recursos
 * 
 * Uso: ./cliente <nombre_usuario>
 * 
 * Comandos disponibles:
 * - join <sala>    : Unirse a una sala de chat
 * - /leave         : Abandonar la sala actual
 * - /list          : Mostrar todas las salas disponibles
 * - /users         : Mostrar usuarios en la sala actual
 * - <mensaje>      : Enviar mensaje a la sala actual
 * - Ctrl+C         : Salir del cliente
 */

#include <stdio.h>        // entrada/salida estándar
#include <stdlib.h>       // funciones de utilidad general
#include <string.h>       // manipulación de strings
#include <sys/types.h>    // tipos de datos del sistema
#include <sys/ipc.h>      // comunicación entre procesos
#include <sys/msg.h>      // colas de mensajes System V
#include <unistd.h>       // funciones estándar de Unix
#include <pthread.h>      // hilos POSIX para recepción asíncrona
#include <signal.h>       // manejo de señales del sistema
#include <errno.h>        // códigos de error del sistema

/* ==================== CONSTANTES Y CONFIGURACIÓN ==================== */
#define MAX_TEXTO 256                   // Longitud máxima de un mensaje de texto
#define MAX_NOMBRE 50                   // Longitud máxima para nombres de usuario y salas

/* ==================== ESTRUCTURAS DE DATOS ==================== */

/**
 * Estructura de mensaje para comunicación cliente-servidor
 * 
 * Esta estructura debe coincidir exactamente con la del servidor para
 * garantizar compatibilidad en la comunicación mediante colas de mensajes.
 * 
 * Tipos de mensaje soportados:
 * - mtype 1 (JOIN): Solicitud para unirse a una sala
 * - mtype 2 (RESP): Respuesta del servidor (confirmaciones/errores)
 * - mtype 3 (MSG):  Mensaje de chat para distribuir en sala
 * - mtype 4 (CHAT): Mensaje distribuido por el servidor
 * - mtype 5 (LEAVE): Solicitud para abandonar sala actual
 * - mtype 6 (USERS): Solicitud de lista de usuarios en sala
 * - mtype 7 (LIST): Solicitud de lista de salas disponibles
 */
struct mensaje {
    long mtype;                     // Tipo de mensaje (ver descripción arriba)
    int reply_qid;                  // ID de cola privada del cliente (para respuestas)
    char remitente[MAX_NOMBRE];     // Nombre del usuario que envía el mensaje
    char texto[MAX_TEXTO];          // Contenido del mensaje de chat
    char sala[MAX_NOMBRE];          // Nombre de la sala objetivo/actual
};

/* ==================== VARIABLES GLOBALES ==================== */
int cola_global = -1;               // ID de la cola global del servidor
int cola_privada = -1;              // ID de la cola privada de este cliente
char nombre_usuario[MAX_NOMBRE];    // Nombre del usuario actual (del argumento de línea de comandos)
char sala_actual[MAX_NOMBRE] = "";  // Nombre de la sala en la que está conectado el usuario

/* ==================== FUNCIONES DE UTILIDAD ==================== */

/**
 * Función de limpieza y terminación del cliente
 * 
 * Esta función se ejecuta cuando el cliente recibe SIGINT (Ctrl+C) o
 * cuando termina normalmente. Se encarga de limpiar los recursos del
 * sistema (cola privada) para evitar dejar recursos huérfanos.
 * 
 * @param signo Número de la señal recibida (0 para terminación normal)
 */
void limpiar_y_salir(int signo) {
    // Eliminar cola privada si fue creada exitosamente
    if (cola_privada != -1) {
        msgctl(cola_privada, IPC_RMID, NULL);
    }
    
    printf("\nCliente %s: desconectado del servidor\n", nombre_usuario);
    printf("¡Hasta luego!\n");
    exit(0);
}

/**
 * Hilo de recepción de mensajes (ejecutado en hilo separado)
 * 
 * Esta función se ejecuta continuamente en un hilo independiente y
 * se encarga de recibir todos los mensajes que el servidor envía a
 * la cola privada del cliente. Procesa diferentes tipos de mensajes:
 * 
 * - RESP (2): Respuestas y notificaciones del servidor
 * - CHAT (4): Mensajes de otros usuarios en la sala
 * - Otros tipos: Mensajes especiales o de extensiones futuras
 * 
 * @param arg Argumento del hilo (no utilizado)
 * @return NULL (requerido por especificación pthread)
 */
void *recibir_mensajes(void *arg) {
    struct mensaje msg;
    
    while (1) {
        // Esperar cualquier mensaje en la cola privada del cliente
        // Parámetro msgtyp=0 significa "aceptar cualquier tipo de mensaje"
        ssize_t r = msgrcv(cola_privada, &msg, sizeof(msg) - sizeof(long), 0, 0);
        
        // Manejo de errores en la recepción
        if (r == -1) {
            if (errno == EINTR) {
                // Interrupción por señal (comportamiento normal), continuar
                continue;
            }
            perror("Error al recibir mensaje en cola privada");
            continue;
        }

        // Procesar mensaje según su tipo
        if (msg.mtype == 2) {
            // RESP: Respuesta del servidor (confirmaciones, errores, listas, etc.)
            printf("[SERVIDOR] %s\n", msg.texto);
        } else if (msg.mtype == 4) {
            // CHAT: Mensaje de chat enviado por otro usuario de la sala
            printf("%s: %s\n", msg.remitente, msg.texto);
        } else {
            // Tipos de mensaje desconocidos o especiales
            printf("[MENSAJE TIPO %ld] %s\n", msg.mtype, msg.texto);
        }
        
        // Mostrar prompt nuevamente para mantener interfaz interactiva
        printf("> ");
        fflush(stdout);
    }
    
    return NULL;  // Nunca se alcanza debido al bucle infinito
}

/* ==================== FUNCIÓN PRINCIPAL ==================== */

/**
 * Función principal del cliente de chat
 * 
 * Inicializa todas las conexiones necesarias, configura el hilo de recepción
 * y maneja el bucle principal de interfaz de usuario para procesar comandos
 * y mensajes del usuario.
 * 
 * @param argc Número de argumentos de línea de comandos
 * @param argv Array de argumentos (debe incluir nombre de usuario)
 * @return 0 en terminación exitosa
 */
int main(int argc, char *argv[]) {
    /* Validación de argumentos de entrada */
    if (argc != 2) {
        printf("Uso: %s <nombre_usuario>\n", argv[0]);
        printf("Ejemplo: %s Juan\n", argv[0]);
        exit(1);
    }
    
    /* Configuración inicial del sistema */
    
    // Instalar manejador de señal para limpieza automática con Ctrl+C
    signal(SIGINT, limpiar_y_salir);
    
    // Copiar nombre de usuario desde argumentos de línea de comandos
    strcpy(nombre_usuario, argv[1]);

    /* Establecer conexión con el servidor */
    
    // Generar clave para cola global (debe coincidir con la del servidor)
    key_t key_global = ftok("/tmp", 'A');
    if (key_global == (key_t)-1) { 
        perror("Error generando clave para cola global"); 
        exit(1); 
    }
    
    // Conectar a la cola global existente (creada por el servidor)
    cola_global = msgget(key_global, 0666);
    if (cola_global == -1) { 
        fprintf(stderr, "Error: No se puede conectar al servidor.\n");
        fprintf(stderr, "¿Está el servidor ejecutándose?\n");
        exit(1); 
    }

    /* Crear cola privada para recibir mensajes del servidor */
    
    // Crear cola privada única usando IPC_PRIVATE
    cola_privada = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (cola_privada == -1) { 
        perror("Error creando cola privada del cliente"); 
        exit(1); 
    }

    /* Mostrar información de bienvenida */
    printf("\n=== Cliente de Chat Multi-Sala ===\n");
    printf("Bienvenid@ %s!\n", nombre_usuario);
    printf("Conectado al servidor (Global: %d, Privada: %d)\n", cola_global, cola_privada);
    printf("\nComandos disponibles:\n");
    printf("  join <sala>  - Unirse a una sala\n");
    printf("  /leave       - Abandonar sala actual\n");
    printf("  /list        - Ver salas disponibles\n");
    printf("  /users       - Ver usuarios en sala\n");
    printf("  <mensaje>    - Enviar mensaje\n");
    printf("==============================\n\n");

    /* Inicializar hilo de recepción de mensajes */
    
    // Crear hilo separado para recibir mensajes asíncronamente del servidor
    // Esto permite al usuario escribir comandos mientras recibe mensajes
    pthread_t hilo_receptor;
    if (pthread_create(&hilo_receptor, NULL, recibir_mensajes, NULL) != 0) {
        perror("Error creando hilo de recepción");
        limpiar_y_salir(1);
    }

    /* Variables para el bucle principal de comandos */
    struct mensaje msg;
    char comando[MAX_TEXTO];

    /* Bucle principal de interfaz de usuario */
    while (1) {
        // Mostrar prompt y esperar entrada del usuario
        printf("> ");
        fflush(stdout);
        
        // Leer comando completo del usuario
        if (!fgets(comando, sizeof(comando), stdin)) {
            // EOF detectado (Ctrl+D), terminar cliente
            printf("\nTerminando cliente...\n");
            break;
        }
        
        // Eliminar salto de línea del final del comando
        comando[strcspn(comando, "\n")] = '\0';
        
        // Ignorar líneas vacías
        if (strlen(comando) == 0) {
            continue;
        }

        /* ===== PROCESAMIENTO DE COMANDO JOIN ===== */
        if (strncmp(comando, "join ", 5) == 0) {
            // Extraer nombre de sala del comando
            char sala[MAX_NOMBRE];
            int items_leidos = sscanf(comando + 5, "%49s", sala);
            
            // Validar que se especificó un nombre de sala
            if (items_leidos != 1 || strlen(sala) == 0) {
                printf("Error: Especifica el nombre de la sala.\n");
                printf("Uso: join <nombre_sala>\n");
                continue;
            }

            // Preparar mensaje JOIN para el servidor
            memset(&msg, 0, sizeof(msg));
            msg.mtype = 1;                                    // Tipo JOIN
            msg.reply_qid = cola_privada;                     // Para recibir respuesta
            strncpy(msg.remitente, nombre_usuario, MAX_NOMBRE - 1);
            msg.remitente[MAX_NOMBRE - 1] = '\0';             // Asegurar terminación nula
            strncpy(msg.sala, sala, MAX_NOMBRE - 1);
            msg.sala[MAX_NOMBRE - 1] = '\0';                  // Asegurar terminación nula
            
            // Enviar solicitud al servidor
            if (msgsnd(cola_global, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("Error enviando solicitud JOIN");
                continue;
            }
            
            // Actualizar sala actual (optimista - confirmación llegará por hilo receptor)
            strncpy(sala_actual, sala, MAX_NOMBRE - 1);
            sala_actual[MAX_NOMBRE - 1] = '\0';
            printf("Solicitando unión a sala '%s'...\n", sala);

        } else if (strncmp(comando, "/leave", 6) == 0) {
            /* ===== PROCESAMIENTO DE COMANDO /LEAVE ===== */
            
            // Verificar que el usuario esté en una sala
            if (strlen(sala_actual) == 0) {
                printf("Error: No estás en ninguna sala para abandonar.\n");
                continue;
            }
            
            // Preparar mensaje LEAVE para el servidor
            memset(&msg, 0, sizeof(msg));
            msg.mtype = 5;                                    // Tipo LEAVE
            msg.reply_qid = cola_privada;                     // Para recibir confirmación
            strncpy(msg.remitente, nombre_usuario, MAX_NOMBRE - 1);
            msg.remitente[MAX_NOMBRE - 1] = '\0';
            strncpy(msg.sala, sala_actual, MAX_NOMBRE - 1);
            msg.sala[MAX_NOMBRE - 1] = '\0';
            
            // Enviar solicitud de abandono al servidor
            if (msgsnd(cola_global, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("Error enviando solicitud LEAVE");
                continue;
            }
            
            // Limpiar sala actual localmente
            printf("Abandonando sala '%s'...\n", sala_actual);
            sala_actual[0] = '\0';

        } else if (strncmp(comando, "/list", 5) == 0) {
            /* ===== PROCESAMIENTO DE COMANDO /LIST ===== */
            
            // Preparar solicitud de lista de salas disponibles
            memset(&msg, 0, sizeof(msg));
            msg.mtype = 7;                                    // Tipo LIST
            msg.reply_qid = cola_privada;                     // Para recibir la lista
            
            // Enviar solicitud al servidor
            if (msgsnd(cola_global, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("Error enviando solicitud LIST");
                continue;
            }
            
            printf("Solicitando lista de salas disponibles...\n");

        } else if (strncmp(comando, "/users", 6) == 0) {
            /* ===== PROCESAMIENTO DE COMANDO /USERS ===== */
            
            // Verificar que el usuario esté en una sala
            if (strlen(sala_actual) == 0) {
                printf("Error: Debes estar en una sala para ver sus usuarios.\n");
                printf("Usa 'join <sala>' para unirte a una sala primero.\n");
                continue;
            }
            
            // Preparar solicitud de lista de usuarios en sala actual
            memset(&msg, 0, sizeof(msg));
            msg.mtype = 6;                                    // Tipo USERS
            msg.reply_qid = cola_privada;                     // Para recibir la lista
            strncpy(msg.sala, sala_actual, MAX_NOMBRE - 1);
            msg.sala[MAX_NOMBRE - 1] = '\0';
            
            // Enviar solicitud al servidor
            if (msgsnd(cola_global, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("Error enviando solicitud USERS");
                continue;
            }
            
            printf("Solicitando lista de usuarios en sala '%s'...\n", sala_actual);

        } else if (strlen(comando) > 0) {
            /* ===== PROCESAMIENTO DE MENSAJE DE CHAT REGULAR ===== */
            
            // Verificar que el usuario esté en una sala para enviar mensajes
            if (strlen(sala_actual) == 0) {
                printf("Error: Debes estar en una sala para enviar mensajes.\n");
                printf("Usa 'join <sala>' para unirte a una sala primero.\n");
                continue;
            }
            
            // Preparar mensaje de chat para distribuir en la sala
            memset(&msg, 0, sizeof(msg));
            msg.mtype = 3;                                    // Tipo MSG (mensaje de chat)
            msg.reply_qid = cola_privada;                     // Para posibles respuestas de error
            strncpy(msg.remitente, nombre_usuario, MAX_NOMBRE - 1);
            msg.remitente[MAX_NOMBRE - 1] = '\0';
            strncpy(msg.sala, sala_actual, MAX_NOMBRE - 1);
            msg.sala[MAX_NOMBRE - 1] = '\0';
            strncpy(msg.texto, comando, MAX_TEXTO - 1);
            msg.texto[MAX_TEXTO - 1] = '\0';
            
            // Enviar mensaje al servidor para distribución
            if (msgsnd(cola_global, &msg, sizeof(msg) - sizeof(long), 0) == -1) {
                perror("Error enviando mensaje de chat");
                continue;
            }
            
            // Nota: El mensaje se distribuirá a otros usuarios, pero no al remitente
        }
    }

    // Terminación normal del programa
    limpiar_y_salir(0);
    return 0;
}
