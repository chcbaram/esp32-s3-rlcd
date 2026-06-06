#include "wifi.h"
#include "cli.h"

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
static void cliCmd(cli_args_t *args);
static int  wifiSettingsSet(const char *name, size_t len, settings_read_cb readCb, void *cbArg);
static void wifiMgmtEventHandler(struct net_mgmt_event_callback *cb, uint64_t mgmtEvent, struct net_if *iface);
static void ipMgmtEventHandler(struct net_mgmt_event_callback *cb, uint64_t mgmtEvent, struct net_if *iface);
static void syncTimeSNTP(void);

K_THREAD_STACK_DEFINE(wifi_thread_stack, 2048);
static struct k_thread wifi_thread_data;
static struct k_sem    wifi_connected_sem;
static struct k_sem    ip_obtained_sem;

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback dhcp_cb;

static uint16_t           retry_num    = 0;
static bool               is_connected = false;
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
  net_mgmt_init_event_callback(&wifi_cb, wifiMgmtEventHandler, NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
  net_mgmt_add_event_callback(&wifi_cb);

  net_mgmt_init_event_callback(&dhcp_cb, ipMgmtEventHandler, NET_EVENT_IPV4_DHCP_BOUND);
  net_mgmt_add_event_callback(&dhcp_cb);

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

  if (k_sem_take(&wifi_connected_sem, K_MSEC(15000)) == 0)
  {
    if (k_sem_take(&ip_obtained_sem, K_MSEC(15000)) == 0)
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

void wifiThread(void *p1, void *p2, void *p3)
{
  k_msleep(1000);

  is_connected = wifiInitSTA();

  if (is_connected)
  {
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port   = htons(wifi_nvs.dest_port);          // 순정 htons 사용
    inet_pton(AF_INET, wifi_nvs.dest_ip, &dest_addr.sin_addr); // 순정 inet_pton 사용

    // 순정 socket 생성 명령 사용
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0)
    {
      printk("[E_] Unable to create socket, errno: %d\n", errno);
    }
    else
    {
      struct timeval timeout = {.tv_sec = 10, .tv_usec = 0};                // 순정 struct timeval 사용
      setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)); // 순정 setsockopt 사용
      printk("[OK] Socket created, target -> %s:%d\n", wifi_nvs.dest_ip, wifi_nvs.dest_port);
    }
  }

  while (1)
  {
    k_msleep(1000);
  }
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
    cliPrintf("wifi sntp\n");    
    cliPrintf("wifi name [name]\n");
    cliPrintf("wifi ssid [ssid]\n");
    cliPrintf("wifi pass [pass]\n");
    cliPrintf("wifi ip   [ip]\n");
    cliPrintf("wifi port [port]\n");
  }
}