# Sistemas Operativos 2 -- Reto 1

Este proyecto implementa un **servidor** y un **cliente** que se
comunican mediante **colas de mensajes (System V Message Queues)** en C,
como parte del curso **Sistemas Operativos 2**.

------------------------------------------------------------------------

## Estructura del proyecto

    SO2/
    ├── cliente.c        # Implementación del cliente
    ├── servidor.c       # Implementación del servidor
    ├── Makefile         # Reglas de compilación
    ├── .gitignore       # Exclusión de archivos innecesarios
    └── README.md        # Documentación del proyecto

------------------------------------------------------------------------

## Flujo de ejecución

1.  **Compilación**

    ``` bash
    make
    ```

    Esto generará los binarios `cliente` y `servidor`.

2.  **Ejecución del servidor**

    ``` bash
    ./servidor
    ```

    El servidor queda a la espera de mensajes entrantes.

3.  **Ejecución del cliente**

    ``` bash
    ./cliente "mensaje de prueba"
    ```

    El cliente envía el mensaje al servidor mediante la cola de
    mensajes.

4.  **Respuesta**

    -   El servidor procesa y muestra el mensaje recibido.
    -   Dependiendo de la lógica, puede responder al cliente
        (extensible).

------------------------------------------------------------------------

## Ejemplo de uso

En dos terminales diferentes:

**Terminal 1 -- Servidor**

``` bash
./servidor
Esperando mensajes...
Recibido: "Hola servidor"
```

**Terminal 2 -- Cliente**

``` bash
./cliente "Hola servidor"
Mensaje enviado
```

------------------------------------------------------------------------

## Cambios respecto al código base

1.  **Separación de responsabilidades**
    -   Se crearon dos archivos: `cliente.c` y `servidor.c`.\
    -   Antes, la base tenía toda la lógica en un solo lugar; ahora está
        dividido para mayor claridad.
2.  **Makefile**
    -   Se agregó un `Makefile` para simplificar la compilación (`make`,
        `make clean`).
3.  **Validación de argumentos**
    -   El cliente ahora verifica que el usuario pase un mensaje como
        argumento.\
    -   En caso contrario, muestra un mensaje de uso correcto.
4.  **Manejo de errores robusto**
    -   Se añadieron verificaciones de retorno (`msgget`, `msgsnd`,
        `msgrcv`).\
    -   Si ocurre un error, se muestra `perror` y se termina la
        ejecución limpiamente.
5.  **Limpieza de recursos**
    -   El servidor libera la cola de mensajes al finalizar (con
        `msgctl(IPC_RMID)`).\
    -   Esto evita fugas de recursos en el sistema.
6.  **Compatibilidad y estilo**
    -   Código documentado con comentarios claros.\
    -   Uso de constantes y estructuras definidas para mejorar
        legibilidad.

------------------------------------------------------------------------

## Próximos pasos

-   Agregar manejo de múltiples clientes concurrentes.
-   Implementar respuestas del servidor al cliente.
-   Extender protocolo de comunicación (códigos de operación).

------------------------------------------------------------------------

## Autor
Jennifer Andrea Lopez Gomez
Santiago Alexander Cardenas Laverde