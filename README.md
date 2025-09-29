# Sistema de Chat Multi-Sala Avanzado - Sistemas Operativos 2

Este proyecto implementa un **sistema completo de chat multi-sala** con **servidor centralizado** y **múltiples clientes** que se comunican mediante **colas de mensajes System V** en C. Incluye funcionalidades avanzadas como historial persistente, comandos administrativos y gestión dinámica de usuarios.

## Características Principales

- **Salas de chat múltiples** - Creación automática y gestión dinámica
- **Múltiples usuarios** - Hasta 20 usuarios por sala, 10 salas simultáneas  
- **Comunicación en tiempo real** - Mensajes instantáneos bidireccionales
- **Multihilo** - Recepción asíncrona en cliente con pthread
- **Historial persistente** - Archivos automáticos por sala (.txt)
- **Comandos avanzados** - join, /leave, /list, /users
- **Gestión robusta** - Limpieza automática de recursos System V
- **Monitoreo** - Logs detallados y información de estado

------------------------------------------------------------------------

## Estructura del Proyecto

```
SistemasOperativos2/
├── servidor.c       # Servidor multi-sala con historial (completamente comentado)
├── cliente.c        # Cliente con comandos avanzados (completamente comentado)
├── Makefile         # Compilación automática optimizada
├── README.md        # Esta documentación completa
├── .gitignore       # Control de archivos temporales
└── *.txt           # Archivos de historial generados automáticamente
```

------------------------------------------------------------------------

## Instalación y Ejecución

### 1. **Compilación**
```bash
make
```
Genera los ejecutables `servidor` y `cliente` optimizados.

### 2. **Iniciar el Servidor**
```bash
./servidor
```
El servidor muestra información detallada al iniciar:
```
=== SERVIDOR DE CHAT MULTI-SALA ===
Servidor iniciado correctamente
Cola global ID: 32768
Capacidad: 10 salas, 20 usuarios por sala
Esperando conexiones de clientes...
Presiona Ctrl+C para terminar el servidor
=====================================
```

### 3. **Conectar Clientes**
En terminales separadas, ejecutar múltiples clientes:
```bash
./cliente Juan
./cliente Maria  
./cliente Pedro
```

Cada cliente muestra una interfaz completa:
```
=== Cliente de Chat Multi-Sala ===
Bienvenid@, Juan!
Conectado al servidor (Global: 32768, Privada: 32769)

Comandos disponibles:
  join <sala>  - Unirse a una sala
  /leave       - Abandonar sala actual
  /list        - Ver salas disponibles
  /users       - Ver usuarios en sala
  <mensaje>    - Enviar mensaje
==============================

> 
```

------------------------------------------------------------------------

## Guía de Comandos Avanzados

### **Comandos Disponibles:**

| Comando | Descripción | Ejemplo | Tipo de Mensaje |
|---------|-------------|---------|------------------|
| `join <sala>` | Unirse a una sala (crea si no existe) | `join General` | **1 (JOIN)** |
| `/leave` | Abandonar la sala actual | `/leave` | **5 (LEAVE)** |
| `/list` | Ver todas las salas disponibles | `/list` | **7 (LIST)** |
| `/users` | Ver usuarios en la sala actual | `/users` | **6 (USERS)** |
| `<mensaje>` | Enviar mensaje a la sala | `Hola a todos!` | **3 (MSG)** |
| `Ctrl+C` | Salir del cliente/servidor | - | **Señal** |

### **Flujo de Uso Típico:**
1. **Conectarse:** `./cliente TuNombre`
2. **Ver salas:** `/list`
3. **Unirse:** `join General` 
4. **Ver usuarios:** `/users`
5. **Chatear:** `¡Hola a todos!`
6. **Cambiar sala:** `join Deportes`
7. **Salir de sala:** `/leave`

------------------------------------------------------------------------

## Ejemplo de Sesión Completa

### **Terminal 1 - Servidor:**
```bash
$ ./servidor
=== SERVIDOR DE CHAT MULTI-SALA ===
Servidor iniciado correctamente
Cola global ID: 32768
Capacidad: 10 salas, 20 usuarios por sala
Esperando conexiones de clientes...

[JOIN] Usuario 'Juan' solicita unirse a sala 'General'
[SERVIDOR] Sala creada: 'General' (ID=32769, Índice=0)
[SERVIDOR] Usuario 'Juan' agregado a sala 'General' (1/20 usuarios)

[JOIN] Usuario 'Maria' solicita unirse a sala 'General'
[SERVIDOR] Usuario 'Maria' agregado a sala 'General' (2/20 usuarios)

[MSG] Usuario 'Juan' en sala 'General': Hola Maria!
[DISTRIBUCIÓN] Sala 'General': 'Juan' dice: Hola Maria! (enviando a 1 usuarios)

[LIST] Solicitud de lista de salas disponibles
[USERS] Solicitud de lista de usuarios en sala 'General'
```

### **Terminal 2 - Cliente Juan:**
```bash
$ ./cliente Juan
=== Cliente de Chat Multi-Sala ===
Bienvenid@, Juan!
Conectado al servidor (Global: 32768, Privada: 32769)
...
> join General
Solicitando unión a sala 'General'...

[SERVIDOR] Te has unido exitosamente a la sala: General
> /users
Solicitando lista de usuarios en sala 'General'...

[SERVIDOR] Usuarios en sala: Juan, Maria (2/20 usuarios)
> Hola Maria!

Maria: ¡Hola Juan! ¿Cómo estás?
> /list
Solicitando lista de salas disponibles...

[SERVIDOR] Salas disponibles: General(2), Deportes(1)
> 
```

### **Terminal 3 - Cliente Maria:**
```bash
$ ./cliente Maria
=== Cliente de Chat Multi-Sala ===
Bienvenid@, Maria!
...
> join General
Solicitando unión a sala 'General'...

[SERVIDOR] Te has unido exitosamente a la sala: General

Juan: Hola Maria!
> ¡Hola Juan! ¿Cómo estás?

Juan: Muy bien, gracias por preguntar
> join Deportes
Solicitando unión a sala 'Deportes'...

[SERVIDOR] Te has unido exitosamente a la sala: Deportes
> /leave
Abandonando sala 'Deportes'...

[SERVIDOR] Has abandonado la sala: Deportes
> 
```

------------------------------------------------------------------------

## Arquitectura del Sistema

### **Protocolo de Comunicación Avanzado:**

| Tipo | Nombre | Dirección | Descripción | Implementado |
|------|--------|-----------|-------------|--------------|
| `1` | **JOIN** | Cliente → Servidor | Solicitud para unirse a una sala | |
| `2` | **RESP** | Servidor → Cliente | Respuestas y notificaciones del servidor | |
| `3` | **MSG** | Cliente → Servidor | Mensaje de chat a distribuir en sala | |
| `4` | **CHAT** | Servidor → Cliente | Mensaje distribuido a usuarios de sala | |
| `5` | **LEAVE** | Cliente → Servidor | Abandonar sala actual | |
| `6` | **USERS** | Cliente → Servidor | Solicitar lista de usuarios en sala | |
| `7` | **LIST** | Cliente → Servidor | Solicitar lista de salas disponibles | |

### **Componentes del Sistema:**

#### **Servidor (`servidor.c`)**
- **Cola Global**: Recibe todas las solicitudes de clientes (ftok "/tmp" 'A')
- **Gestión de Salas**: Crea y administra hasta 10 salas simultáneas
- **Distribución de Mensajes**: Envía a colas privadas de usuarios
- **Historial Persistente**: Guarda mensajes en archivos `<sala>.txt`
- **Comandos Administrativos**: Lista de salas y usuarios
- **Limpieza Automática**: Elimina colas System V al terminar

#### **Cliente (`cliente.c`)**
- **Cola Privada**: Recibe respuestas del servidor y mensajes (IPC_PRIVATE)
- **Interfaz de Usuario**: Comandos intuitivos y feedback en tiempo real
- **Multihilo**: Hilo separado para recepción asíncrona de mensajes
- **Gestión de Estado**: Mantiene sala actual y conexión al servidor
- **Comandos Avanzados**: join, /leave, /list, /users + mensajes

### **Flujo de Datos:**
1. **Cliente** envía mensaje (JOIN/MSG/LEAVE/LIST/USERS) a **Cola Global**
2. **Servidor** procesa mensaje y actualiza estructuras internas
3. **Servidor** responde con RESP y/o distribuye CHAT a **Colas Privadas**
4. **Clientes** reciben mensajes asíncronamente en hilo receptor
5. **Historial** se guarda automáticamente en archivos por sala

------------------------------------------------------------------------

## Detalles Técnicos Avanzados

### **Límites del Sistema:**
- **Salas máximas:** 10 simultáneas (configurable con MAX_SALAS)
- **Usuarios por sala:** 20 máximo (configurable con MAX_USUARIOS_POR_SALA)
- **Longitud de mensaje:** 256 caracteres (MAX_TEXTO)
- **Longitud de nombres:** 50 caracteres (MAX_NOMBRE)

### **Tecnologías Utilizadas:**
- **IPC System V Message Queues** - Comunicación entre procesos
- **POSIX Threads (pthread)** - Recepción asíncrona en cliente
- **Signal Handling** - Limpieza automática con Ctrl+C (SIGINT/SIGTERM)
- **ftok()** - Generación de claves únicas para colas
- **File I/O** - Persistencia de historial en archivos de texto

### **Gestión de Memoria y Recursos:**
- **Arrays estáticos** - Sin allocación dinámica para mayor estabilidad
- **Terminación nula explícita** - Prevención de buffer overflow
- **Limpieza automática** - Eliminación de colas al terminar procesos
- **Manejo robusto de errores** - Validación en todas las operaciones IPC
- **Historial persistente** - Archivos conservados después del cierre

### **Funcionalidades Avanzadas:**
- **Creación automática de salas** - No requiere pre-configuración
- **Entrada/salida dinámica** - Usuarios pueden unirse/abandonar libremente
- **Prevención de duplicados** - Validación de usuarios únicos por sala
- **Información de estado** - Contadores de usuarios y ocupación
- **Logs detallados** - Debugging y monitoreo de operaciones

------------------------------------------------------------------------

## Comandos de Desarrollo

### **Compilación:**
```bash
make                # Compilar ambos programas
make servidor      # Solo servidor
make cliente       # Solo cliente
make clean         # Limpiar archivos objeto y ejecutables
```

### **Debugging y Monitoreo:**
```bash
# Ver colas de mensajes activas en el sistema
ipcs -q

# Eliminar colas manualmente si es necesario
ipcrm -q <id_cola>

# Monitorear procesos del chat
ps aux | grep -E "(servidor|cliente)"

# Ver historial de una sala
cat General.txt
cat Deportes.txt

# Monitorear archivos de log en tiempo real
tail -f General.txt
```

### **Testing Avanzado:**
```bash
# Terminal 1: Servidor con logs
./servidor

# Terminales 2-5: Múltiples clientes concurrentes
./cliente Alice &
./cliente Bob &
./cliente Charlie &
./cliente Diana &

# Todos pueden unirse a salas y chatear simultáneamente
# Probar comandos: join, /leave, /list, /users
```

------------------------------------------------------------------------

## Solución de Problemas

### **Error: "No se puede conectar al servidor"**
- **Causa:** El servidor no está ejecutándose
- **Solución:** Ejecutar `./servidor` primero en terminal separada

### **Error: "msgget: File exists" o cola no disponible**
- **Causa:** Colas de mensajes previas no eliminadas correctamente
- **Solución:** 
  ```bash
  # Ver colas activas
  ipcs -q
  # Eliminar manualmente
  ipcrm -q <id_cola_problemática>
  ```

### **Cliente no recibe mensajes:**
- **Verificar:** Que estés en la misma sala que otros usuarios
- **Solución:** Usar `/users` para ver usuarios en sala, `/list` para ver salas

### **Historial no se guarda:**
- **Verificar:** Permisos de escritura en directorio
- **Ubicación:** Archivos `<nombre_sala>.txt` en directorio actual

### **Comportamiento inesperado:**
- **Restart completo:** Terminar todos los procesos y reiniciar servidor
- **Logs:** Revisar salida del servidor para debugging

------------------------------------------------------------------------

## Características del Código Documentado

### **Documentación Comprensiva:**
- **Comentarios completos** in español para mejor comprensión
- **Headers de funciones** con parámetros y valores de retorno
- **Explicación de estructuras** de datos y su propósito
- **Flujo de operaciones** paso a paso documentado
- **Manejo de errores** explicado en detalle

### **Mejores Prácticas Implementadas:**
- **Terminación nula segura** en todas las operaciones de string
- **Validación de parámetros** en todas las funciones
- **Limpieza de recursos** automática y manual
- **Manejo robusto de señales** para terminación limpia
- **Logs informativos** para debugging y monitoreo

### **Arquitectura Extensible:**
- **Configuración flexible** mediante constantes
- **Protocolo extensible** - fácil agregar nuevos tipos de mensaje
- **Modularidad** - funciones bien definidas y reutilizables
- **Escalabilidad** - límites configurables para producción

------------------------------------------------------------------------

## Autores

- **Jennifer Andrea Lopez Gomez**
- **Santiago Alexander Cardenas Laverde**

---

**Curso:** Sistemas Operativos 2  
**Proyecto:** Sistema de Chat Multi-Sala con IPC System V  
**Año:** 2025  
**Institución:** Universidad EAFIT

### **Licencia**
Este proyecto es desarrollado con fines académicos para el curso de Sistemas Operativos 2.

### **Contribuciones**
El código está completamente documentado y es ideal para:
- **Aprendizaje de IPC** en sistemas Unix/Linux
- **Referencia de programación** en C con System V
- **Material educativo** sobre comunicación entre procesos
- **Ejemplo de mejores prácticas** en programación de sistemas

### **Características Destacadas**
Este proyecto demuestra dominio de:
- **Colas de Mensajes System V** - Comunicación robusta entre procesos
- **Programación Multihilo** - pthread para manejo asíncrono
- **Gestión de Recursos** - Limpieza automática y manual
- **Persistencia de Datos** - Historial en archivos de texto
- **Arquitectura de Protocolos** - Diseño extensible de mensajes
- **Documentación Técnica** - Código completamente comentado

---

*README actualizado: Septiembre 2025 - Versión con funcionalidades avanzadas completas*