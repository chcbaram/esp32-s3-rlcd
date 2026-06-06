#include "cli_net.h"
#include "uart.h"

#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/sys/select.h>


// telnet 명령 바이트
#define TN_IAC      255
#define TN_DONT     254
#define TN_DO       253
#define TN_WONT     252
#define TN_WILL     251
#define TN_SB       250
#define TN_SE       240
#define TN_OPT_ECHO 1
#define TN_OPT_SGA  3

enum
{
  IAC_NORMAL,
  IAC_CMD,    // IAC 직후 (명령 바이트 대기)
  IAC_OPT,    // WILL/WONT/DO/DONT 의 옵션 바이트 대기
  IAC_SB,     // subnegotiation 데이터 (IAC SE 까지)
  IAC_SB_IAC, // SB 안에서 IAC 만남
};

static uint16_t net_port    = 23;
static int      listen_sock = -1;
static int      client_sock = -1;

static uint8_t  rx_buf[256];
static uint16_t rx_len    = 0;
static uint16_t rx_idx    = 0;
static uint8_t  iac_state = IAC_NORMAL;
static bool     prev_cr   = false; // 직전 수신 바이트가 CR 인지 (CRLF/CRNUL 정규화용)
static uint8_t  tx_prev   = 0;     // 직전 송신 바이트 (LF→CRLF 변환 시 중복 CR 방지)

static uart_driver_t net_drv;

static bool     _open(uint32_t baud);
static bool     _close(void);
static uint32_t _available(void);
static bool     _flush(void);
static uint8_t  _read(void);
static uint32_t _write(uint8_t *p_data, uint32_t length);
static bool     sockReadable(int sock);


bool cliNetInit(uint16_t port)
{
  net_port    = port;
  listen_sock = -1;
  client_sock = -1;

  net_drv.open      = _open;
  net_drv.close     = _close;
  net_drv.available = _available;
  net_drv.flush     = _flush;
  net_drv.read      = _read;
  net_drv.write     = _write;

  uartSetDriver(HW_UART_CH_NET, &net_drv);
  
  return true;
}

void cliNetPoll(void)
{
  // 네트워크 인터페이스 준비 후 listen 소켓 1회 지연 생성
  if (listen_sock < 0)
  {
    struct sockaddr_in addr;
    int                opt = 1;

    // Zephyr 커널 인터페이스 가동 상태 확인
    struct net_if *iface = net_if_get_default();
    if (!iface || !net_if_is_up(iface))
      return;

    listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock < 0)
      return;

    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(net_port);

    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(listen_sock, 1) < 0)
    {
      close(listen_sock);
      listen_sock = -1;
      return;
    }

    printk("[OK] cliNetListen() port %d\n", net_port);
  }

  // client 없을 때만 논블로킹 accept
  if (client_sock < 0 && sockReadable(listen_sock))
  {
    int fd = accept(listen_sock, NULL, NULL);
    if (fd >= 0)
    {
      // char 모드 협상: 서버가 echo/ SGA 를 맡아 클라가 char 단위로 키를 보내게 한다.
      static const uint8_t nego[] = {
        TN_IAC,
        TN_WILL,
        TN_OPT_ECHO,
        TN_IAC,
        TN_WILL,
        TN_OPT_SGA,
      };

      client_sock = fd;
      rx_len      = 0;
      rx_idx      = 0;
      iac_state   = IAC_NORMAL;
      prev_cr     = false;
      tx_prev     = 0;

      // Zephyr POSIX 표준 레이어 아래서는 MSG_DONTWAIT 명칭 그대로 사용 가능합니다.
      send(client_sock, nego, sizeof(nego), MSG_DONTWAIT);
      printk("[  ] cliNet client connected\n");

      int opt_on = 1;
      if (setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, &opt_on, sizeof(opt_on)) < 0)
      {
        printk("[E_] Failed to set TCP_NODELAY (errno: %d)\n", errno);
      }
      else
      {
        printk("[  ] TCP_NODELAY enabled for faster response.\n");
      }

      int snd_buf_size = 4096; // 4KB 확보
      setsockopt(client_sock, SOL_SOCKET, SO_SNDBUF, &snd_buf_size, sizeof(snd_buf_size));      
    }
  }
}

bool cliNetIsConnected(void)
{
  return client_sock >= 0;
}

static bool sockReadable(int sock)
{
  fd_set         fds;
  struct timeval tv;

  FD_ZERO(&fds);
  FD_SET(sock, &fds);
  tv.tv_sec  = 0;
  tv.tv_usec = 0;

  return (select(sock + 1, &fds, NULL, NULL, &tv) > 0 && FD_ISSET(sock, &fds));
}

static void clientClose(void)
{
  if (client_sock >= 0)
  {
    close(client_sock);
    client_sock = -1;
  }
  rx_len    = 0;
  rx_idx    = 0;
  iac_state = IAC_NORMAL;
  prev_cr   = false;
  tx_prev   = 0;
}

// 줄바꿈 정규화: CLI 코어는 Enter 를 CR(0x0D)로만 인식한다.
static void pushByte(uint8_t b)
{
  if (b == '\r')
  {
    if (rx_len < sizeof(rx_buf))
      rx_buf[rx_len++] = '\r';
    prev_cr = true;
    return;
  }

  if (b == '\n')
  {
    if (prev_cr)                 // CR LF → LF 버림(CR 은 이미 보냄)
    {
      prev_cr = false;
      return;
    }
    if (rx_len < sizeof(rx_buf)) // LF 단독 → CR 로
      rx_buf[rx_len++] = '\r';
    return;
  }

  if (b == 0x00)                 // CR NUL 의 NUL(또는 잡 NUL) → 버림
  {
    prev_cr = false;
    return;
  }

  // 대부분 터미널의 Backspace 키는 DEL(0x7F)을 보낸다. 이를 BACK(0x08)으로 매핑
  if (b == 0x7F)
    b = 0x08;

  prev_cr = false;
  if (rx_len < sizeof(rx_buf))
    rx_buf[rx_len++] = b;
}

bool _open(uint32_t baud)
{
  (void)baud;
  return true;
}

bool _close(void)
{
  return true;
}

uint32_t _available(void)
{
  uint8_t raw[256];
  int     n;

  if (client_sock < 0)
    return 0;

  if (rx_idx < rx_len)
    return rx_len - rx_idx;

  if (!sockReadable(client_sock))
    return 0;

  n = recv(client_sock, raw, sizeof(raw), 0);
  if (n == 0)
  {
    clientClose();
    return 0;
  }
  if (n < 0)
  {
    if (errno == EWOULDBLOCK || errno == EAGAIN)
      return 0;
    clientClose();
    return 0;
  }

  // telnet IAC 시퀀스를 제거하며 실제 데이터만 rx_buf 로
  rx_len = 0;
  rx_idx = 0;
  for (int i = 0; i < n; i++)
  {
    uint8_t b = raw[i];

    switch (iac_state)
    {
      case IAC_NORMAL:
        if (b == TN_IAC)
          iac_state = IAC_CMD;
        else
          pushByte(b);
        break;

      case IAC_CMD:
        if (b == TN_IAC)
        {
          pushByte(TN_IAC);
          iac_state = IAC_NORMAL;
        }
        else if (b == TN_WILL || b == TN_WONT || b == TN_DO || b == TN_DONT)
          iac_state = IAC_OPT;
        else if (b == TN_SB)
          iac_state = IAC_SB;
        else
          iac_state = IAC_NORMAL;
        break;

      case IAC_OPT:
        iac_state = IAC_NORMAL;
        break;

      case IAC_SB:
        if (b == TN_IAC)
          iac_state = IAC_SB_IAC;
        break;

      case IAC_SB_IAC:
        iac_state = (b == TN_SE) ? IAC_NORMAL : IAC_SB;
        break;
    }
  }

  return rx_len;
}

bool _flush(void)
{
  rx_len = 0;
  rx_idx = 0;
  return true;
}

uint8_t _read(void)
{
  if (rx_idx < rx_len)
    return rx_buf[rx_idx++];

  return 0;
}

static bool sendAll(const uint8_t *p, uint32_t n)
{
  uint32_t off = 0;

  while (off < n)
  {
    int s = send(client_sock, p + off, n - off, 0);
    if (s <= 0)
    {
      if (s < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
        continue;
      return false;
    }
    off += (uint32_t)s;
  }
  return true;
}

uint32_t _write(uint8_t *p_data, uint32_t length)
{
  uint8_t  out[256];
  uint32_t oi = 0;

  if (client_sock < 0)
    return 0;

  for (uint32_t i = 0; i < length; i++)
  {
    uint8_t c = p_data[i];

    if (c == '\n' && tx_prev != '\r')
    {
      out[oi++] = '\r';
      if (oi >= sizeof(out))
      {
        if (!sendAll(out, oi))
        {
          clientClose();
          return 0;
        }
        oi = 0;
      }
    }

    out[oi++] = c;
    tx_prev   = c;

    if (oi >= sizeof(out))
    {
      if (!sendAll(out, oi))
      {
        clientClose();
        return 0;
      }
      oi = 0;
    }
  }

  if (oi > 0 && !sendAll(out, oi))
  {
    clientClose();
    return 0;
  }

  return length;
}


