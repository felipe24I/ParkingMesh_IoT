# ParkingMesh IoT

Proyecto de **parqueadero IoT** usando **Wi-Fi Mesh (ESP-MESH)** con tarjetas **Seeed Studio XIAO ESP32C6**.  
Cada nodo mesh tiene un **sensor ultrasónico** para detectar si la plaza está **OCUPADA** o **LIBRE** y envía su estado a través de la red mesh hacia un **nodo ROOT**

Este proyecto hace parte del **proyecto final de IoT** y está pensado como base para un sistema de monitoreo de parqueaderos en tiempo real.

---

## Características principales

- Topología **Wi-Fi Mesh** (ESP-MESH):
  - 1 nodo **ROOT** (XIAO ESP32C6).
  - 2 nodos **MESH** (XIAO ESP32C6).

- Cada nodo mesh tiene:
  - 1 sensor ultrasónico (p.ej. HC-SR04) para medir distancia.
  - Lógica para clasificar la plaza como **OCUPADA** o **LIBRE**.

- El ROOT:
  - Recibe los mensajes de todos los nodos.
  - Muestra los estados por puerto serie.

Ejemplo de mensaje recibido en el ROOT:
```text
Nodo 2: LIBRE (dist=107.3 cm, capa 2)
Nodo 1: OCUPADO (dist=18.4 cm, capa 1)
```

## Arquitectura general

- Cada **Nodo MESH:**
  - Lee el sensor ultrasónico.
  - Determina estado OCUPADO/LIBRE según un umbral.
  - Envía mensaje al ROOT usando ESP-MESH.

- El **ROOT:**
  - Centraliza los mensajes.

## Hardware utilizado

- **Placas**
  - 1 × Seeed Studio **XIAO ESP32C6** (ROOT).
  - 2 × Seeed Studio **XIAO ESP32C6** (nodos mesh).
    
- **Sensores de distancia:**
  - 2 × Sensor ultrasónico tipo **HC-SR04** (uno por cada nodo mesh).

- **Otros:**
  - Cables Dupont.
  - Protoboard (opcional).
  - Fuente de alimentación USB para cada placa.

## Software y herramientas

- **ESP-IDF** (v5.x o superior).
- **Python 3** (para el entorno de ESP-IDF).
- **Visual Studio Code** (opcional pero recomendado).
- Extensión ESP-IDF para VS Code (opcional).
- **Git** para control de versiones.

## Estructura del proyecto

Estructura típica de un proyecto ESP-IDF:

´´´
Programa_mesh_sensores/
├─ main/
│  ├─ mesh_main.c        # Lógica principal de Mesh + sensores
│  └─ CMakeLists.txt
├─ CMakeLists.txt
├─ sdkconfig             # Configuración generada por menuconfig
└─ README.md             # Este archivo
´´´

## Configuración del proyecto

### 1. Clonar el repositorio

´´´
git clone https://github.com/<tu_usuario>/Programa_mesh_sensores.git
cd Programa_mesh_sensores
´´´

### 2. Configurar el entorno ESP-IDF

Ejemplo (Windows, usando el ESP-IDF previamente instalado):

 ´´´
 idf.py --version   # Para comprobar que todo está bien
´´´

### 3. Configurar parámetros Wi-Fi / Mesh

Se Configuran con menuconfig (desde VS Code)

Recuerda tener la placa y el com al cual le vas a aplicar la configuración

<img width="408" height="45" alt="image" src="https://github.com/user-attachments/assets/be02ec34-c3bc-496a-aa64-6f64fe390fba" />


1. En Visual Studio Code, presiona:
   
   Ctrl + Shift + P

  y escriba:

  ```
  ESP-IDF: Open ESP-IDF terminal
  ```

  <img width="741" height="496" alt="image" src="https://github.com/user-attachments/assets/63a4cd35-9605-417b-9cb2-35f71d3cf1c0" />

  Luego selecciona esa opción para abrir la terminal de ESP-IDF.
  

  Luego selecciona esa opción para abrir la terminal de ESP-IDF.

2. En la terminal de ESP-IDF, ejecuta:

   ```
   idf.py menuconfig
   ```
   
  <img width="1157" height="197" alt="image" src="https://github.com/user-attachments/assets/e1ad4a8d-6e55-4f78-98d3-45b3622ca60d" />

  Presiona **Enter** y se abrirá el menú:

  **Espressif IoT Development Framework Configuration**

  ![Imagen de WhatsApp 2025-12-10 a las 08 36 11_866b182d](https://github.com/user-attachments/assets/8ff5a1dc-0c02-4af6-ad87-7785594c07f3)


3. Dentro de este menú configura lo siguiente:
   - Ve a **Serial flasher config**
     - Cambia Flash size a: 4 MB
     - Guarda los cambios pulsando la tecla S (Save).
     - Vuelve al menú anterior con Esc.
       
  ![Imagen de WhatsApp 2025-12-10 a las 08 37 27_447ff5a3](https://github.com/user-attachments/assets/75da2d78-a8c2-4594-9545-3f1b12824b2f)

  - Ve a **Partition Table**
    - En la opción Partition Table selecciona: **Partition Table (Single factory app (large), no OTA)**
    - Guarda de nuevo con S y vuelve con Esc.

  ![Imagen de WhatsApp 2025-12-10 a las 08 38 47_ba1d4b71](https://github.com/user-attachments/assets/f9f4cee2-5966-4ba9-9a2b-6b902cc1d2de)

  - Ve a **Parking Mesh Configuration** (menú específico del proyecto):
    - Marca la casilla **“Este dispositivo será ROOT”** solo si vas a compilar y flashear el **ROOT.**
    - Si el dispositivo será un nodo **MESH**, deja la casilla **desmarcada**
    - Configura **Router SSID** y **Router Password**, teniendo en cuenta que debe ser una red en la banda de **2.4 GHz** (no 5 GHz).

  ![Imagen de WhatsApp 2025-12-10 a las 08 40 53_f6c34094](https://github.com/user-attachments/assets/683e40b8-f37d-47db-a9f2-b07eca2c4467)

4. Cuando termines de configurar todo, guarda con S, sal con Esc hasta volver a la terminal, y ya puedes compilar y flashear normalmente:

### Configuración de los sensores ultrasónicos

En el código se definen los pines para el TRIG y el ECHO del sensor:

```
#define TRIG_GPIO   XX   // GPIO conectado al TRIG
#define ECHO_GPIO   YY   // GPIO conectado al ECHO
```

Asegúrate de que:
- Esos GPIO existan en la XIAO ESP32C6 / ESP32S3.
- El cableado coincida con estos valores.

También tendrás un umbral en centímetros para decidir si la plaza está ocupada:

```
#define UMBRAL_OCUPADO_CM   40.0f   // Ejemplo: < 40 cm = OCUPADO
```

---

## Compilación y carga del firmware

1. Conecta la placa por USB
2. En la raíz del proyecto:

```
idf.py set-target esp32c6        # solo la primera vez (para la XIAO ESP32C6)
idf.py -p COMx build flash monitor     # cambia COMx por el puerto correcto
````

#### Ejemplo de salida:

---

## Pruebas básicas

1. Flashea primero el ROOT (con THIS_IS_ROOT = 1, osea la casilla marcada en el menuconfig).
2. Flashea los nodos MESH (con THIS_IS_ROOT = 0 y NODE_ID distinto).
3. Abre el monitor serie del ROOT:
   
  ```
   idf.py -p COMx monitor
  ```
4. Acerca/aleja un objeto del sensor ultrasónico de cada nodo y verifica:
  - Cambios en la distancia.
  - Cambio entre OCUPADO / LIBRE.

---

## Pendientes / Mejoras futuras

- Implementar completamente la **integración MQTT** en el nodo ROOT:
  - Conexión estable al broker (credenciales, puerto, QoS).
  - Lógica de reconexión automática.
  - Publicación periódica/por evento en tópicos del tipo: `esp32/parking/node/<NODE_ID>`.
    
- Dashboard web en HTML/JS que escuche el broker MQTT y muestre:
  - Estado de cada plaza (verde/rojo).
  - Distancia medida por nodo.
  - Guardar histórico en base de datos.
  - Alertas cuando un vehículo ocupe una plaza por encima de cierto tiempo.

---

## Licencia

Proyecto académico. Puedes reutilizar y modificar el código con fines educativos, citando la fuente si lo compartes.
