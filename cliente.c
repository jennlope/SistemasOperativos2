// cliente.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>

#define MAX_TEXTO 256
#define MAX_NOMBRE 50

// Estructura de mensaje compartida con el servidor.
// Se añadió 'reply_qid' para que el cliente le diga al servidor
// cuál es su cola privada y el servidor pueda responder directamente.
struct mensaje {
    long mtype;
    int reply_qid;                      // QID de la cola privada del cliente
    char remitente[MAX_NOMBRE];         // nombre del usuario remitente
    char texto[MAX_TEXTO];              // contenido del mensaje
    char sala[MAX_NOMBRE];              // sala objetivo (join / msg)
};

int cola_global = -1;   // QID de la cola global (servidor)
int cola_privada = -1;  // QID de la cola privada del cliente
char nombre_usuario[MAX_NOMBRE];
char sala_actual[MAX_NOMBRE] = "";

// Limpieza al terminar: eliminar la cola privada para evitar colas huérfanas.
void limpiar_y_salir(int signo) {
    if (cola_privada != -1) {
        msgctl(cola_privada, IPC_RMID, NULL);
    }
    printf("\nCliente %s: saliendo\n", nombre_usuario);
    exit(0);
}

// Hilo que recibe mensajes en la cola privada del cliente.
// El servidor enviará RESP (tipo 2) y CHAT (tipo 4) a esta cola.
void *recibir_mensajes(void *arg) {
    struct mensaje msg;
    while (1) {
        // msgrcv espera cualquier tipo (0) en la cola privada
        ssize_t r = msgrcv(cola_privada, &msg, sizeof(struct mensaje) - sizeof(long), 0, 0);
        if (r == -1) {
            if (errno == EINTR) continue; // reintentar si fue interrumpido por señal
            perror("msgrcv privado");
            continue;
        }

        if (msg.mtype == 2) { // RESP del servidor (confirmaciones / errores)
            printf("[SERVIDOR] %s\n", msg.texto);
        } else if (msg.mtype == 4) { // CHAT: mensaje enviado por otro usuario de la sala
            printf("%s: %s\n", msg.remitente, msg.texto);
        } else {
            // Otros tipos (por si se extiende el protocolo)
            printf("[TIPO %ld] %s\n", msg.mtype, msg.texto);
        }
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Uso: %s <nombre_usuario>\n", argv[0]);
        exit(1);
    }
    // Capturar Ctrl+C para limpiar recursos
    signal(SIGINT, limpiar_y_salir);
    strcpy(nombre_usuario, argv[1]);

    // Conectar a la cola global creada por el servidor.
    // Usamos la misma clave ftok("/tmp", 'A') que el servidor.
    key_t key_global = ftok("/tmp", 'A');
    if (key_global == (key_t)-1) {
        perror("ftok global cliente");
        exit(1);
    }
    cola_global = msgget(key_global, 0666);
    if (cola_global == -1) {
        perror("Error al conectar a la cola global (¿corre el servidor?)");
        exit(1);
    }

    // Crear una cola privada propia (IPC_PRIVATE) para recibir respuestas y chats.
    // El servidor recibirá el qid y podrá enviar mensajes directamente.
    cola_privada = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (cola_privada == -1) {
        perror("Error al crear cola privada");
        exit(1);
    }

    printf("Bienvenid@, %s. Salas disponibles: General, Deportes\n", nombre_usuario);

    // Iniciar hilo receptor que escucha en la cola privada
    pthread_t hilo;
    pthread_create(&hilo, NULL, recibir_mensajes, NULL);

    struct mensaje msg;
    char comando[MAX_TEXTO];

    while (1) {
        printf("> ");
        if (!fgets(comando, sizeof(comando), stdin)) break;
        comando[strcspn(comando, "\n")] = '\0';

        if (strncmp(comando, "join ", 5) == 0) {
            // Comando join: enviar solicitud al servidor informando sala y reply_qid
            char sala[MAX_NOMBRE];
            sscanf(comando + 5, "%49s", sala);

            memset(&msg, 0, sizeof(msg));
            msg.mtype = 1; // JOIN
            msg.reply_qid = cola_privada; // indicar al servidor dónde responder
            strncpy(msg.remitente, nombre_usuario, MAX_NOMBRE);
            strncpy(msg.sala, sala, MAX_NOMBRE);
            msg.texto[0] = '\0';
            if (msgsnd(cola_global, &msg, sizeof(struct mensaje) - sizeof(long), 0) == -1) {
                perror("Error al enviar JOIN");
                continue;
            }

            // Guardamos la sala localmente (optimista). La confirmación real llegará
            // por la cola privada y se imprimirá allí. Si quieres seguridad, esperar
            // explícitamente la RESP antes de asignar sala_actual.
            strncpy(sala_actual, sala, MAX_NOMBRE);

        } else if (strlen(comando) > 0) {
            // Enviar texto como mensaje a la sala actual por la cola global
            if (strlen(sala_actual) == 0) {
                printf("No estás en ninguna sala. Usa 'join <sala>' para unirte.\n");
                continue;
            }
            memset(&msg, 0, sizeof(msg));
            msg.mtype = 3; // MSG (mensaje de chat al servidor)
            msg.reply_qid = cola_privada; // por si el servidor necesita responder (errores)
            strncpy(msg.remitente, nombre_usuario, MAX_NOMBRE);
            strncpy(msg.sala, sala_actual, MAX_NOMBRE);
            strncpy(msg.texto, comando, MAX_TEXTO);

            if (msgsnd(cola_global, &msg, sizeof(struct mensaje) - sizeof(long), 0) == -1) {
                perror("Error al enviar mensaje");
                continue;
            }
        }
    }

    limpiar_y_salir(0);
    return 0;
}
