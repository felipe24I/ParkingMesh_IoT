#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>   // malloc

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"

#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"

#include "esp_mesh.h"
#include "esp_mesh_internal.h"
#include "sdkconfig.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"   // esp_rom_delay_us()

#include "mqtt_client.h"   // Cliente MQTT

static const char *TAG = "PARKING_MESH";

// =====================================================
//  CONFIG MESH
// =====================================================

// ID fijo de la red mesh (igual en todos los nodos)
static const uint8_t MESH_ID[6] = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11 };

// =====================================================
//  ID LÓGICO POR NODO (CAMBIA ESTO EN CADA FIRMWARE)
// =====================================================
// Para mesh1 compilas con:  -DNODE_ID=1
// Para mesh2 compilas con:  -DNODE_ID=2
#ifndef NODE_ID
#define NODE_ID 2
#endif

// =====================================================
//  PINES Y UMBRAL DEL SENSOR HC-SR04
// =====================================================
// XIAO ESP32C6
// D2 (A2, GPIO2)  -> TRIG (salida)
// D3 (GPIO21)     -> ECHO (entrada, con divisor de tensión)
#define TRIG_GPIO   GPIO_NUM_2     // D2
#define ECHO_GPIO   GPIO_NUM_21    // D3

// Si la distancia es menor o igual a este valor => plaza OCUPADA
#define SLOT_DISTANCE_THRESHOLD_CM   20.0f

// =====================================================
//  ESTADO DE CONEXIÓN A LA MALLA
// =====================================================
static bool s_mesh_connected = false;   // true cuando tiene padre

// =====================================================
//  ESTADO/MANEJO DE MQTT (SOLO LO USA EL ROOT)
// =====================================================
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;
static bool s_mqtt_started   = false;   // indica si ya iniciamos el cliente MQTT

// =====================================================
//  FORMATO DE MENSAJE PUNTO A PUNTO
// =====================================================
typedef struct {
    uint8_t src_id;      // ID lógico del nodo origen
    uint8_t hop_count;   // reservado
    char    payload[60]; // mensaje de texto
} parking_msg_t;

// Ajusta estos datos de tu broker MQTT en AWS
#define MQTT_URI  "mqtt://54.177.245.166:1883"
#define MQTT_USER "esp32"
#define MQTT_PASS "esp32"


// =====================================================
//  FUNCIONES DEL SENSOR HC-SR04
// =====================================================
static void hcsr04_init(void)
{
    // TRIG como salida
    gpio_config_t io_conf = {0};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << TRIG_GPIO);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en   = 0;
    gpio_config(&io_conf);

    // ECHO como entrada
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << ECHO_GPIO);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en   = 0;
    gpio_config(&io_conf);

    // Asegurar TRIG en bajo
    gpio_set_level(TRIG_GPIO, 0);
}

/**
 * Mide la distancia en centímetros con el HC-SR04.
 * Devuelve true si la medición fue válida, false si hubo timeout.
 */
static bool hcsr04_measure(float *distance_cm)
{
    const int64_t timeout_us = 30000; // 30ms de timeout

    // Pulso de disparo de 10us
    gpio_set_level(TRIG_GPIO, 0);
    esp_rom_delay_us(2);
    gpio_set_level(TRIG_GPIO, 1);
    esp_rom_delay_us(10);
    gpio_set_level(TRIG_GPIO, 0);

    // Esperar a que ECHO suba a 1
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(ECHO_GPIO) == 0) {
        if (esp_timer_get_time() - start > timeout_us) {
            return false;  // timeout esperando flanco de subida
        }
    }

    // Medir cuánto tiempo permanece en 1
    int64_t echo_start = esp_timer_get_time();
    while (gpio_get_level(ECHO_GPIO) == 1) {
        if (esp_timer_get_time() - echo_start > timeout_us) {
            return false;  // timeout demasiado largo, probablemente sin eco
        }
    }
    int64_t echo_end = esp_timer_get_time();

    int64_t pulse_width = echo_end - echo_start; // microsegundos

    // Distancia (cm) = (tiempo_us / 2) * 0.0343 => 0.01715 * tiempo_us
    *distance_cm = (float)pulse_width * 0.01715f;
    return true;
}


// =====================================================
//  MQTT: EVENT HANDLER
// =====================================================
static void mqtt_event_handler(void *handler_args,
                               esp_event_base_t base,
                               int32_t event_id,
                               void *event_data)
{
    (void) handler_args;
    (void) base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    (void) event; // no lo usamos, pero evitamos warning

    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        s_mqtt_connected = true;
        ESP_LOGI(TAG, "✅ MQTT conectado al broker");
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_mqtt_connected = false;
        ESP_LOGW(TAG, "⚠️ MQTT desconectado del broker");
        break;

    default:
        // Otros eventos: no necesarios para este proyecto
        break;
    }
}

// =====================================================
//  MQTT: INICIAR CLIENTE
// =====================================================
static void mqtt_app_start(void)
{
    if (s_mqtt_started) {
        // Ya iniciado, no hacemos nada
        return;
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_URI,   // Ej: "mqtt://54.177.245.166:1883"

        .credentials = {
            .username  = MQTT_USER,       // Ej: "esp32"
            .client_id = "parking_root",  // Puedes cambiarlo si quieres

            .authentication = {
                .password = MQTT_PASS,    // Ej: "esp32"
            },
        },
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_mqtt_client == NULL) {
        ESP_LOGE(TAG, "❌ Error inicializando cliente MQTT");
        return;
    }

    esp_mqtt_client_register_event(
        s_mqtt_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL
    );

    esp_err_t err = esp_mqtt_client_start(s_mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "❌ Error al iniciar MQTT: %s", esp_err_to_name(err));
    } else {
        s_mqtt_started = true;
        ESP_LOGI(TAG, "✅ Cliente MQTT iniciado (intentando conectar al broker)");
    }
}



// ============================ RX TASK (ROOT) ============================
static void mesh_rx_task(void *arg)
{
    mesh_addr_t from;
    mesh_data_t data;
    int flag;

    uint8_t *buf = malloc(150);
    if (!buf) {
        ESP_LOGE(TAG, "No se pudo reservar memoria para RX");
        vTaskDelete(NULL);
        return;
    }

    data.data  = buf;
    data.size  = 150;
    data.proto = MESH_PROTO_BIN;
    data.tos   = MESH_TOS_P2P;

    ESP_LOGI(TAG, "RX task iniciada (ROOT escuchando)");

    while (true) {
        data.size = 150;
        flag = 0;

        esp_err_t err = esp_mesh_recv(&from, &data, portMAX_DELAY, &flag, NULL, 0);
        if (err == ESP_OK) {

            if (data.size >= sizeof(parking_msg_t)) {
                parking_msg_t *msg = (parking_msg_t *)data.data;

                bool ocupado = (strstr(msg->payload, "OCUPADO") != NULL);

                ESP_LOGI(TAG,
                         "📩 Mensaje desde src_id=%d (último salto MAC %02x:%02x:%02x:%02x:%02x:%02x): '%s'",
                         msg->src_id,
                         from.addr[0], from.addr[1], from.addr[2],
                         from.addr[3], from.addr[4], from.addr[5],
                         msg->payload);

                ESP_LOGI(TAG, "      → Estado plaza: %s",
                         ocupado ? "OCUPADA" : "LIBRE / DESCONOCIDO");

                // ================== PUBLICAR A MQTT ==================
                if (s_mqtt_client && s_mqtt_connected) {
                    char topic[64];
                    snprintf(topic, sizeof(topic),
                             "esp32/parking/node/%d",
                             msg->src_id);

                    int msg_id = esp_mqtt_client_publish(
                        s_mqtt_client,
                        topic,
                        msg->payload, // mensaje en texto
                        0,            // 0 => usa strlen
                        1,            // QoS 1
                        0             // retain = 0
                    );

                    if (msg_id >= 0) {
                        ESP_LOGI(TAG, "📡 Publicado en MQTT [%s]", topic);
                    } else {
                        ESP_LOGW(TAG, "⚠️ Fallo al publicar en MQTT");
                    }
                } else {
                    ESP_LOGW(TAG, "MQTT aún no iniciado o no conectado, no se publica");
                }

            } else {
                ESP_LOGW(TAG,
                         "Mensaje binario muy pequeño (%d bytes), contenido (texto): '%s'",
                         data.size,
                         (char *)data.data);
            }

        } else {
            ESP_LOGW(TAG, "Error en esp_mesh_recv: %s", esp_err_to_name(err));
        }
    }
}


// ============================ TX TASK (NODES) ============================
static void mesh_tx_task(void *arg)
{
    mesh_data_t data;
    parking_msg_t msg;

    data.data  = (uint8_t *)&msg;
    data.proto = MESH_PROTO_BIN;
    data.tos   = MESH_TOS_P2P;

    ESP_LOGI(TAG, "TX task iniciada (NODES enviando hacia ROOT)");

    while (true) {

        // 🔹 Si soy ROOT o aún no tengo padre, NO envío
        if (esp_mesh_is_root() || !s_mesh_connected) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        msg.src_id    = (uint8_t)NODE_ID;
        msg.hop_count = 0;

        float dist_cm = 0.0f;
        bool ok = hcsr04_measure(&dist_cm);
        bool ocupado = false;

        if (!ok) {
            snprintf(msg.payload, sizeof(msg.payload),
                     "Nodo %d: ERROR sensor (layer %d)",
                     NODE_ID, esp_mesh_get_layer());
        } else {
            ocupado = (dist_cm <= SLOT_DISTANCE_THRESHOLD_CM);

            snprintf(msg.payload, sizeof(msg.payload),
                     "Nodo %d: %s (dist=%.1f cm, capa %d)",
                     NODE_ID,
                     ocupado ? "OCUPADO" : "LIBRE",
                     dist_cm,
                     esp_mesh_get_layer());
        }

        data.size = sizeof(parking_msg_t);

        esp_err_t err = esp_mesh_send(NULL, &data, 0, NULL, 0);
        if (err == ESP_OK) {
            ESP_LOGI(TAG,
                     "📤 Enviado a ROOT: %s",
                     msg.payload);
        } else {
            ESP_LOGW(TAG, "Fallo al enviar al ROOT: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(5000));  // cada 5s reporta el estado de la plaza
    }
}



// ======================= EVENT HANDLER MESH ======================
static void mesh_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    (void) arg;
    (void) event_data;

    if (event_base != MESH_EVENT) {
        return;
    }

    switch (event_id) {
    case MESH_EVENT_STARTED:
        ESP_LOGI(TAG, "MESH iniciado");
        s_mesh_connected = false;
        break;

    case MESH_EVENT_STOPPED:
        ESP_LOGI(TAG, "MESH detenido");
        s_mesh_connected = false;
        break;

    case MESH_EVENT_PARENT_CONNECTED:
        s_mesh_connected = true;
        ESP_LOGI(TAG, "Conectado a padre, layer actual: %d", esp_mesh_get_layer());
        break;

    case MESH_EVENT_PARENT_DISCONNECTED:
        s_mesh_connected = false;
        ESP_LOGW(TAG, "Desconectado del padre");
        break;

    case MESH_EVENT_CHILD_CONNECTED:
        ESP_LOGI(TAG, "👶 Nodo hijo conectado a este nodo");
        break;

    case MESH_EVENT_CHILD_DISCONNECTED:
        ESP_LOGW(TAG, "Un nodo hijo se ha desconectado");
        break;

    case MESH_EVENT_ROOT_ADDRESS:
        ESP_LOGI(TAG, "ROOT address actualizada");
        break;

    default:
        // Otros eventos no críticos para este proyecto
        break;
    }
}

// ======================= EVENT HANDLER IP (ROOT TIENE IP) =========
static void ip_event_handler(void *arg,
                             esp_event_base_t event_base,
                             int32_t event_id,
                             void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP → el root ya tiene IP del router");

#if CONFIG_MESH_IS_ROOT
        // Iniciamos MQTT SOLO cuando el root ya tiene IP
        mqtt_app_start();
#endif
    }
}


// ======================== MESH INIT & START ======================
static void mesh_init_start(void)
{
    esp_err_t err;

    // 1) NVS
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // 2) NETIF + EVENT LOOP
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Registrar handler de IP (para saber cuándo tenemos IP del router)
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &ip_event_handler,
        NULL
    ));

    // 3) NETIFS MESH
    esp_netif_t *netif_sta = NULL;
    esp_netif_t *netif_ap  = NULL;
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_mesh_netifs(&netif_sta, &netif_ap));

    // Usar la STA como interfaz por defecto (sale a Internet)
    ESP_ERROR_CHECK(esp_netif_set_default_netif(netif_sta));

    // 4) WIFI INIT
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 5) MESH INIT
    ESP_ERROR_CHECK(esp_mesh_init());

    ESP_ERROR_CHECK(esp_event_handler_register(
        MESH_EVENT,
        ESP_EVENT_ANY_ID,
        &mesh_event_handler,
        NULL
    ));

    ESP_ERROR_CHECK(esp_mesh_set_topology(MESH_TOPO_TREE));
    ESP_ERROR_CHECK(esp_mesh_set_max_layer(6));
    ESP_ERROR_CHECK(esp_mesh_set_vote_percentage(1));
    ESP_ERROR_CHECK(esp_mesh_set_ap_assoc_expire(60));

    // Por ahora mismo límite para todos (ROOT y NODES)
    ESP_ERROR_CHECK(esp_mesh_set_ap_connections(5));

    // 6) CONFIGURAR MESH
    mesh_cfg_t cfg = MESH_INIT_CONFIG_DEFAULT();

    memcpy((uint8_t *)&cfg.mesh_id, MESH_ID, 6);
    cfg.channel = 0;                       // 0 => mismo canal del router
    cfg.mesh_ap.max_connection = 5;

    // Router del ROOT (SSID & PASS desde sdkconfig)
    const char *router_ssid = CONFIG_MESH_ROUTER_SSID;
    const char *router_pass = CONFIG_MESH_ROUTER_PASS;

    if (router_ssid && strlen(router_ssid) > 0 && strlen(router_ssid) <= 32) {
        memset(cfg.router.ssid, 0, sizeof(cfg.router.ssid));
        strncpy((char *)cfg.router.ssid, router_ssid, sizeof(cfg.router.ssid) - 1);
        cfg.router.ssid_len = strlen(router_ssid);

        memset(cfg.router.password, 0, sizeof(cfg.router.password));
        if (router_pass && strlen(router_pass) > 0 && strlen(router_pass) <= 64) {
            strncpy((char *)cfg.router.password, router_pass, sizeof(cfg.router.password) - 1);
        }
    } else {
        cfg.router.ssid_len = 0;
        memset(cfg.router.ssid, 0, sizeof(cfg.router.ssid));
        memset(cfg.router.password, 0, sizeof(cfg.router.password));
    }

    ESP_ERROR_CHECK(esp_mesh_set_config(&cfg));

#if CONFIG_MESH_IS_ROOT
    ESP_LOGI(TAG, "🟦 Este dispositivo será ROOT");
    ESP_ERROR_CHECK(esp_mesh_set_type(MESH_ROOT));
#else
    ESP_LOGI(TAG, "🟩 Este dispositivo será NODE (tipo por defecto)");
#endif

    ESP_ERROR_CHECK(esp_mesh_start());
    ESP_LOGI(TAG, "🚀 ESP-MESH iniciado correctamente");
}


// ============================== app_main ==========================
void app_main(void)
{
    // Ajustar niveles de log
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);

    ESP_LOGI(TAG, "Iniciando Parking Mesh...");

    mesh_init_start();

    // Inicializar sensor en todos los nodos
    hcsr04_init();

#if CONFIG_MESH_IS_ROOT
    // 👉 ROOT: solo crea la tarea RX;
    // MQTT se arranca automáticamente cuando llegue IP_EVENT_STA_GOT_IP
    xTaskCreate(mesh_rx_task, "mesh_rx_task", 4096, NULL, 5, NULL);
#else
    // 👉 NODOS: solo envían periódicamente al ROOT
    xTaskCreate(mesh_tx_task, "mesh_tx_task", 4096, NULL, 5, NULL);
#endif
}
