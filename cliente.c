/*
 * cliente.c - Cliente de Chat usando Colas de Mensajes System V
 * 
 * Este programa implementa un cliente de chat que se conecta al servidor
 * de chat multi-sala. Utiliza colas de mensajes System V para la comunicación
 * bidireccional con el servidor.
 * 
 * Funcionalidades:
 * - Conexión automática al servidor de chat
 * - Unión a salas de chat mediante comando 'join'
 * - Envío y recepción de mensajes en tiempo real
 * - Manejo multi-hilo para recepción asíncrona de mensajes
 * - Limpieza automática de recursos al terminar
 * 
 * Uso: ./cliente <nombre_usuario>
 * 
 * Comandos disponibles:
 * - join <sala>  : Unirse a una sala de chat
 * - <mensaje>    : Enviar mensaje a la sala actual
 * - Ctrl+C      : Salir del cliente
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>    // tipos de datos del sistema
#include <sys/ipc.h>      // comunicación entre procesos
#include <sys/msg.h>      // colas de mensajes
#include <unistd.h>       // funciones estándar de Unix
#include <pthread.h>      // hilos POSIX para recepción asíncrona
#include <signal.h>       // manejo de señales
#include <errno.h>        // códigos de error

/* ==================== CONSTANTES Y CONFIGURACIÓN ==================== */
#define MAX_TEXTO 256                   // Longitud máxima de un mensaje de texto
#define MAX_NOMBRE 50                   // Longitud máxima para nombres de usuario y salas

/* ==================== ESTRUCTURAS DE DATOS ==================== */

/**
 * Estructura de mensaje para comunicación cliente-servidor
 * Debe coincidir exactamente con la estructura del servidor para
 * garantizar compatibilidad en la comunicación.
 */
struct mensaje {
    long mtype;                     // Tipo de mensaje (1=JOIN, 2=RESP, 3=MSG, 4=CHAT)
    int reply_qid;                  // ID de cola privada del cliente (para respuestas)
    char remitente[MAX_NOMBRE];     // Nombre del usuario que envía el mensaje
    char texto[MAX_TEXTO];          // Contenido del mensaje o comando
    char sala[MAX_NOMBRE];          // Nombre de la sala objetivo
};

/* ==================== VARIABLES GLOBALES ==================== */
int cola_global = -1;               // ID de la cola global del servidor
int cola_privada = -1;              // ID de la cola privada del cliente
char nombre_usuario[MAX_NOMBRE];    // Nombre del usuario actual
char sala_actual[MAX_NOMBRE] = "";  // Sala en la que está conectado el usuario

/* ==================== FUNCIONES DE UTILIDAD ==================== */

/**
 * Función de limpieza llamada al terminar el cliente
 * 
 * Esta función se ejecuta cuando el cliente recibe SIGINT (Ctrl+C) o
 * al terminar normalmente. Se encarga de eliminar la cola privada
 * del cliente para evitar dejar recursos huérfanos en el sistema.
 * 
 * @param signo Número de la señal recibida (0 para terminación normal)
 */
void limpiar_y_salir(int signo) {
    // Eliminar cola privada si fue creada
    if (cola_privada != -1) {
        if (msgctl(cola_privada, IPC_RMID, NULL) == 0) {
            printf("\nCola privada eliminada correctamente\n");
        } else {
            fprintf(stderr, "\nError al eliminar cola privada: %s\n", strerror(errno));
        }
    }
    
    printf("Cliente '%s': desconectado del servidor\n", nombre_usuario);
    printf("¡Hasta luego!\n");
    exit(0);
}

/**
 * Hilo de recepción de mensajes (función ejecutada en hilo separado)
 * 
 * Esta función se ejecuta continuamente en un hilo separado y se encarga
 * de recibir todos los mensajes que el servidor envía a la cola privada
 * del cliente. Maneja tanto respuestas del servidor como mensajes de
 * otros usuarios en la sala de chat.
 * 
 * @param arg Argumento del hilo (no utilizado)
 * @return NULL (requerido por pthread)
 */
void *recibir_mensajes(void *arg) {
    struct mensaje msg;
    
    while (1) {
        // Esperar cualquier mensaje en la cola privada del cliente
        // El parámetro msgtyp=0 significa "aceptar cualquier tipo de mensaje"
        ssize_t r = msgrcv(cola_privada, &msg, sizeof(struct mensaje) - sizeof(long), 0, 0);
        
        // Manejar errores de recepción
        if (r == -1) {
            if (errno == EINTR) {
                // Interrupción por señal (normal), continuar esperando
                continue;
            }
            perror("Error recibiendo mensaje en cola privada");
            continue;
        }

        // Procesar mensaje según su tipo
        if (msg.mtype == 2) {
            // RESP: Respuesta del servidor (confirmaciones, errores, notificaciones)
            printf("\n[SERVIDOR] %s\n> ", msg.texto);
            fflush(stdout);  // Forzar salida inmediata para mantener prompt
            
        } else if (msg.mtype == 4) {
            // CHAT: Mensaje de chat enviado por otro usuario de la sala
            printf("\n[%s] %s: %s\n> ", msg.sala, msg.remitente, msg.texto);
            fflush(stdout);  // Forzar salida inmediata para mantener prompt
            
        } else {
            // Tipos de mensaje desconocidos (para extensiones futuras)
            printf("\n[MENSAJE TIPO %ld] De '%s': %s\n> ", 
                   msg.mtype, msg.remitente, msg.texto);
            fflush(stdout);
        }
    }
    
    return NULL;  // Nunca se alcanza debido al bucle infinito
}

/* ==================== FUNCIÓN PRINCIPAL ==================== */

/**
 * Función principal del cliente de chat
 * 
 * Inicializa la conexión con el servidor, crea la cola privada,
 * configura el hilo de recepción y maneja la interfaz de usuario
 * para envío de comandos y mensajes.
 */
int main(int argc, char *argv[]) {
    /* Validación de argumentos de línea de comandos */
    if (argc != 2) {
        printf("Uso: %s <nombre_usuario>\n", argv[0]);
        printf("Ejemplo: %s Juan\n", argv[0]);
        exit(1);
    }
    
    /* Configuración inicial del cliente */
    
    // Instalar manejador de señal para limpieza automática
    signal(SIGINT, limpiar_y_salir);
    
    // Guardar nombre de usuario del argumento de línea de comandos
    strncpy(nombre_usuario, argv[1], MAX_NOMBRE - 1);
    nombre_usuario[MAX_NOMBRE - 1] = '\0';  // Asegurar terminación nula

    /* Conexión a la cola global del servidor */
    
    // Generar la misma clave que usa el servidor para la cola global
    key_t key_global = ftok("/tmp", 'A');
    if (key_global == (key_t)-1) {
        perror("Error generando clave para cola global");
        exit(1);
    }
    
    // Conectar a la cola global existente (creada por el servidor)
    // Solo permisos de acceso, no creación (sin IPC_CREAT)
    cola_global = msgget(key_global, 0666);
    if (cola_global == -1) {
        fprintf(stderr, "Error: No se puede conectar al servidor.\n");
        fprintf(stderr, "¿Está el servidor ejecutándose?\n");
        exit(1);
    }

    /* Creación de cola privada del cliente */
    
    // Crear cola privada única para este cliente usando IPC_PRIVATE
    // Esta cola recibirá respuestas del servidor y mensajes de otros usuarios
    cola_privada = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (cola_privada == -1) {
        perror("Error creando cola privada del cliente");
        exit(1);
    }

    /* Información de bienvenida */
    printf("\n=== Cliente de Chat ===\n");
    printf("Bienvenid@, %s!\n", nombre_usuario);
    printf("Conectado al servidor (Cola Global: %d, Cola Privada: %d)\n", 
           cola_global, cola_privada);
    printf("\nComandos disponibles:\n");
    printf("  join <sala>  - Unirse a una sala de chat\n");
    printf("  <mensaje>    - Enviar mensaje a la sala actual\n");
    printf("  Ctrl+C       - Salir del cliente\n");
    printf("\nSalas sugeridas: General, Deportes, Tecnologia\n");
    printf("======================\n\n");

    /* Inicialización del hilo de recepción */
    
    // Crear hilo separado para recibir mensajes asíncronamente
    // Esto permite que el usuario siga escribiendo mientras recibe mensajes
    pthread_t hilo_receptor;
    if (pthread_create(&hilo_receptor, NULL, recibir_mensajes, NULL) != 0) {
        perror("Error creando hilo de recepción");
        limpiar_y_salir(1);
    }

    /* Bucle principal de interfaz de usuario */
    
    struct mensaje msg;
    char comando[MAX_TEXTO];

    while (1) {
        // Mostrar prompt y esperar entrada del usuario
        printf("> ");
        fflush(stdout);
        
        // Leer línea completa de entrada del usuario
        if (!fgets(comando, sizeof(comando), stdin)) {
            // EOF o error en entrada, terminar cliente
            printf("\nEntrada terminada, cerrando cliente...\n");
            break;
        }
        
        // Eliminar salto de línea del final
        comando[strcspn(comando, "\n")] = '\0';
        
        // Ignorar comandos vacíos
        if (strlen(comando) == 0) {
            continue;
        }

        /* Procesamiento del comando JOIN */
        if (strncmp(comando, "join ", 5) == 0) {
            // Extraer nombre de sala del comando
            char sala[MAX_NOMBRE];
            int items_leidos = sscanf(comando + 5, "%49s", sala);
            
            if (items_leidos != 1 || strlen(sala) == 0) {
                printf("Error: Especifica el nombre de la sala. Ejemplo: join General\n");
                continue;
            }

            printf("Intentando unirse a la sala '%s'...\n", sala);
            
            // Preparar mensaje JOIN para el servidor
            memset(&msg, 0, sizeof(msg));
            msg.mtype = 1;  // Tipo JOIN
            msg.reply_qid = cola_privada;  // Indicar dónde puede responder el servidor
            
            // Copiar datos del cliente y sala, asegurando terminación nula
            strncpy(msg.remitente, nombre_usuario, MAX_NOMBRE - 1);
            msg.remitente[MAX_NOMBRE - 1] = '\0';
            strncpy(msg.sala, sala, MAX_NOMBRE - 1);
            msg.sala[MAX_NOMBRE - 1] = '\0';
            msg.texto[0] = '\0';  // No hay texto en mensaje JOIN
            
            // Enviar solicitud JOIN al servidor
            if (msgsnd(cola_global, &msg, sizeof(struct mensaje) - sizeof(long), 0) == -1) {
                perror("Error enviando solicitud JOIN");
                continue;
            }

            // Actualizar sala actual (optimista - la confirmación llegará por el hilo receptor)
            strncpy(sala_actual, sala, MAX_NOMBRE - 1);
            sala_actual[MAX_NOMBRE - 1] = '\0';
            
        } else {
            /* Procesamiento de mensaje de chat normal */
            
            // Verificar que el usuario esté en una sala
            if (strlen(sala_actual) == 0) {
                printf("Error: No estás en ninguna sala.\n");
                printf("Usa 'join <nombre_sala>' para unirte a una sala primero.\n");
                continue;
            }
            
            // Preparar mensaje de chat para enviar a la sala
            memset(&msg, 0, sizeof(msg));
            msg.mtype = 3;  // Tipo MSG (mensaje de chat)
            msg.reply_qid = cola_privada;  // Para posibles respuestas de error del servidor
            
            // Copiar datos del mensaje, asegurando terminación nula
            strncpy(msg.remitente, nombre_usuario, MAX_NOMBRE - 1);
            msg.remitente[MAX_NOMBRE - 1] = '\0';
            strncpy(msg.sala, sala_actual, MAX_NOMBRE - 1);
            msg.sala[MAX_NOMBRE - 1] = '\0';
            strncpy(msg.texto, comando, MAX_TEXTO - 1);
            msg.texto[MAX_TEXTO - 1] = '\0';

            // Enviar mensaje al servidor para distribución en la sala
            if (msgsnd(cola_global, &msg, sizeof(struct mensaje) - sizeof(long), 0) == -1) {
                perror("Error enviando mensaje de chat");
                continue;
            }
        }
    }

    // Salida normal del bucle principal
    limpiar_y_salir(0);
    return 0;
}