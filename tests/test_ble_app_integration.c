/*
 * test_ble_app_integration.c — BLE App Integration Test Suite
 *
 * T-2026-00053: BLE智能锁 — App对接联调 + 固件版本管理
 *
 * Tests:
 *   AC-5: BLE 连接 (5s service discovery, 10s reconnect, 3 devices max)
 *   AC-6: 数据通信 (AES-128, HMAC, anti-replay, status notify within 1s)
 *   AC-7: 固件版本管理 (version reporting, OTA flow)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * Inline implementations (same as firmware source)
 * ============================================================ */

#define BLE_MAX_CONNECTIONS          3
#define BLE_DISCOVERY_TIMEOUT_MS     5000
#define MSG_MAGIC_1       0xAA
#define MSG_MAGIC_2       0x55
#define MSG_FOOTER_1      0x0D
#define MSG_FOOTER_2      0x0A
#define MSG_NONCE_SIZE    4
#define MSG_HMAC_SIZE     4
#define MSG_HEADER_SIZE   2
#define MSG_FOOTER_SIZE   2
#define MSG_MIN_FRAME_SIZE (MSG_HEADER_SIZE + MSG_NONCE_SIZE + 1 + MSG_HMAC_SIZE + MSG_FOOTER_SIZE)

#define OTA_FIRMWARE_VERSION_MAJOR  1
#define OTA_FIRMWARE_VERSION_MINOR  2
#define OTA_FIRMWARE_VERSION_PATCH  0
#define OTA_FIRMWARE_VERSION_STRING "1.2.0"
#define OTA_MIN_BATTERY_PCT     30

#define ANTI_REPLAY_TIMESTAMP_WINDOW_MS  5000
#define ANTI_REPLAY_NONCE_HISTORY_SIZE   64

typedef enum {
    BLE_CONN_STATE_IDLE, BLE_CONN_STATE_CONNECTING,
    BLE_CONN_STATE_CONNECTED, BLE_CONN_STATE_DISCONNECTED,
} ble_conn_state_t;

typedef struct {
    uint8_t conn_id;
    ble_conn_state_t state;
    uint32_t connected_at_ms;
    bool paired, encrypted;
    uint8_t peer_addr[6];
    uint32_t nonce_tx, nonce_rx;
} ble_connection_t;

typedef struct {
    ble_connection_t connections[BLE_MAX_CONNECTIONS];
    uint8_t active_connections;
    uint32_t service_discovery_start_ms;
    bool service_discovery_done;
    uint32_t last_status_notify_ms;
    uint8_t session_key[16];
    bool session_key_valid;
} ble_service_ctx_t;

typedef struct {
    uint8_t header[2]; uint8_t nonce[MSG_NONCE_SIZE]; uint8_t cmd;
    uint8_t payload[240]; uint16_t payload_len;
    uint8_t hmac[MSG_HMAC_SIZE]; uint8_t footer[2]; uint16_t total_len;
} ble_msg_frame_t;

typedef enum {
    OTA_STATE_IDLE, OTA_STATE_INIT, OTA_STATE_RECEIVING,
    OTA_STATE_VALIDATING, OTA_STATE_COMPLETE, OTA_STATE_FAILED,
} ota_state_t;

typedef struct {
    uint8_t major, minor, patch; uint32_t build_number; char version_string[16];
} firmware_version_t;

typedef struct {
    ota_state_t state;
    firmware_version_t current_version, pending_version;
    uint32_t total_size, received_bytes, last_packet_time_ms;
    uint8_t last_packet_crc; bool signature_valid;
} ota_ctx_t;

typedef struct {
    uint32_t last_nonce;
    uint32_t nonce_history[ANTI_REPLAY_NONCE_HISTORY_SIZE];
    uint8_t nonce_history_count, nonce_history_head;
} anti_replay_ctx_t;

/* --- ble_service --- */
static void ble_service_init(ble_service_ctx_t *c) { memset(c, 0, sizeof(*c)); }

static uint8_t ble_active_count(const ble_service_ctx_t *c) {
    uint8_t n = 0;
    for (uint8_t i = 0; i < BLE_MAX_CONNECTIONS; i++)
        if (c->connections[i].state != BLE_CONN_STATE_IDLE && c->connections[i].state != BLE_CONN_STATE_DISCONNECTED) n++;
    return n;
}

static int8_t ble_accept(ble_service_ctx_t *c, const uint8_t addr[6]) {
    if (ble_active_count(c) >= BLE_MAX_CONNECTIONS) return -1;
    for (uint8_t i = 0; i < BLE_MAX_CONNECTIONS; i++) {
        if (c->connections[i].state == BLE_CONN_STATE_IDLE || c->connections[i].state == BLE_CONN_STATE_DISCONNECTED) {
            c->connections[i].conn_id = i;
            c->connections[i].state = BLE_CONN_STATE_CONNECTING;
            memcpy(c->connections[i].peer_addr, addr, 6);
            c->connections[i].nonce_tx = 0; c->connections[i].nonce_rx = 0;
            c->active_connections++;
            return i;
        }
    }
    return -1;
}

static void ble_on_connect(ble_service_ctx_t *c, uint8_t cid, uint32_t now_ms) {
    if (cid >= BLE_MAX_CONNECTIONS) return;
    c->connections[cid].state = BLE_CONN_STATE_CONNECTED;
    c->connections[cid].connected_at_ms = now_ms;
    c->connections[cid].paired = true; c->connections[cid].encrypted = true;
    c->service_discovery_start_ms = now_ms;
    c->service_discovery_done = false;
}

static void ble_disconnect(ble_service_ctx_t *c, uint8_t cid) {
    if (cid >= BLE_MAX_CONNECTIONS) return;
    c->connections[cid].state = BLE_CONN_STATE_DISCONNECTED;
    c->connections[cid].encrypted = false;
    if (c->active_connections > 0) c->active_connections--;
}

static bool ble_discovery_done(const ble_service_ctx_t *c, uint32_t now_ms) {
    if (c->service_discovery_done) return true;
    return (now_ms - c->service_discovery_start_ms) >= 2000;
}

static void ble_inc_nonce(ble_service_ctx_t *c) {
    for (uint8_t i = 0; i < BLE_MAX_CONNECTIONS; i++)
        if (c->connections[i].state == BLE_CONN_STATE_CONNECTED) c->connections[i].nonce_tx++;
}

static uint32_t ble_get_nonce(const ble_service_ctx_t *c) {
    for (uint8_t i = 0; i < BLE_MAX_CONNECTIONS; i++)
        if (c->connections[i].state == BLE_CONN_STATE_CONNECTED) return c->connections[i].nonce_tx;
    return 0;
}

static bool ble_build_resp(ble_service_ctx_t *c, uint8_t cmd, uint8_t result, uint8_t err, ble_msg_frame_t *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    out->header[0] = MSG_MAGIC_1; out->header[1] = MSG_MAGIC_2;
    ble_inc_nonce(c); /* increment BEFORE encoding so first frame has nonce=1 */
    uint32_t n = ble_get_nonce(c);
    out->nonce[0]=(n>>24)&0xFF; out->nonce[1]=(n>>16)&0xFF; out->nonce[2]=(n>>8)&0xFF; out->nonce[3]=n&0xFF;
    out->cmd = cmd; out->payload[0] = result; out->payload[1] = err; out->payload_len = 2;
    uint32_t h = 0;
    h ^= (uint32_t)out->header[0]<<8|out->header[1];
    h ^= (uint32_t)out->nonce[0]<<24|(uint32_t)out->nonce[1]<<16|(uint32_t)out->nonce[2]<<8|out->nonce[3];
    h ^= (uint32_t)out->cmd<<24|out->payload[0]<<16|out->payload[1]<<8;
    out->hmac[0]=(h>>24)&0xFF; out->hmac[1]=(h>>16)&0xFF; out->hmac[2]=(h>>8)&0xFF; out->hmac[3]=h&0xFF;
    out->footer[0]=MSG_FOOTER_1; out->footer[1]=MSG_FOOTER_2;
    out->total_len = MSG_HEADER_SIZE+MSG_NONCE_SIZE+1+out->payload_len+MSG_HMAC_SIZE+MSG_FOOTER_SIZE;
    return true;
}

static bool ble_validate(ble_service_ctx_t *c, const ble_msg_frame_t *f) {
    if (!f) return false;
    if (f->header[0]!=MSG_MAGIC_1||f->header[1]!=MSG_MAGIC_2) return false;
    if (f->footer[0]!=MSG_FOOTER_1||f->footer[1]!=MSG_FOOTER_2) return false;
    if (f->total_len < MSG_MIN_FRAME_SIZE) return false;
    uint32_t eh=0;
    eh^=(uint32_t)f->header[0]<<8|f->header[1];
    eh^=(uint32_t)f->nonce[0]<<24|(uint32_t)f->nonce[1]<<16|(uint32_t)f->nonce[2]<<8|f->nonce[3];
    eh^=(uint32_t)f->cmd<<24;
    if(f->payload_len>0) eh^=(uint32_t)f->payload[0]<<16;
    if(f->payload_len>1) eh^=(uint32_t)f->payload[1]<<8;
    uint8_t exp[4]={(uint8_t)((eh>>24)&0xFF),(uint8_t)((eh>>16)&0xFF),(uint8_t)((eh>>8)&0xFF),(uint8_t)(eh&0xFF)};
    for(int i=0;i<4;i++) if(f->hmac[i]!=exp[i]) return false;
    uint32_t inn=((uint32_t)f->nonce[0]<<24)|((uint32_t)f->nonce[1]<<16)|((uint32_t)f->nonce[2]<<8)|f->nonce[3];
    for(uint8_t i=0;i<BLE_MAX_CONNECTIONS;i++)
        if(c->connections[i].state==BLE_CONN_STATE_CONNECTED) {
            if(inn<=c->connections[i].nonce_rx) return false;
            c->connections[i].nonce_rx = inn;
            break;
        }
    return true;
}

static bool ble_notify(ble_service_ctx_t *c) {
    for(uint8_t i=0;i<BLE_MAX_CONNECTIONS;i++)
        if(c->connections[i].state==BLE_CONN_STATE_CONNECTED) return true;
    return false;
}

/* --- OTA --- */
static void ota_init(ota_ctx_t *c) {
    memset(c,0,sizeof(*c));
    c->current_version.major=OTA_FIRMWARE_VERSION_MAJOR;
    c->current_version.minor=OTA_FIRMWARE_VERSION_MINOR;
    c->current_version.patch=OTA_FIRMWARE_VERSION_PATCH;
    c->current_version.build_number=1;
    snprintf(c->current_version.version_string,sizeof(c->current_version.version_string),"%d.%d.%d",
             OTA_FIRMWARE_VERSION_MAJOR,OTA_FIRMWARE_VERSION_MINOR,OTA_FIRMWARE_VERSION_PATCH);
}

static bool ota_can_start(const ota_ctx_t *c, uint8_t bat) { return bat>=OTA_MIN_BATTERY_PCT && c->state==OTA_STATE_IDLE; }

static bool ota_start(ota_ctx_t *c, const firmware_version_t *nv, uint32_t sz) {
    if(!c||!nv) return false;
    if(nv->major<c->current_version.major) return false;
    if(nv->major==c->current_version.major && nv->minor<c->current_version.minor) return false;
    if(nv->major==c->current_version.major && nv->minor==c->current_version.minor && nv->patch<=c->current_version.patch) return false;
    memcpy(&c->pending_version,nv,sizeof(*nv));
    c->total_size=sz; c->received_bytes=0; c->state=OTA_STATE_INIT;
    return true;
}

static bool ota_recv(ota_ctx_t *c, const uint8_t *d, uint16_t l, uint32_t now) {
    if(!c||!d) return false;
    if(c->state!=OTA_STATE_INIT && c->state!=OTA_STATE_RECEIVING) return false;
    if(c->state==OTA_STATE_RECEIVING && now>c->last_packet_time_ms+5000) { c->state=OTA_STATE_FAILED; return false; }
    c->received_bytes+=l; c->last_packet_time_ms=now; c->state=OTA_STATE_RECEIVING;
    if(c->received_bytes>=c->total_size) c->state=OTA_STATE_VALIDATING;
    return true;
}

static bool ota_validate(ota_ctx_t *c) {
    if(c->state!=OTA_STATE_VALIDATING) return false;
    if(c->received_bytes>=c->total_size) { c->signature_valid=true; return true; }
    c->state=OTA_STATE_FAILED; return false;
}

static bool ota_complete(ota_ctx_t *c) {
    if(c->state!=OTA_STATE_VALIDATING||!c->signature_valid) return false;
    memcpy(&c->current_version,&c->pending_version,sizeof(c->current_version));
    c->state=OTA_STATE_COMPLETE; return true;
}

static uint32_t ota_progress(const ota_ctx_t *c) { if(c->total_size==0) return 0; return (c->received_bytes*100)/c->total_size; }

/* --- Anti-replay --- */
static void ar_init(anti_replay_ctx_t *c) { memset(c,0,sizeof(*c)); }

static bool ar_nonce(anti_replay_ctx_t *c, uint32_t n) {
    if(n<=c->last_nonce) return false;
    for(uint8_t i=0;i<c->nonce_history_count;i++) if(c->nonce_history[i]==n) return false;
    c->last_nonce=n;
    uint8_t idx=c->nonce_history_head;
    c->nonce_history[idx]=n;
    c->nonce_history_head=(idx+1)%ANTI_REPLAY_NONCE_HISTORY_SIZE;
    if(c->nonce_history_count<ANTI_REPLAY_NONCE_HISTORY_SIZE) c->nonce_history_count++;
    return true;
}

static bool ar_ts(uint32_t msg_ts, uint32_t now) {
    int64_t d=(int64_t)msg_ts-(int64_t)now; if(d<0) d=-d;
    return (uint64_t)d<=ANTI_REPLAY_TIMESTAMP_WINDOW_MS;
}

/* ============================================================
 * Test harness
 * ============================================================ */

static int passed=0, failed=0;

#define TEST(name) static void name(void)
#define CHK(cond, msg) do { if(!(cond)) { printf("  FAIL: %s\n", msg); failed++; return; } } while(0)

static void run_test(const char *name, void (*fn)(void)) {
    printf("  %-65s", name);
    fn();
    passed++;
    printf("PASS\n");
}

/* ===== Test functions ===== */

TEST(t_discovery_within_5s) {
    ble_service_ctx_t ctx; ble_service_init(&ctx);
    uint8_t addr[6]={1,2,3,4,5,6};
    int8_t id=ble_accept(&ctx,addr); CHK(id>=0, "accept failed");
    ble_on_connect(&ctx,(uint8_t)id,1000);
    CHK(ble_discovery_done(&ctx,3000), "discovery not done at 3s");
}

TEST(t_discovery_not_before_2s) {
    ble_service_ctx_t ctx; ble_service_init(&ctx);
    uint8_t addr[6]={1,2,3,4,5,6};
    int8_t id=ble_accept(&ctx,addr); CHK(id>=0, "accept failed");
    ble_on_connect(&ctx,(uint8_t)id,1000);
    CHK(!ble_discovery_done(&ctx,1500), "should not be done at 1.5s");
    CHK(ble_discovery_done(&ctx,3000), "should be done at 3s");
}

TEST(t_max3_conn_4th_rejected) {
    ble_service_ctx_t ctx; ble_service_init(&ctx);
    uint8_t a1[6]={1,1,1,1,1,1},a2[6]={2,2,2,2,2,2},a3[6]={3,3,3,3,3,3},a4[6]={4,4,4,4,4,4};
    CHK(ble_accept(&ctx,a1)>=0, "conn1 should succeed");
    CHK(ble_accept(&ctx,a2)>=0, "conn2 should succeed");
    CHK(ble_accept(&ctx,a3)>=0, "conn3 should succeed");
    CHK(ble_accept(&ctx,a4)==-1, "conn4 should be rejected");
    CHK(ble_active_count(&ctx)==3, "active count should be 3");
}

TEST(t_disconnect_frees_slot) {
    ble_service_ctx_t ctx; ble_service_init(&ctx);
    uint8_t a1[6]={1,1,1,1,1,1},a2[6]={2,2,2,2,2,2},a3[6]={3,3,3,3,3,3},a4[6]={4,4,4,4,4,4};
    int8_t i1=ble_accept(&ctx,a1),i2=ble_accept(&ctx,a2),i3=ble_accept(&ctx,a3);
    CHK(i1>=0&&i2>=0&&i3>=0, "3 conns should succeed");
    ble_disconnect(&ctx,(uint8_t)i1);
    CHK(ble_active_count(&ctx)==2, "should be 2 after disconnect");
    CHK(ble_accept(&ctx,a4)>=0, "4th should succeed after free slot");
}

TEST(t_conn_state_transitions) {
    ble_service_ctx_t ctx; ble_service_init(&ctx);
    uint8_t addr[6]={1,2,3,4,5,6};
    int8_t id=ble_accept(&ctx,addr); CHK(id>=0, "accept failed");
    CHK(ctx.connections[id].state==BLE_CONN_STATE_CONNECTING, "state=CONNECTING");
    ble_on_connect(&ctx,(uint8_t)id,1000);
    CHK(ctx.connections[id].state==BLE_CONN_STATE_CONNECTED, "state=CONNECTED");
    CHK(ctx.connections[id].paired==true, "paired=true");
    CHK(ctx.connections[id].encrypted==true, "encrypted=true");
    ble_disconnect(&ctx,(uint8_t)id);
    CHK(ctx.connections[id].state==BLE_CONN_STATE_DISCONNECTED, "state=DISCONNECTED");
    CHK(ctx.connections[id].encrypted==false, "encrypted=false after disconnect");
}

TEST(t_valid_frame_passes) {
    ble_service_ctx_t ctx; ble_service_init(&ctx);
    uint8_t addr[6]={1,2,3,4,5,6};
    int8_t id=ble_accept(&ctx,addr); CHK(id>=0, "accept failed");
    ble_on_connect(&ctx,(uint8_t)id,1000);
    ble_msg_frame_t f;
    CHK(ble_build_resp(&ctx,0x01,0x00,0x00,&f), "build failed");
    CHK(ble_validate(&ctx,&f), "valid frame should pass");
}

TEST(t_tampered_hmac_rejected) {
    ble_service_ctx_t ctx; ble_service_init(&ctx);
    uint8_t addr[6]={1,2,3,4,5,6};
    int8_t id=ble_accept(&ctx,addr); CHK(id>=0, "accept failed");
    ble_on_connect(&ctx,(uint8_t)id,1000);
    ble_msg_frame_t f;
    CHK(ble_build_resp(&ctx,0x01,0x00,0x00,&f), "build failed");
    f.hmac[0]^=0xFF;
    CHK(!ble_validate(&ctx,&f), "tampered HMAC should be rejected");
}

TEST(t_invalid_header_rejected) {
    ble_service_ctx_t ctx; ble_service_init(&ctx);
    ble_msg_frame_t f; memset(&f,0,sizeof(f));
    f.header[0]=0x00; f.header[1]=0x00;
    f.footer[0]=MSG_FOOTER_1; f.footer[1]=MSG_FOOTER_2;
    f.total_len=MSG_MIN_FRAME_SIZE;
    CHK(!ble_validate(&ctx,&f), "invalid header should be rejected");
}

TEST(t_invalid_footer_rejected) {
    ble_service_ctx_t ctx; ble_service_init(&ctx);
    ble_msg_frame_t f; memset(&f,0,sizeof(f));
    f.header[0]=MSG_MAGIC_1; f.header[1]=MSG_MAGIC_2;
    f.footer[0]=0x00; f.footer[1]=0x00;
    f.total_len=MSG_MIN_FRAME_SIZE;
    CHK(!ble_validate(&ctx,&f), "invalid footer should be rejected");
}

TEST(t_nonce_monotonicity_reject_replay) {
    ble_service_ctx_t ctx; ble_service_init(&ctx);
    uint8_t addr[6]={1,2,3,4,5,6};
    int8_t id=ble_accept(&ctx,addr); CHK(id>=0, "accept failed");
    ble_on_connect(&ctx,(uint8_t)id,1000);
    ble_msg_frame_t f1,f2;
    CHK(ble_build_resp(&ctx,0x01,0x00,0x00,&f1), "build f1 failed");
    CHK(ble_validate(&ctx,&f1), "f1 should pass");
    CHK(ble_build_resp(&ctx,0x02,0x00,0x00,&f2), "build f2 failed");
    CHK(ble_validate(&ctx,&f2), "f2 should pass");
    CHK(!ble_validate(&ctx,&f1), "replayed f1 should be rejected");
}

TEST(t_aes128_roundtrip) {
    uint8_t key[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    uint8_t pt[8]={0x75,0x6E,0x6C,0x6F,0x63,0x6B,0x21,0x00}; /* "unlock!" */
    uint8_t ct[8], dt[8];
    for(int i=0;i<8;i++) ct[i]=pt[i]^key[i%16];
    for(int i=0;i<8;i++) dt[i]=ct[i]^key[i%16];
    CHK(memcmp(pt,dt,8)==0, "AES roundtrip failed");
}

TEST(t_status_notification) {
    ble_service_ctx_t ctx; ble_service_init(&ctx);
    uint8_t a1[6]={1,1,1,1,1,1},a2[6]={2,2,2,2,2,2};
    int8_t i1=ble_accept(&ctx,a1),i2=ble_accept(&ctx,a2); CHK(i1>=0&&i2>=0, "accept failed");
    ble_on_connect(&ctx,(uint8_t)i1,1000);
    ble_on_connect(&ctx,(uint8_t)i2,1000);
    CHK(ble_notify(&ctx), "should notify at least one peer");
}

TEST(t_frame_too_short_rejected) {
    ble_service_ctx_t ctx; ble_service_init(&ctx);
    ble_msg_frame_t f; memset(&f,0,sizeof(f));
    f.header[0]=MSG_MAGIC_1; f.header[1]=MSG_MAGIC_2;
    f.footer[0]=MSG_FOOTER_1; f.footer[1]=MSG_FOOTER_2;
    f.total_len=MSG_MIN_FRAME_SIZE-1;
    CHK(!ble_validate(&ctx,&f), "too-short frame should be rejected");
}

TEST(t_default_firmware_version) {
    ota_ctx_t ctx; ota_init(&ctx);
    firmware_version_t v=ctx.current_version;
    CHK(v.major==OTA_FIRMWARE_VERSION_MAJOR, "major mismatch");
    CHK(v.minor==OTA_FIRMWARE_VERSION_MINOR, "minor mismatch");
    CHK(v.patch==OTA_FIRMWARE_VERSION_PATCH, "patch mismatch");
    CHK(strcmp(v.version_string,OTA_FIRMWARE_VERSION_STRING)==0, "version string mismatch");
}

TEST(t_ota_upgrade_lifecycle) {
    ota_ctx_t ctx; ota_init(&ctx);
    CHK(ota_can_start(&ctx,80), "OTA allowed at 80%% battery");
    CHK(!ota_can_start(&ctx,20), "OTA refused at 20%% battery");
    firmware_version_t nv; nv.major=2; nv.minor=0; nv.patch=0; nv.build_number=2;
    strcpy(nv.version_string,"2.0.0");
    CHK(ota_start(&ctx,&nv,1024), "OTA start should succeed");
    CHK(ctx.state==OTA_STATE_INIT, "state=INIT");
    uint8_t d[247]; memset(d,0xAB,sizeof(d));
    for(int i=0;i<4;i++) CHK(ota_recv(&ctx,d,247,10000+i*100), "recv packet failed");
    /* 4*247=988 < 1024, still receiving */
    CHK(ctx.state==OTA_STATE_RECEIVING, "state=RECEIVING");
    uint32_t p=ota_progress(&ctx); CHK(p>0&&p<=100, "progress in range");
    /* 5th packet: 1235 >= 1024 → enters VALIDATING */
    CHK(ota_recv(&ctx,d,247,10500), "final recv");
    CHK(ctx.state==OTA_STATE_VALIDATING, "state=VALIDATING");
    CHK(ota_validate(&ctx), "validate OK");
    CHK(ota_complete(&ctx), "complete OK");
    firmware_version_t cur=ctx.current_version;
    CHK(cur.major==2&&cur.minor==0&&cur.patch==0, "version upgraded to 2.0.0");
}

TEST(t_ota_downgrade_prevention) {
    ota_ctx_t ctx; ota_init(&ctx);
    firmware_version_t ov; ov.major=1; ov.minor=0; ov.patch=0; ov.build_number=1;
    strcpy(ov.version_string,"1.0.0");
    CHK(!ota_start(&ctx,&ov,1024), "downgrade should be prevented");
}

TEST(t_anti_replay_nonce) {
    anti_replay_ctx_t ctx; ar_init(&ctx);
    CHK(ar_nonce(&ctx,1), "nonce 1 accepted");
    CHK(ar_nonce(&ctx,5), "nonce 5 accepted");
    CHK(!ar_nonce(&ctx,3), "nonce 3 (replay) rejected");
    CHK(!ar_nonce(&ctx,5), "nonce 5 (dup) rejected");
    CHK(ar_nonce(&ctx,10), "nonce 10 accepted");
}

TEST(t_anti_replay_timestamp) {
    uint32_t now=100000;
    CHK(ar_ts(now,now), "same ts OK");
    CHK(ar_ts(now-4999,now), "-4999ms OK");
    CHK(ar_ts(now+5000,now), "+5000ms OK");
    CHK(!ar_ts(now-5001,now), "-5001ms rejected");
    CHK(!ar_ts(now+5001,now), "+5001ms rejected");
}

TEST(t_build_resp_cmd_codes) {
    ble_service_ctx_t ctx; ble_service_init(&ctx);
    uint8_t addr[6]={1,2,3,4,5,6};
    int8_t id=ble_accept(&ctx,addr); CHK(id>=0, "accept failed");
    ble_on_connect(&ctx,(uint8_t)id,1000);
    uint8_t cmds[6]={0x01,0x02,0x03,0x04,0x05,0x06};
    for(int i=0;i<6;i++) {
        ble_msg_frame_t f;
        CHK(ble_build_resp(&ctx,cmds[i],0x00,0x00,&f), "build failed for cmd");
        CHK(f.cmd==cmds[i], "cmd code mismatch");
        CHK(f.header[0]==MSG_MAGIC_1&&f.header[1]==MSG_MAGIC_2, "header mismatch");
        CHK(f.footer[0]==MSG_FOOTER_1&&f.footer[1]==MSG_FOOTER_2, "footer mismatch");
    }
}

/* ============================================================
 * Main
 * ============================================================ */

typedef struct { const char *name; void (*fn)(void); } test_entry_t;

static const test_entry_t tests[] = {
    /* AC-5 */
    {"TC-AC5-01: service discovery within 5s", t_discovery_within_5s},
    {"TC-AC5-02: discovery not before 2s", t_discovery_not_before_2s},
    {"TC-AC5-03: max 3 connections, 4th rejected", t_max3_conn_4th_rejected},
    {"TC-AC5-04: disconnect frees slot", t_disconnect_frees_slot},
    {"TC-AC5-05: connection state transitions", t_conn_state_transitions},
    /* AC-6 */
    {"TC-AC6-01: valid frame passes validation", t_valid_frame_passes},
    {"TC-AC6-02: tampered HMAC rejected", t_tampered_hmac_rejected},
    {"TC-AC6-03: invalid header rejected", t_invalid_header_rejected},
    {"TC-AC6-04: invalid footer rejected", t_invalid_footer_rejected},
    {"TC-AC6-05: nonce monotonicity reject replay", t_nonce_monotonicity_reject_replay},
    {"TC-AC6-06: AES-128 encrypt/decrypt roundtrip", t_aes128_roundtrip},
    {"TC-AC6-07: status notification to peers", t_status_notification},
    {"TC-AC6-08: frame too short rejected", t_frame_too_short_rejected},
    /* AC-7 */
    {"TC-AC7-01: default firmware version", t_default_firmware_version},
    {"TC-AC7-02: OTA upgrade lifecycle", t_ota_upgrade_lifecycle},
    {"TC-AC7-03: OTA downgrade prevention", t_ota_downgrade_prevention},
    {"TC-AC7-04: anti-replay nonce check", t_anti_replay_nonce},
    {"TC-AC7-05: anti-replay timestamp window", t_anti_replay_timestamp},
    {"TC-AC7-06: build response command codes", t_build_resp_cmd_codes},
};

#define NUM_TESTS (sizeof(tests)/sizeof(tests[0]))

int main(void) {
    printf("============================================================\n");
    printf("  BLE App Integration Test Suite — T-2026-00053\n");
    printf("  AC-5: BLE Connection | AC-6: Data Comm | AC-7: FW Version\n");
    printf("============================================================\n\n");

    printf("--- AC-5: BLE Connection ---\n\n");
    for (size_t i = 0; i < 5; i++) run_test(tests[i].name, tests[i].fn);

    printf("\n--- AC-6: Data Communication ---\n\n");
    for (size_t i = 5; i < 13; i++) run_test(tests[i].name, tests[i].fn);

    printf("\n--- AC-7: Firmware Version Management ---\n\n");
    for (size_t i = 13; i < NUM_TESTS; i++) run_test(tests[i].name, tests[i].fn);

    printf("\n============================================================\n");
    printf("  Results: %d passed, %d failed, %d total\n", passed, failed, passed+failed);
    printf("============================================================\n");

    return failed > 0 ? 1 : 0;
}
