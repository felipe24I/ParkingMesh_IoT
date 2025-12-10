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



