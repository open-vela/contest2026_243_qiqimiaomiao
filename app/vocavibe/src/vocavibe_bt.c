/****************************************************************************
 * contest2026_243_qiqimiaomiao/app/vocavibe/src/vocavibe_bt.c
 *
 * VocaVibe BLE Nordic UART Service (NUS) Implementation
 ****************************************************************************/

#include "vocavibe_bt.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <advertiser_data.h>
#include <bluetooth.h>
#include <bt_addr.h>
#include <bt_gatt_defs.h>
#include <bt_gatts.h>
#include <bt_le_advertiser.h>
#include <bt_uuid.h>

#define TAG "vocavibe_bt"

/* NUS UUIDs (little-endian byte arrays for BT_UUID_DECLARE_128)
 * Base: 6E40xxxx-B5A3-F393-E0A9-E50E24DCCA9E
 */
#define NUS_SVC_UUID_BYTES \
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, \
    0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e

#define NUS_RX_UUID_BYTES \
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, \
    0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e

#define NUS_TX_UUID_BYTES \
    0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, \
    0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e

enum {
    NUS_SVC_ID = 1,
    NUS_TX_CHR_ID,
    NUS_TX_CCC_ID,
    NUS_RX_CHR_ID,
};

#define DEFAULT_MTU 23
#define BLE_ADV_DEVICE_NAME "VocaVibe-243"
#define BLE_ADV_INTERVAL    160 /* 100ms (160 * 0.625ms) */

/* Global BLE State */
static struct {
    bt_instance_t *bt_ins;
    gatts_handle_t srv_handle;

    bt_address_t peer_addr;
    bool connected;
    uint16_t mtu;
    bool notify_enabled;

    bt_advertiser_t *adv_handle;
    bool advertising;

    vocavibe_bt_config_t config;

    bool initialized;
    pthread_mutex_t lock;
} g_vocavibe_bt = {
    .connected = false,
    .mtu = DEFAULT_MTU,
    .notify_enabled = false,
    .adv_handle = NULL,
    .advertising = false,
    .initialized = false,
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

/* Forward Declarations */
static int vocavibe_adv_start(void);
static void vocavibe_adv_stop(void);

/* Callbacks */
static void on_connected(gatts_handle_t srv_handle, bt_address_t *addr)
{
    char addr_str[18] = {0};
    if (!addr) {
        return;
    }
    bt_addr_ba2str(addr, addr_str);
    printf("[%s] BLE Client Connected: %s\n", TAG, addr_str);

    vocavibe_bt_conn_cb_t conn_cb = NULL;
    void *user_data = NULL;

    pthread_mutex_lock(&g_vocavibe_bt.lock);
    memcpy(&g_vocavibe_bt.peer_addr, addr, sizeof(bt_address_t));
    g_vocavibe_bt.connected = true;
    g_vocavibe_bt.notify_enabled = false;
    conn_cb = g_vocavibe_bt.config.conn_cb;
    user_data = g_vocavibe_bt.config.user_data;
    pthread_mutex_unlock(&g_vocavibe_bt.lock);

    if (conn_cb) {
        conn_cb(true, user_data);
    }
}

static void on_disconnected(gatts_handle_t srv_handle, bt_address_t *addr)
{
    char addr_str[18] = {0};
    if (addr) {
        bt_addr_ba2str(addr, addr_str);
        printf("[%s] BLE Client Disconnected: %s\n", TAG, addr_str);
    }

    vocavibe_bt_conn_cb_t conn_cb = NULL;
    void *user_data = NULL;
    bool still_init = false;

    pthread_mutex_lock(&g_vocavibe_bt.lock);
    g_vocavibe_bt.connected = false;
    g_vocavibe_bt.notify_enabled = false;
    g_vocavibe_bt.mtu = DEFAULT_MTU;
    conn_cb = g_vocavibe_bt.config.conn_cb;
    user_data = g_vocavibe_bt.config.user_data;
    still_init = g_vocavibe_bt.initialized;
    pthread_mutex_unlock(&g_vocavibe_bt.lock);

    if (conn_cb) {
        conn_cb(false, user_data);
    }

    /* Auto restart advertising on disconnect */
    if (still_init) {
        vocavibe_adv_start();
    }
}

static void on_mtu_changed(gatts_handle_t srv_handle, bt_address_t *addr, uint32_t mtu)
{
    printf("[%s] MTU Changed to: %u\n", TAG, (unsigned int)mtu);
    pthread_mutex_lock(&g_vocavibe_bt.lock);
    g_vocavibe_bt.mtu = (uint16_t)mtu;
    pthread_mutex_unlock(&g_vocavibe_bt.lock);
}

static void on_attr_table_added(gatts_handle_t srv_handle, gatt_status_t status, uint16_t attr_handle)
{
    printf("[%s] Attr table added (handle=0x%04x, status=%d)\n", TAG, attr_handle, status);
}

static void on_notify_complete(gatts_handle_t srv_handle, bt_address_t *addr, gatt_status_t status, uint16_t attr_handle)
{
    if (status != GATT_STATUS_SUCCESS) {
        printf("[%s] Notify failed (handle=0x%04x, status=%d)\n", TAG, attr_handle, status);
    }
}

/* RX Characteristic Write Callback (Incoming Data from Phone) */
static uint16_t rx_char_on_write(gatts_handle_t srv_handle, bt_address_t *addr,
                                 uint16_t attr_handle, const uint8_t *value,
                                 uint16_t length, uint16_t offset)
{
    (void)srv_handle;
    (void)addr;
    (void)attr_handle;
    (void)offset;

    if (value && length > 0) {
        vocavibe_bt_recv_cb_t recv_cb = NULL;
        void *user_data = NULL;

        pthread_mutex_lock(&g_vocavibe_bt.lock);
        recv_cb = g_vocavibe_bt.config.recv_cb;
        user_data = g_vocavibe_bt.config.user_data;
        pthread_mutex_unlock(&g_vocavibe_bt.lock);

        if (recv_cb) {
            recv_cb(value, length, user_data);
        }
    }

    return length;
}

/* TX CCC Write Callback (Enable/Disable Notifications) */
static uint16_t tx_ccc_on_write(gatts_handle_t srv_handle, bt_address_t *addr,
                                uint16_t attr_handle, const uint8_t *value,
                                uint16_t length, uint16_t offset)
{
    (void)srv_handle;
    (void)addr;
    (void)attr_handle;
    (void)offset;

    if (!value || length < 2) {
        return 0;
    }

    uint16_t ccc_val = value[0] | (value[1] << 8);
    bool enabled = (ccc_val & 0x0001) != 0;

    pthread_mutex_lock(&g_vocavibe_bt.lock);
    g_vocavibe_bt.notify_enabled = enabled;
    pthread_mutex_unlock(&g_vocavibe_bt.lock);

    printf("[%s] TX Notification %s by client\n", TAG, enabled ? "ENABLED" : "DISABLED");
    return length;
}

static const gatts_callbacks_t g_gatts_cbs = {
    .size = sizeof(gatts_callbacks_t),
    .on_connected = on_connected,
    .on_disconnected = on_disconnected,
    .on_attr_table_added = on_attr_table_added,
    .on_attr_table_removed = NULL,
    .on_notify_complete = on_notify_complete,
    .on_mtu_changed = on_mtu_changed,
    .on_phy_read = NULL,
    .on_phy_updated = NULL,
    .on_conn_param_changed = NULL,
};

static gatt_attr_db_t s_nus_attr_db[] = {
    GATT_H_PRIMARY_SERVICE(BT_UUID_DECLARE_128(NUS_SVC_UUID_BYTES), NUS_SVC_ID),
    GATT_H_CHARACTERISTIC_AUTO_RSP(BT_UUID_DECLARE_128(NUS_TX_UUID_BYTES),
        GATT_PROP_NOTIFY, 0, NULL, 0, NUS_TX_CHR_ID),
    GATT_H_CCCD(GATT_PERM_READ | GATT_PERM_WRITE,
        tx_ccc_on_write, NUS_TX_CCC_ID),
    GATT_H_CHARACTERISTIC_USER_RSP(BT_UUID_DECLARE_128(NUS_RX_UUID_BYTES),
        GATT_PROP_WRITE | GATT_PROP_WRITE_NR, GATT_PERM_WRITE,
        NULL, rx_char_on_write, NUS_RX_CHR_ID),
};

static gatt_srv_db_t s_nus_service_db = {
    .attr_db = s_nus_attr_db,
    .attr_num = sizeof(s_nus_attr_db) / sizeof(gatt_attr_db_t),
};

/* Advertising Callbacks */
static void on_adv_start(bt_advertiser_t *adv, uint8_t adv_id, uint8_t status)
{
    if (status == BT_ADV_STATUS_SUCCESS) {
        printf("[%s] Advertising active (adv_id=%u, name=%s)\n", TAG, adv_id, BLE_ADV_DEVICE_NAME);
        pthread_mutex_lock(&g_vocavibe_bt.lock);
        g_vocavibe_bt.advertising = true;
        pthread_mutex_unlock(&g_vocavibe_bt.lock);
    } else {
        printf("[%s] Advertising start returned status: %u\n", TAG, status);
    }
}

static void on_adv_stopped(bt_advertiser_t *adv, uint8_t adv_id)
{
    printf("[%s] Advertising stopped (adv_id=%u)\n", TAG, adv_id);
    pthread_mutex_lock(&g_vocavibe_bt.lock);
    g_vocavibe_bt.advertising = false;
    g_vocavibe_bt.adv_handle = NULL;
    pthread_mutex_unlock(&g_vocavibe_bt.lock);
}

static const advertiser_callback_t g_adv_cbs = {
    .size = sizeof(advertiser_callback_t),
    .on_advertising_start = on_adv_start,
    .on_advertising_stopped = on_adv_stopped,
};

static int vocavibe_adv_start(void)
{
    pthread_mutex_lock(&g_vocavibe_bt.lock);
    if (!g_vocavibe_bt.initialized || !g_vocavibe_bt.bt_ins) {
        pthread_mutex_unlock(&g_vocavibe_bt.lock);
        return -ENODEV;
    }
    pthread_mutex_unlock(&g_vocavibe_bt.lock);

    advertiser_data_t *adv_data = advertiser_data_new();
    if (!adv_data) {
        return -ENOMEM;
    }

    bt_uuid_t svc_uuid;
    static const uint8_t svc_bytes[] = { NUS_SVC_UUID_BYTES };
    bt_uuid128_create(&svc_uuid, svc_bytes);
    advertiser_data_add_service_uuid(adv_data, &svc_uuid);

    uint16_t adv_len = 0;
    uint8_t *p_adv = advertiser_data_build(adv_data, &adv_len);

    advertiser_data_t *scan_rsp = advertiser_data_new();
    if (!scan_rsp) {
        advertiser_data_free(adv_data);
        return -ENOMEM;
    }
    advertiser_data_set_name(scan_rsp, BLE_ADV_DEVICE_NAME);

    uint16_t rsp_len = 0;
    uint8_t *p_rsp = advertiser_data_build(scan_rsp, &rsp_len);

    ble_adv_params_t params;
    memset(&params, 0, sizeof(params));
    params.adv_type = BT_LE_ADV_IND;
    params.own_addr_type = BT_LE_ADDR_TYPE_PUBLIC;
    params.interval = BLE_ADV_INTERVAL;
    params.channel_map = BT_LE_ADV_CHANNEL_DEFAULT;
    params.filter_policy = BT_LE_ADV_FILTER_WHITE_LIST_FOR_NONE;
    params.duration = 0;

    bt_advertiser_t *handle = bt_le_start_advertising(
        g_vocavibe_bt.bt_ins, &params,
        p_adv, adv_len,
        p_rsp, rsp_len,
        (advertiser_callback_t *)&g_adv_cbs);

    advertiser_data_free(adv_data);
    advertiser_data_free(scan_rsp);

    if (!handle) {
        return -EIO;
    }

    pthread_mutex_lock(&g_vocavibe_bt.lock);
    g_vocavibe_bt.adv_handle = handle;
    pthread_mutex_unlock(&g_vocavibe_bt.lock);

    return 0;
}

static void vocavibe_adv_stop(void)
{
    pthread_mutex_lock(&g_vocavibe_bt.lock);
    bt_advertiser_t *handle = g_vocavibe_bt.adv_handle;
    g_vocavibe_bt.adv_handle = NULL;
    g_vocavibe_bt.advertising = false;
    pthread_mutex_unlock(&g_vocavibe_bt.lock);

    if (handle && g_vocavibe_bt.bt_ins) {
        bt_le_stop_advertising(g_vocavibe_bt.bt_ins, handle);
    }
}

static void *adv_retry_task(void *arg)
{
    (void)arg;
    for (int i = 1; i <= 8; i++) {
        sleep(2);
        pthread_mutex_lock(&g_vocavibe_bt.lock);
        bool done = g_vocavibe_bt.advertising || !g_vocavibe_bt.initialized || g_vocavibe_bt.connected;
        pthread_mutex_unlock(&g_vocavibe_bt.lock);

        if (done) {
            break;
        }

        printf("[%s] Advertising retry %d/8...\n", TAG, i);
        if (vocavibe_adv_start() == 0) {
            sleep(1);
            pthread_mutex_lock(&g_vocavibe_bt.lock);
            bool ok = g_vocavibe_bt.advertising;
            pthread_mutex_unlock(&g_vocavibe_bt.lock);
            if (ok) {
                break;
            }
        }
    }
    return NULL;
}

/* Public APIs */
int vocavibe_bt_init(const vocavibe_bt_config_t *config)
{
    bt_status_t status;
    int ret;

    if (!config) {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_vocavibe_bt.lock);
    if (g_vocavibe_bt.initialized) {
        pthread_mutex_unlock(&g_vocavibe_bt.lock);
        return 0;
    }
    g_vocavibe_bt.config = *config;
    g_vocavibe_bt.initialized = true;
    pthread_mutex_unlock(&g_vocavibe_bt.lock);

    printf("[%s] Initializing VocaVibe BLE stack...\n", TAG);

    g_vocavibe_bt.bt_ins = bluetooth_get_instance();
    if (!g_vocavibe_bt.bt_ins) {
        printf("[%s] Error: Failed to obtain Bluetooth instance\n", TAG);
        g_vocavibe_bt.initialized = false;
        return -ENODEV;
    }

    status = bt_gatts_register_service(g_vocavibe_bt.bt_ins, &g_vocavibe_bt.srv_handle,
                                       (gatts_callbacks_t *)&g_gatts_cbs);
    if (status != BT_STATUS_SUCCESS) {
        printf("[%s] Error: bt_gatts_register_service failed: %d\n", TAG, status);
        g_vocavibe_bt.initialized = false;
        return -EIO;
    }

    status = bt_gatts_add_attr_table(g_vocavibe_bt.srv_handle, &s_nus_service_db);
    if (status != BT_STATUS_SUCCESS) {
        printf("[%s] Error: bt_gatts_add_attr_table failed: %d\n", TAG, status);
        bt_gatts_unregister_service(g_vocavibe_bt.srv_handle);
        g_vocavibe_bt.srv_handle = NULL;
        g_vocavibe_bt.initialized = false;
        return -EIO;
    }

    printf("[%s] NUS GATT Service registered successfully\n", TAG);

    ret = vocavibe_adv_start();
    if (ret < 0) {
        printf("[%s] Notice: Initial adv start returned %d (starting retry task)\n", TAG, ret);
    }

    pthread_t thread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, 2048);
    pthread_create(&thread, &attr, adv_retry_task, NULL);
    pthread_attr_destroy(&attr);
    pthread_detach(thread);

    return 0;
}

int vocavibe_bt_deinit(void)
{
    pthread_mutex_lock(&g_vocavibe_bt.lock);
    if (!g_vocavibe_bt.initialized) {
        pthread_mutex_unlock(&g_vocavibe_bt.lock);
        return 0;
    }
    g_vocavibe_bt.initialized = false;
    g_vocavibe_bt.connected = false;
    g_vocavibe_bt.notify_enabled = false;
    pthread_mutex_unlock(&g_vocavibe_bt.lock);

    vocavibe_adv_stop();

    if (g_vocavibe_bt.srv_handle) {
        bt_gatts_unregister_service(g_vocavibe_bt.srv_handle);
        g_vocavibe_bt.srv_handle = NULL;
    }

    printf("[%s] BLE Service stopped\n", TAG);
    return 0;
}

bool vocavibe_bt_is_connected(void)
{
    bool conn;
    pthread_mutex_lock(&g_vocavibe_bt.lock);
    conn = g_vocavibe_bt.connected;
    pthread_mutex_unlock(&g_vocavibe_bt.lock);
    return conn;
}

bool vocavibe_bt_is_notify_enabled(void)
{
    bool enabled;
    pthread_mutex_lock(&g_vocavibe_bt.lock);
    enabled = g_vocavibe_bt.notify_enabled;
    pthread_mutex_unlock(&g_vocavibe_bt.lock);
    return enabled;
}

int vocavibe_bt_send(const uint8_t *data, uint16_t len)
{
    if (!data || len == 0) {
        return -EINVAL;
    }

    gatts_handle_t srv_handle;
    bt_address_t peer_addr;
    uint16_t mtu;

    pthread_mutex_lock(&g_vocavibe_bt.lock);
    if (!g_vocavibe_bt.initialized || !g_vocavibe_bt.connected || !g_vocavibe_bt.notify_enabled) {
        pthread_mutex_unlock(&g_vocavibe_bt.lock);
        return -ENOTCONN;
    }
    srv_handle = g_vocavibe_bt.srv_handle;
    memcpy(&peer_addr, &g_vocavibe_bt.peer_addr, sizeof(bt_address_t));
    mtu = g_vocavibe_bt.mtu;
    pthread_mutex_unlock(&g_vocavibe_bt.lock);

    uint16_t max_payload = (mtu > 3) ? (mtu - 3) : 20;
    if (len > max_payload) {
        len = max_payload;
    }

    bt_status_t status = bt_gatts_notify(srv_handle, &peer_addr, NUS_TX_CHR_ID, (uint8_t *)data, len);
    if (status != BT_STATUS_SUCCESS) {
        printf("[%s] Notify send failed: %d\n", TAG, status);
        return -EIO;
    }

    return (int)len;
}

int vocavibe_bt_send_str(const char *str)
{
    if (!str) {
        return -EINVAL;
    }
    return vocavibe_bt_send((const uint8_t *)str, (uint16_t)strlen(str));
}

uint16_t vocavibe_bt_get_mtu(void)
{
    uint16_t mtu;
    pthread_mutex_lock(&g_vocavibe_bt.lock);
    mtu = g_vocavibe_bt.mtu;
    pthread_mutex_unlock(&g_vocavibe_bt.lock);
    return mtu;
}
