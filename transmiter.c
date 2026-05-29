#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"

#define IR_TX_GPIO 4
#define FREQUENCIA 38000

static rmt_channel_handle_t tx_channel = NULL;
static rmt_encoder_handle_t copy_encoder = NULL;

void ir_init(void)
{
    rmt_tx_channel_config_t tx_chan_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .gpio_num = IR_TX_GPIO,
        .mem_block_symbols = 64,
        .resolution_hz = 1000000, // 1 MHz
        .trans_queue_depth = 4,
    };

    ESP_ERROR_CHECK(
        rmt_new_tx_channel(&tx_chan_config, &tx_channel)
    );

    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = FREQUENCIA,
        .duty_cycle = 0.33,
    };

    ESP_ERROR_CHECK(
        rmt_apply_carrier(tx_channel, &carrier_cfg)
    );

    rmt_copy_encoder_config_t encoder_config = {
    };

    ESP_ERROR_CHECK(
        rmt_new_copy_encoder(
            &encoder_config,
            &copy_encoder
        )
    );

    ESP_ERROR_CHECK(
        rmt_enable(tx_channel)
    );
}

void ir_send_raw(void)
{
    rmt_symbol_word_t symbols[] = {

        // header
        {
            .level0 = 1,
            .duration0 = 9000,
            .level1 = 0,
            .duration1 = 4500
        },

        // bit exemplo
        {
            .level0 = 1,
            .duration0 = 560,
            .level1 = 0,
            .duration1 = 560
        },

        {
            .level0 = 1,
            .duration0 = 560,
            .level1 = 0,
            .duration1 = 1690
        },
    };

    rmt_transmit_config_t tx_config = {
        .loop_count = 0,
    };

    ESP_ERROR_CHECK(
        rmt_transmit(
            tx_channel,
            copy_encoder,
            symbols,
            sizeof(symbols),
            &tx_config
        )
    );

    ESP_ERROR_CHECK(
        rmt_tx_wait_all_done(
            tx_channel,
            portMAX_DELAY
        )
    );
}

void app_main(void)
{
    ir_init();

    vTaskDelay(pdMS_TO_TICKS(1000));

    printf("Enviando IR...\n");

    ir_send_raw();
}

/*CHAT INVENTOU*/
rmt_symbol_word_t symbols[] = {

    // Header NEC
    {
        .level0 = 1,
        .duration0 = 9000,
        .level1 = 0,
        .duration1 = 4500
    },

    // 32 bits exemplo NEC
    // endereço + comando

    // bit 0
    {1,560,0,560},

    // bit 1
    {1,560,0,1690},

    // bit 0
    {1,560,0,560},

    // bit 1
    {1,560,0,1690},

    // bit 0
    {1,560,0,560},

    // bit 1
    {1,560,0,1690},

    // bit 0
    {1,560,0,560},

    // bit 1
    {1,560,0,1690},

    // byte 2
    {1,560,0,560},
    {1,560,0,560},
    {1,560,0,1690},
    {1,560,0,560},
    {1,560,0,1690},
    {1,560,0,1690},
    {1,560,0,560},
    {1,560,0,1690},

    // byte 3
    {1,560,0,560},
    {1,560,0,1690},
    {1,560,0,560},
    {1,560,0,560},
    {1,560,0,1690},
    {1,560,0,560},
    {1,560,0,1690},
    {1,560,0,560},

    // byte 4
    {1,560,0,1690},
    {1,560,0,560},
    {1,560,0,1690},
    {1,560,0,560},
    {1,560,0,560},
    {1,560,0,1690},
    {1,560,0,560},
    {1,560,0,1690},

    // Stop bit
    {
        .level0 = 1,
        .duration0 = 560,
        .level1 = 0,
        .duration1 = 0
    }
};