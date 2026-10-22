#include <cstdio>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" void app_main()
{
    while (true)
    {
        printf("Hello World from iotpp analyzer!\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}