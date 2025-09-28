# Sistema de Chat Multi-Sala - Sistemas Operativos 2

Este proyecto implementa un **sistema completo de chat multi-sala** con un **servidor centralizado** y **múltiples clientes** que se comunican mediante **colas de mensajes System V** en C, desarrollado para el curso **Sistemas Operativos 2**.

## Características Principales

- **Salas de chat múltiples** - Creación automática de salas
- **Múltiples usuarios** - Hasta 20 usuarios por sala, 10 salas simultáneas
- **Comunicación en tiempo real** - Mensajes instantáneos entre usuarios
- **Multihilo** - Recepción asíncrona de mensajes en el cliente
- **Gestión robusta de recursos** - Limpieza automática de colas System V
- **Protocolo estructurado** - Tipos de mensaje bien definidos

------------------------------------------------------------------------

## Estructura del Proyecto

```
SistemasOperativos2/
├── servidor.c       # Servidor de chat multi-sala (completamente comentado)
├── cliente.c        # Cliente de chat con interfaz de usuario (completamente comentado)  
├── Makefile         # Reglas de compilación automática
├── README.md        # Esta documentación
└── .gitignore       # Exclusión de archivos temporales
```

------------------------------------------------------------------------

## Instalación y Ejecución

### 1. **Compilación**
```bash
make
```
Esto generará los ejecutables `servidor` y `cliente`.

### 2. **Iniciar el Servidor**
```bash
./servidor
```
El servidor se iniciará y esperará conexiones de clientes:
```
=== Servidor de Chat Iniciado ===
Cola global ID: 32768
Esperando conexiones de clientes...
```

### 3. **Conectar Clientes**
En terminales separadas, ejecutar múltiples clientes:
```bash
./cliente Juan
./cliente Maria  
./cliente Pedro
```

Cada cliente mostrará:
```
=== Cliente de Chat ===
Bienvenid@, Juan!
Conectado al servidor (Cola Global: 32768, Cola Privada: 32769)

Comandos disponibles:
  join <sala>  - Unirse a una sala de chat
  <mensaje>    - Enviar mensaje a la sala actual
  Ctrl+C       - Salir del cliente

Salas sugeridas: General, Deportes, Tecnologia
======================

> 
```

------------------------------------------------------------------------

## Guía de Uso

### **Comandos Disponibles:**

#### **Unirse a una Sala:**
```bash
> join General
```
- Si la sala no existe, se crea automáticamente
- El servidor confirma la conexión

#### **Enviar Mensajes:**
```bash
> Hola a todos!
> ¿Cómo están?
```
- Los mensajes se distribuyen a todos los usuarios de la sala
- El remitente no ve su propio mensaje (como en chats reales)

#### **Salir del Cliente:**
- `Ctrl+C` - Cierra el cliente y limpia recursos automáticamente

------------------------------------------------------------------------

## Ejemplo de Sesión Completa

### **Terminal 1 - Servidor:**
```bash
$ ./servidor
=== Servidor de Chat Iniciado ===
Cola global ID: 32768
Esperando conexiones de clientes...

[JOIN] Usuario 'Juan' solicita unirse a sala 'General' (cola_cliente=32769)
[INFO] Nueva sala creada: 'General' (ID=32770)
[JOIN] Usuario 'Juan' agregado a sala 'General' (1/20 usuarios)

[JOIN] Usuario 'Maria' solicita unirse a sala 'General' (cola_cliente=32771)
[JOIN] Usuario 'Maria' agregado a sala 'General' (2/20 usuarios)

[MSG] Sala='General', Usuario='Juan': Hola Maria!
[DISTRIBUCIÓN] Enviando mensaje a 2 usuarios en sala 'General'

[MSG] Sala='General', Usuario='Maria': ¡Hola Juan! ¿Cómo estás?
[DISTRIBUCIÓN] Enviando mensaje a 2 usuarios en sala 'General'
```

### **Terminal 2 - Cliente Juan:**
```bash
$ ./cliente Juan
=== Cliente de Chat ===
Bienvenid@, Juan!
...
> join General
Intentando unirse a la sala 'General'...

[SERVIDOR] Bienvenido a la sala 'General'!
> Hola Maria!

[General] Maria: ¡Hola Juan! ¿Cómo estás?
> Muy bien, gracias por preguntar
```

### **Terminal 3 - Cliente Maria:**
```bash
$ ./cliente Maria  
=== Cliente de Chat ===
Bienvenid@, Maria!
...
> join General
Intentando unirse a la sala 'General'...

[SERVIDOR] Bienvenido a la sala 'General'!

[General] Juan: Hola Maria!
> ¡Hola Juan! ¿Cómo estás?

[General] Juan: Muy bien, gracias por preguntar
> Me alegra saber eso :)
```

------------------------------------------------------------------------

## Arquitectura del Sistema

### **Protocolo de Comunicación:**

| Tipo | Nombre | Dirección | Descripción |
|------|--------|-----------|-------------|
| `1` | **JOIN** | Cliente → Servidor | Solicitud para unirse a una sala |
| `2` | **RESP** | Servidor → Cliente | Respuestas y notificaciones del servidor |
| `3` | **MSG** | Cliente → Servidor | Mensaje de chat a distribuir |
| `4` | **CHAT** | Servidor → Cliente | Mensaje distribuido a usuarios de sala |

### **Componentes del Sistema:**

#### **Servidor (`servidor.c`)**
- **Cola Global**: Recibe todas las solicitudes de clientes
- **Gestión de Salas**: Crea y administra hasta 10 salas simultáneas
- **Distribución de Mensajes**: Envía mensajes a colas privadas de usuarios
- **Limpieza Automática**: Elimina colas System V al terminar

#### **Cliente (`cliente.c`)**
- **Cola Privada**: Recibe respuestas del servidor y mensajes de otros usuarios
- **Interfaz de Usuario**: Comandos simples e intuitivos
- **Multihilo**: Hilo separado para recepción asíncrona de mensajes
- **Gestión de Estado**: Mantiene información de sala actual

### **Flujo de Datos:**
1. **Cliente** envía mensaje JOIN/MSG a **Cola Global**
2. **Servidor** procesa mensaje y actualiza estructuras internas
3. **Servidor** distribuye mensajes CHAT a **Colas Privadas** de usuarios
4. **Clientes** reciben mensajes asíncronamente en hilo separado

------------------------------------------------------------------------

## Detalles Técnicos

### **Límites del Sistema:**
- **Máximo de salas:** 10 simultáneas
- **Usuarios por sala:** 20 máximo
- **Longitud de mensaje:** 256 caracteres
- **Longitud de nombres:** 50 caracteres

### **Tecnologías Utilizadas:**
- **IPC System V Message Queues** - Comunicación entre procesos
- **POSIX Threads** - Recepción asíncrona en cliente
- **Signal Handling** - Limpieza automática con Ctrl+C
- **ftok()** - Generación de claves únicas para colas

### **Gestión de Memoria:**
- **Sin allocación dinámica** - Uso de arrays estáticos para estabilidad
- **Limpieza automática** - Eliminación de colas al terminar procesos
- **Manejo de errores robusto** - Validación en todas las operaciones IPC

------------------------------------------------------------------------

## Comandos de Desarrollo

### **Compilación:**
```bash
make                # Compilar ambos programas
make servidor      # Solo servidor
make cliente       # Solo cliente
make clean         # Limpiar archivos objeto
```

### **Debugging:**
```bash
# Ver colas de mensajes activas
ipcs -q

# Eliminar colas manualmente (si es necesario)
ipcrm -q <id_cola>

# Monitorear procesos
ps aux | grep -E "(servidor|cliente)"
```

### **Testing:**
```bash
# Terminal 1: Servidor
./servidor

# Terminal 2-4: Múltiples clientes
./cliente Alice &
./cliente Bob &  
./cliente Charlie &

# Todos pueden unirse a la misma sala y chatear
```

------------------------------------------------------------------------

## Solución de Problemas

### **Error: "No se puede conectar al servidor"**
- **Causa:** El servidor no está ejecutándose
- **Solución:** Ejecutar `./servidor` primero

### **Error: "msgget: File exists"**
- **Causa:** Colas de mensajes previas no eliminadas
- **Solución:** `ipcrm -q $(ipcs -q | awk '/32768/ {print $2}')`

### **Comportamiento inesperado:**
- **Verificar:** Que servidor y cliente estén compilados con la misma estructura `struct mensaje`
- **Restart:** Terminar todos los procesos y reiniciar servidor

------------------------------------------------------------------------

## Características Avanzadas del Código

### **Comentarios Comprehensivos:**
- **Documentación completa** en español
- **Explicación de estructuras** de datos
- **Flujo de funciones** paso a paso
- **Manejo de errores** documentado

### **Mejores Prácticas:**
- **Terminación nula** explícita en strings
- **Validación de límites** en arrays
- **Limpieza de recursos** automática
- **Manejo de señales** robusto

### **Extensibilidad:**
- Fácil agregar nuevos tipos de mensaje
- Configuración de límites mediante constantes
- Estructura modular para nuevas funcionalidades

------------------------------------------------------------------------

## Autores

- **Jennifer Andrea Lopez Gomez**
- **Santiago Alexander Cardenas Laverde**

---

**Curso:** Sistemas Operativos  
**Institución:** Universidad EAFIT
**Año:** 2025