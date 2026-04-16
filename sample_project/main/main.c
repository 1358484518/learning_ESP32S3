#include <stdio.h>
#include "klpgpio.h"
#include "klpws2812.h"
#include "klpcamera.h"
#include "klpwifiscan.h"
#include "klpwifiapsta.h"
#include "klpudp.h"
#include "klptcpclient.h"
#include "klptcpserver.h"
void app_main(void)
{
    // ws2812();
    // klpgpio();
    // klpcamera();
    // klp_wifi_scan();
    // klp_wifi_ap_sta();
    // klpudp();
    // klp_tcp_client();
    klp_tcp_server();
}
