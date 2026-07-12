#include "wifi.h"
#include "cli.h"
#include "rtc.h"

#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_core.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/sys/time.h>
#include <zephyr/net/sntp.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/sys/select.h>


#if defined(CONFIG_WIFI_ESP32)
#include <esp_err.h>
#include <esp_wifi.h> 
#endif

#define CONFIG_ESP_WIFI_SSID     ""
#define CONFIG_ESP_WIFI_PASSWORD ""
#define CONFIG_ESP_MAXIMUM_RETRY 3

typedef struct
{
  char name[64];
  char wifi_ssid[32];
  char wifi_pass[64];
  char dest_ip[128];
  int  dest_port;
} wifi_nvs_t;

static void wifiThread(void *p1, void *p2, void *p3);
static bool wifiConfigLoad(void);
static bool wifiConfigSave(void);
static void wifiDisconnectSTA(void);
static void cliCmd(cli_args_t *args);
static int  wifiSettingsSet(const char *name, size_t len, settings_read_cb readCb, void *cbArg);
static void wifiMgmtEventHandler(struct net_mgmt_event_callback *cb, uint64_t mgmtEvent, struct net_if *iface);
static void ipMgmtEventHandler(struct net_mgmt_event_callback *cb, uint64_t mgmtEvent, struct net_if *iface);
static void syncTimeSNTP(void);

K_THREAD_STACK_DEFINE(wifi_thread_stack, 8192);
static struct k_thread wifi_thread_data;
static struct k_sem    wifi_connected_sem;
static struct k_sem    ip_obtained_sem;

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback dhcp_cb;

static uint16_t           retry_num    = 0;
static bool               is_connected = false;
static bool               sync_done    = false;

#define WIFI_SYNC_HOUR        4          // 매일 이 시각(정각)에 SNTP 재동기
#define WIFI_TIME_VALID_EPOCH 1600000000LL // 이 값보다 작으면 시각 미설정으로 간주
static int                sock         = -1;
static struct sockaddr_in dest_addr; // 순정 BSD sockaddr_in 사용

static wifi_nvs_t wifi_nvs =
{
  .name      = "ESP32_Zephyr",
  .wifi_ssid = CONFIG_ESP_WIFI_SSID,
  .wifi_pass = CONFIG_ESP_WIFI_PASSWORD,
  .dest_ip   = "192.168.0.155",
  .dest_port = 50000,
};

static struct settings_handler wifiSettingsConf = 
{
  .name  = "wifi",
  .h_set = wifiSettingsSet,
};

MODULE_DEF(wifi) 
{
  .name = "wifi",
  .priority = MODULE_PRI_LOW,
  .init = wifiInit
};



bool wifiInit(void)
{
  k_sem_init(&wifi_connected_sem, 0, 1);
  k_sem_init(&ip_obtained_sem, 0, 1);

  setenv("TZ", "KST-9", 1);
  tzset();
  
  if (settings_subsys_init() == 0)
  {
    settings_register(&wifiSettingsConf);
    wifiConfigLoad();
  }

  k_thread_create(&wifi_thread_data, wifi_thread_stack,
                  K_THREAD_STACK_SIZEOF(wifi_thread_stack),
                  wifiThread, NULL, NULL, NULL,
                  7, 0, K_NO_WAIT);

  printk("[OK] wifiInit\n");
  cliAdd("wifi", cliCmd);
  return true;
}

// Settings 서브시스템 콜백
static int wifiSettingsSet(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
  const char *next;
  if (settings_name_steq(name, "config", &next) && !next)
  {
    if (len == sizeof(wifi_nvs_t))
    {
      read_cb(cb_arg, &wifi_nvs, sizeof(wifi_nvs_t));
      return 0;
    }
  }
  if (settings_name_steq(name, "last_sync", &next) && !next)
  {
    return 0; // 이전 버전 잔재 키 : 무시하여 로드 에러 방지
  }
  return -ENOENT;
}

// Wi-Fi L2 연결 핸들러
static void wifiMgmtEventHandler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface)
{
  if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT)
  {
    const struct wifi_status *status = (const struct wifi_status *)cb->info;
    if (status->status == 0)
    {
      printk("[OK] Wi-Fi Connected to AP\n");
      k_sem_give(&wifi_connected_sem);
    }
    else
    {
      printk("[E_] Wi-Fi Connection failed (status: %d)\n", status->status);
    }
  }
  if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT)
  {
    printk("[  ] Wi-Fi Disconnected\n");
    is_connected = false;
  }
}

// DHCP IP 할당 핸들러
static void ipMgmtEventHandler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event, struct net_if *iface)
{
  if (mgmt_event == NET_EVENT_IPV4_DHCP_BOUND)
  {
    char buf[16];
    printk("[OK] Got IP: %s\n", net_addr_ntop(AF_INET, &iface->config.dhcpv4.requested_ip, buf, sizeof(buf)));
    retry_num = 0;
    k_sem_give(&ip_obtained_sem);
  }
}

bool wifiInitSTA(void)
{
  static bool cb_registered = false;
  if (!cb_registered)
  {
    net_mgmt_init_event_callback(&wifi_cb, wifiMgmtEventHandler, NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&wifi_cb);

    net_mgmt_init_event_callback(&dhcp_cb, ipMgmtEventHandler, NET_EVENT_IPV4_DHCP_BOUND);
    net_mgmt_add_event_callback(&dhcp_cb);
    cb_registered = true;
  }

  struct net_if *iface = net_if_get_default();
  if (!iface)
  {
    printk("[E_] Default network interface not found\n");
    return false;
  }

  struct wifi_connect_req_params c_params = {
    .ssid        = (uint8_t *)wifi_nvs.wifi_ssid,
    .ssid_length = strlen(wifi_nvs.wifi_ssid),
    .psk         = (uint8_t *)wifi_nvs.wifi_pass,
    .psk_length  = strlen(wifi_nvs.wifi_pass),
    .channel     = WIFI_CHANNEL_ANY,
    .security    = WIFI_SECURITY_TYPE_PSK,
  };

  printk("[  ] Connecting to AP SSID: %s...\n", wifi_nvs.wifi_ssid);

  int ret = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &c_params, sizeof(struct wifi_connect_req_params));
  if (ret)
  {
    printk("[E_] Wi-Fi connect request failed: %d\n", ret);
    return false;
  }

#if defined(CONFIG_WIFI_ESP32)
  int8_t    power = 80; // ESP32-S3 규격 상 최대치 (20dBm)
  esp_err_t err   = esp_wifi_set_max_tx_power(power);
  if (err == ESP_OK)
  {
    printk("[OK] Wi-Fi TX Power set to maximum (20dBm)\n");
  }
  else
  {
    printk("[E_] Failed to set Wi-Fi TX Power: %d\n", err);
  }
#endif

  if (k_sem_take(&wifi_connected_sem, K_MSEC(8000)) == 0)
  {
    if (k_sem_take(&ip_obtained_sem, K_MSEC(8000)) == 0)
    {
      // DHCP IP 바인딩이 성공 완료된 시점에 SNTP 서버 요청 연동
      syncTimeSNTP();      
      return true;
    }
  }

  return false;
}

static void syncTimeSNTP(void)
{
  struct sntp_ctx  ctx;
  struct sntp_time sntpTm;
  const char      *ntpServer = "kr.pool.ntp.org";

  struct zsock_addrinfo hints = {
    .ai_family   = AF_INET,
    .ai_socktype = SOCK_DGRAM,
    .ai_protocol = IPPROTO_UDP,
  };
  struct zsock_addrinfo *res;

  printk("[  ] Resolving NTP server DNS -> %s\n", ntpServer);

  // 1. 도메인 문자열을 구조체 주소 규격으로 변환하기 위해 DNS 쿼리 수행
  int dnsRet = zsock_getaddrinfo(ntpServer, "123", &hints, &res);
  if (dnsRet != 0)
  {
    printk("[E_] DNS resolution failed for %s (err: %d)\n", ntpServer, dnsRet);
    return;
  }

  printk("[  ] Initializing SNTP context...\n");

  // 2. Zephyr v4.4.0 규격: 파싱된 sockaddr 구조체 주소를 직접 전달 (인자는 총 3개)
  int ret = sntp_init(&ctx, res->ai_addr, res->ai_addrlen);
  if (ret < 0)
  {
    printk("[E_] Failed to initialize SNTP context (err: %d)\n", ret);
    zsock_freeaddrinfo(res);
    return;
  }

  printk("[  ] Sending SNTP query...\n");

  // 3. Zephyr v4.4.0 규격: timeout에 K_MSEC()이 아닌 정수형 밀리초(4000) 바로 기입
  ret = sntp_query(&ctx, 4000, &sntpTm);

  // 사용이 끝난 DNS 메모리는 즉시 해제
  zsock_freeaddrinfo(res);

  if (ret < 0)
  {
    printk("[E_] SNTP query failed (err: %d)\n", ret);
    return;
  }

  // 4. POSIX 시스템 타임스탬프 셋 주입
  // fraction 비트를 나노초(ns) 규격 범위(0 ~ 999,999,999) 내로 안전하게 스케일링 변환
  struct timespec ts = {
    .tv_sec  = sntpTm.seconds,
    .tv_nsec = ((uint64_t)sntpTm.fraction * 1000000000LL) >> 32};

  // 만약 위 스케일링 연산 후에도 미세하게 범위를 초과할 경우를 대비한 방어 코드
  if (ts.tv_nsec >= 1000000000LL)
  {
    ts.tv_nsec = 0;
  }

  if (clock_settime(CLOCK_REALTIME, &ts) == 0)
  {
    printk("[OK] Network time synchronization success! (Epoch: %lld)\n", (long long)ts.tv_sec);
    rtcSyncRtcFromSystem();
  }
  else
  {
    printk("[E_] Failed to set POSIX system clock (errno: %d, nsec: %ld)\n", errno, ts.tv_nsec);
  }
}

bool wifiConfigLoad(void)
{
  int err = settings_load();
  return (err == 0);
}

bool wifiConfigSave(void)
{
  int err = settings_save_one("wifi/config", &wifi_nvs, sizeof(wifi_nvs_t));
  return (err == 0);
}

bool wifiIsConnected(void)
{
  return is_connected;
}

/**
 * @brief Wi-Fi 연결을 명시적으로 끊고 무선 인터페이스를 정리합니다.
 */
static void wifiDisconnectSTA(void)
{
  struct net_if *iface = net_if_get_default();
  if (!iface) return;

  // 1. 기존에 생성된 소켓이 있다면 안전하게 닫기
  if (sock >= 0)
  {
    close(sock);
    sock = -1;
  }

  // 2. Zephyr Network Management에 Wi-Fi 연결 해제 요청
  int ret = net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0);
  if (ret && ret != -EALREADY)
  {
    printk("[E_] Wi-Fi disconnect request failed: %d\n", ret);
  }
  else
  {
    printk("[OK] Wi-Fi Disconnect requested for power saving\n");
  }

  // L3 스태이트 강제 해제 (Event Handler에서 처리되지만 확실히 하기 위함)
  is_connected = false;
}

// -------------------------------------------------------------------------
// 송신 가상화 계층 (순정 BSD 호환 API 적용)
// -------------------------------------------------------------------------
bool wifiWrite(void *p_data, uint32_t length)
{
  if (sock < 0)
    return false;

  // 순정 sendto 명칭 사용
  int err = sendto(sock, p_data, length, 0, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
  return (err >= 0);
}

bool wifiPrintf(const char *fmt, ...)
{
  char    buf[256];
  va_list args;
  int     len;

  va_start(args, fmt);
  len = vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  return wifiWrite(buf, len);
}

const char *wifiGetName(void)
{
  return wifi_nvs.name;
}

const char *wifiGetIPAddress(void)
{
  static char    ip_str[16] = "0.0.0.0";
  struct net_if *iface      = net_if_get_default();

  if (iface != NULL && is_connected == true)
  {
    // DHCP를 통해 정상적으로 IP가 Bound(할당)되었는지 확인 후 변환
    if (iface->config.dhcpv4.state == NET_DHCPV4_BOUND)
    {
      net_addr_ntop(AF_INET, &iface->config.dhcpv4.requested_ip, ip_str, sizeof(ip_str));
    }
  }
  else
  {
    // 연결이 끊어진 경우 버퍼 초기화
    strncpy(ip_str, "0.0.0.0", sizeof(ip_str));
  }

  return ip_str;
}

#if defined(CONFIG_WIFI_ESP32)
/**
 * @brief 현재 연결된 AP의 RSSI(신호 감도)를 반환합니다.
 * @return int8_t RSSI 값 (dBm 단위, 미연결 시 -100)
 */
int8_t wifiGetRssi(void)
{
  if (is_connected == false)
  {
    return -100;
  }

  wifi_ap_record_t ap_info;
  
  if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
  {
    return ap_info.rssi;
  }

  return -100;
}
#else
int8_t wifiGetRssi(void)
{
  return is_connected ? -60 : -100;
}
#endif

/**
 * @brief 1시간 주기로 Wi-Fi ON -> 연결 -> SNTP 동기화 -> Wi-Fi OFF를 반복하는 스레드
 */
void wifiThread(void *p1, void *p2, void *p3)
{
  // 딥슬립 듀티사이클 : 매 웨이크(재부팅)마다 실행된다. 아래 조건에서만 WiFi 를 올려
  // SNTP 동기하고, 그 외에는 즉시 반환해 빠르게 다시 슬립한다.
  //  - 매일 WIFI_SYNC_HOUR 정각 (정기 보정)
  //  - 시각이 아직 설정되지 않았을 때 : 10분에 1회만 재시도(동기 실패해도 전력 폭주 방지)
  struct timespec now;
  struct tm       tm_now;
  clock_gettime(CLOCK_REALTIME, &now);
  localtime_r(&now.tv_sec, &tm_now);

  bool need_sync = (tm_now.tm_hour == WIFI_SYNC_HOUR && tm_now.tm_min == 0) ||
                   (now.tv_sec < WIFI_TIME_VALID_EPOCH && (tm_now.tm_min % 10) == 0);

  if (!need_sync)
  {
    sync_done = true;
    return;
  }

  // 동기할 때만 네트워크 스택 안정화 대기
  k_msleep(1000);

  printk("\n[=== WiFi & SNTP Sync ===]\n");

  is_connected = wifiInitSTA();

  if (is_connected)
  {
    printk("[OK] Wi-Fi Link & SNTP Sync Complete.\n");

    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port   = htons(wifi_nvs.dest_port);
    inet_pton(AF_INET, wifi_nvs.dest_ip, &dest_addr.sin_addr);

    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock >= 0)
    {
      struct timeval timeout = {.tv_sec = 5, .tv_usec = 0};
      setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
      wifiPrintf("ESP32-S3 Synced. SoC: %d%%\n", batteryGetPercent());
      k_msleep(500);
    }
  }
  else
  {
    printk("[E_] Wi-Fi or SNTP Sync failed.\n");
  }

  wifiDisconnectSTA();

  sync_done = true;
}

bool wifiSyncDone(void)
{
  return sync_done;
}

// CLI 인터페이스 제어 셋
void cliCmd(cli_args_t *args)
{
  bool ret = false;

  if (args->argc == 1 && args->isStr(0, "info"))
  {
    cliPrintf("name : %s\n", wifi_nvs.name);
    cliPrintf("ssid : %s\n", wifi_nvs.wifi_ssid);
    cliPrintf("pass : %s\n", wifi_nvs.wifi_pass);
    cliPrintf("ip   : %s\n", wifi_nvs.dest_ip);
    cliPrintf("port : %d\n", wifi_nvs.dest_port);
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "time"))
  {
    // struct timespec ts;
    // clock_gettime(CLOCK_REALTIME, &ts);
    // cliPrintf("Current Epoch Time: %lld\n", (long long)ts.tv_sec);

    while (cliKeepLoop())
    {
      struct timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);

      // Epoch 단위를 구조체 broken-down time(년, 월, 일, 시, 분, 초)으로 변환
      struct tm tmRef;
      localtime_r(&ts.tv_sec, &tmRef); 

      // 디스플레이 포맷팅 (YYYY-MM-DD HH:MM:SS)
      cliPrintf("[%04d-%02d-%02d %02d:%02d:%02d] (Epoch: %lld)\r",
                tmRef.tm_year + 1900,
                tmRef.tm_mon + 1,
                tmRef.tm_mday,
                tmRef.tm_hour,
                tmRef.tm_min,
                tmRef.tm_sec,
                (long long)ts.tv_sec);

      delay(1000);
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "connect"))
  {
    cliPrintf("connecting to '%s' ...\n", wifi_nvs.wifi_ssid);
    is_connected = wifiInitSTA();
    cliPrintf("result : %s\n", is_connected ? "CONNECTED" : "FAILED");
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "sntp"))
  {
    if (wifiIsConnected())
    {
      cliPrintf("Triggering manual SNTP time synchronization...\n");
      syncTimeSNTP();
    }
    else
    {
      cliPrintf("[E_] Wi-Fi is not connected. Connect to AP first.\n");
    }
    ret = true;
  }

  if (args->argc == 1 && args->isStr(0, "reset"))
  {
    sys_reboot(SYS_REBOOT_WARM);
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "name"))
  {
    strncpy(wifi_nvs.name, args->getStr(1), sizeof(wifi_nvs.name) - 1);
    wifiConfigSave();
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "ssid"))
  {
    strncpy(wifi_nvs.wifi_ssid, args->getStr(1), sizeof(wifi_nvs.wifi_ssid) - 1);
    wifiConfigSave();
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "pass"))
  {
    strncpy(wifi_nvs.wifi_pass, args->getStr(1), sizeof(wifi_nvs.wifi_pass) - 1);
    wifiConfigSave();
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "ip"))
  {
    strncpy(wifi_nvs.dest_ip, args->getStr(1), sizeof(wifi_nvs.dest_ip) - 1);
    wifiConfigSave();
    ret = true;
  }

  if (args->argc == 2 && args->isStr(0, "port"))
  {
    wifi_nvs.dest_port = args->getData(1);
    wifiConfigSave();
    ret = true;
  }

  if (ret == false)
  {
    cliPrintf("wifi info\n");
    cliPrintf("wifi reset\n");
    cliPrintf("wifi time\n");
    cliPrintf("wifi connect\n");
    cliPrintf("wifi sntp\n");
    cliPrintf("wifi name [name]\n");
    cliPrintf("wifi ssid [ssid]\n");
    cliPrintf("wifi pass [pass]\n");
    cliPrintf("wifi ip   [ip]\n");
    cliPrintf("wifi port [port]\n");
  }
}