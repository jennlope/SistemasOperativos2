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

struct mensaje {
    long mtype;
    int reply_qid;
    char remitente[MAX_NOMBRE];
    char texto[MAX_TEXTO];
    char sala[MAX_NOMBRE];
};

int cola_global = -1;
int cola_privada = -1;
char nombre_usuario[MAX_NOMBRE];
char sala_actual[MAX_NOMBRE] = "";

void limpiar_y_salir(int signo) {
    if (cola_privada != -1) {
        msgctl(cola_privada, IPC_RMID, NULL);
    }
    printf("\nCliente %s: saliendo\n", nombre_usuario);
    exit(0);
}

void *recibir_mensajes(void *arg) {
    struct mensaje msg;
    while (1) {
        ssize_t r = msgrcv(cola_privada, &msg, sizeof(struct mensaje) - sizeof(long), 0, 0);
        if (r == -1) {
            if (errno == EINTR) continue;
            perror("msgrcv privado");
            continue;
        }

        if (msg.mtype == 2) { // RESP del servidor
            printf("[SERVIDOR] %s\n", msg.texto);
        } else if (msg.mtype == 4) { // CHAT
            // mostrar mensaje
            printf("%s: %s\n", msg.remitente, msg.texto);
        } else {
            // otros tipos
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
    signal(SIGINT, limpiar_y_salir);
    strcpy(nombre_usuario, argv[1]);

    // conectar a cola global
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

    // crear cola privada (IPC_PRIVATE -> cola accesible por qid devuelto)
    cola_privada = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (cola_privada == -1) {
        perror("Error al crear cola privada");
        exit(1);
    }

    printf("Bienvenid@, %s. Salas disponibles: General, Deportes\n", nombre_usuario);

    // hilo receptor
    pthread_t hilo;
    pthread_create(&hilo, NULL, recibir_mensajes, NULL);

    struct mensaje msg;
    char comando[MAX_TEXTO];

    while (1) {
        printf("> ");
        if (!fgets(comando, sizeof(comando), stdin)) break;
        comando[strcspn(comando, "\n")] = '\0';

        if (strncmp(comando, "join ", 5) == 0) {
            char sala[MAX_NOMBRE];
            sscanf(comando + 5, "%49s", sala);

            // enviar JOIN al servidor por cola global
            memset(&msg, 0, sizeof(msg));
            msg.mtype = 1; // JOIN
            msg.reply_qid = cola_privada;
            strncpy(msg.remitente, nombre_usuario, MAX_NOMBRE);
            strncpy(msg.sala, sala, MAX_NOMBRE);
            msg.texto[0] = '\0';
            if (msgsnd(cola_global, &msg, sizeof(struct mensaje) - sizeof(long), 0) == -1) {
                perror("Error al enviar JOIN");
                continue;
            }

            // esperar la respuesta (el hilo receptor la imprimirá)
            // Guardamos sala_actual solo si la respuesta fue exitosa:
            // Para simplificar, asumimos que la respuesta del servidor llegará pronto y usuario confía en ella.
            strncpy(sala_actual, sala, MAX_NOMBRE);

        } else if (strlen(comando) > 0) {
            // enviar mensaje a la sala actual por la cola global (servidor reenvía)
            if (strlen(sala_actual) == 0) {
                printf("No estás en ninguna sala. Usa 'join <sala>' para unirte.\n");
                continue;
            }
            memset(&msg, 0, sizeof(msg));
            msg.mtype = 3; // MSG
            msg.reply_qid = cola_privada; // por si el servidor necesita responder
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
