#include "ap_def.h"
#include "driver/cli_net.h"


static bool init(void);
static void cliThread(void const *arg);

MODULE_DEF(cli) 
{
  .name = "cli",
  .priority = MODULE_PRI_LOW,
  .init = init
};

// K_THREAD_DEFINE(cli_thread,
//                 _HW_DEF_RTOS_THREAD_MEM_CLI,
//                 cliThread, NULL, NULL, NULL,
//                 _HW_DEF_RTOS_THREAD_PRI_CLI, 0, 0);
static K_THREAD_STACK_DEFINE(thread_stack, _HW_DEF_RTOS_THREAD_MEM_CLI);
static struct k_thread thread_data;

static uint8_t  cli_ch    = HW_UART_CH_CLI;
static uint32_t cli_baud  = 115200;



bool init(void)
{
  bool ret;

  cliNetInit(23);

  ret = cliOpen(cli_ch, cli_baud);  

  k_tid_t tid = k_thread_create(&thread_data, thread_stack,
                                  K_THREAD_STACK_SIZEOF(thread_stack),
                                  (k_thread_entry_t)cliThread,
                                  NULL, NULL, NULL,
                                  _HW_DEF_RTOS_THREAD_PRI_CLI, 0, K_NO_WAIT);

  ret = tid != NULL ? true:false;                                  
  logPrintf("[%s] climInit()\n", ret ? "OK":"E_");

  return ret;
}

void cliThread(void const *arg)
{
  bool init_ret = true;


  moduleIsReady();

  logPrintf("[%s] Thread Started : CLI\n", init_ret ? "OK":"E_" );
  while(1)
  {
    cliMain();

    cliNetPoll();
    if (cliNetIsConnected())
    {
      cli_ch = HW_UART_CH_NET;
    }
    else if (cli_ch == HW_UART_CH_NET)
    {
      cli_ch = HW_UART_CH_CLI;
    }

    if (uartAvailable(HW_UART_CH_CLI))
    {
      cli_ch = HW_UART_CH_CLI;
    }

    if (cliGetPort() != cli_ch)
    {
      cliOpen(cli_ch, cli_baud);
      logOpen(cli_ch, cli_baud);
    }

    delay(5);
  }
}

