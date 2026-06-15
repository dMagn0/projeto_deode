#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "freertos/timers.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_encoder.h"
#include <inttypes.h>
#include <string.h>

#include <stdbool.h>
#include <stddef.h>

static volatile bool rx_done = false;
static volatile size_t rx_num_symbols = 0;

#define PRESENCA 35 
#define CORRENTE 34 
#define INFRAVERMELHO 27 

#define INTERRUPT_1 21
#define INTERRUPT_2 19

#define LIMIAR_TENSAO 80
#define TEMPO_DE_ESPERA 10000
#define TEMPO_DE_ESPERA_DO_AR 14000

#define DEBUG 1

#define IR_TX_GPIO 4
#define FREQUENCIA 38000
#define IR_RX_GPIO 5
#define RX_BUF_SIZE 1024



static rmt_channel_handle_t rx_channel = NULL;
// static rmt_symbol_word_t raw_symbols[RX_BUF_SIZE];
static rmt_channel_handle_t tx_channel = NULL;
static rmt_encoder_handle_t copy_encoder = NULL;
static TimerHandle_t timer_de_presenca, timer_do_ar;

void comuta_chave(){
    static int inter_1=1, inter_2=0;

    inter_1 = !inter_1;
    inter_2 = !inter_2;
    
    gpio_set_level(INTERRUPT_1,inter_1);
    gpio_set_level(INTERRUPT_2,inter_2);
}

int desligou_recente = 0;
static void IRAM_ATTR detecta_presenca(void *arg){
    
    // printf(".\n");
    // if(desligou_recente){
    //     desligou_recente = 0;
    //     comuta_chave();
    // }
    xTimerStartFromISR(timer_de_presenca, 0);
    xTimerStartFromISR(timer_do_ar, 0);
    
}

int le_corrente(){
    int raw=0, media_corrente=0, lower=0, upper=0;
    raw = 0;
    media_corrente = 0;

    for(int i=0; i < 50; i++)
    {
        TickType_t ticks_o = xTaskGetTickCount();
        raw = adc1_get_raw(ADC1_CHANNEL_6);
        if(i == 0){
            lower = raw;
            upper = raw;
        }else if (raw<lower){
            lower = raw;
        }else if(raw>upper){
            upper = raw;
        }
        media_corrente += raw;

        TickType_t ticks_f = xTaskGetTickCount();
        uint32_t ms = (ticks_f- ticks_o) * portTICK_PERIOD_MS;
        if(ms<20){
            vTaskDelay(pdMS_TO_TICKS(20-ms));
        }else if(DEBUG){
            printf("\ntempo da conversao: %ld\n", ms);
        }
            
    }

    media_corrente = media_corrente/50;

    if(DEBUG){
        printf("media medida: %d\n", media_corrente);
        printf("amplitude maxima (%d,%d): %d\n", upper,lower,(upper-lower));
    }

    if((upper-lower) >= LIMIAR_TENSAO){
        return 1;
    } 
    return 0;
}

static void desliga_lampada(TimerHandle_t xTimer){

    if(le_corrente()){
        comuta_chave();
        desligou_recente = 1;
    }
    printf("asdasd\n");


}

void ir_send_raw(void){

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

static void desliga_ar(TimerHandle_t xTimer){
    desligou_recente = 0;

    // ir_send_raw();
}

static bool rx_done_callback(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *user_ctx){
    /*printf("Recebidos %u símbolos\n",
           (unsigned)edata->num_symbols);

    for (size_t i = 0; i < edata->num_symbols; i++) {
        rmt_symbol_word_t s = edata->received_symbols[i];

        printf(
            "[%03u] D0=%u D1=%u\n",
            (unsigned)i,
            s.duration0,
            s.duration1
        );
    }*/
    rx_done = true;

    return false;
}

void ir_rx_init(void){

    rmt_rx_channel_config_t rx_cfg = {
        .gpio_num = IR_RX_GPIO,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 5000000, // 1 us por tick
        .mem_block_symbols = 128,
    };

    ESP_ERROR_CHECK(
        rmt_new_rx_channel(&rx_cfg, &rx_channel)
    );

    ESP_ERROR_CHECK(
        rmt_enable(rx_channel)
    );
    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rx_done_callback,
    };

    ESP_ERROR_CHECK(
        rmt_rx_register_event_callbacks(
            rx_channel,
            &cbs,
            NULL
        )
    );
}

#define MAX_SYMBOLS 2048

static rmt_symbol_word_t raw_symbols[MAX_SYMBOLS];
static rmt_symbol_word_t full_buffer[MAX_SYMBOLS * 4];

static size_t full_index = 0;
static inline int decode_bit(uint32_t duration_us)
{
    if (duration_us > 5000) return 1;
    return 0;
}
void ir_receive_once(void)
{
    rmt_receive_config_t receive_cfg = {
        .signal_range_min_ns = 500,
        .signal_range_max_ns = 13000000,
    };

    full_index = 0;

    printf("Aguardando sinal IR...\n");

    for (int frame = 0; frame < 2; frame++) {

        rx_done = false;

        ESP_ERROR_CHECK(
            rmt_receive(
                rx_channel,
                raw_symbols,
                sizeof(raw_symbols),
                &receive_cfg
            )
        );

        // espera terminar recepção
        while (!rx_done) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        // concatena chunk recebido
        for (int i = 0; i < RX_BUF_SIZE; i++) {

            if (raw_symbols[i].duration0 == 0 &&
                raw_symbols[i].duration1 == 0) {
                break;
            }

            if (full_index < MAX_SYMBOLS * 4) {
                full_buffer[full_index++] = raw_symbols[i];
            }
        }

        // printf("Frame %d recebido. total acumulado=%d\n",
            //    frame, full_index);

        // vTaskDelay(pdMS_TO_TICKS(50)); // pequena pausa entre frames
    }


    // ===== DECODIFICA EM BINÁRIO =====
    printf("\n=== BINARIO ===\n");

    for (int i = 1; i < full_index; i++) {

        uint32_t d0 = full_buffer[i].duration0;
        uint32_t d1 = full_buffer[i].duration1;

        // escolhe o maior (normal em RMT)
        uint32_t pulse = (d0 > d1) ? d0 : d1;

        int bit = decode_bit(pulse);

        printf("%d", bit);

        // quebra linha a cada 8 bits
        if ((i % 8) == 0) printf(" ");
    }

    printf("\n");
}

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


/*static void loop(void *pvParameters){
    while(1){
        printf("sensor: %d\n", gpio_get_level(PRESENCA));
        // if( le_corrente()){
        //     comuta_chave();
        // }        
        vTaskDelay(pdMS_TO_TICKS(1000));
        // comuta_chave();
        // vTaskDelay(pdMS_TO_TICKS(1000));
        // le_corrente();
        // comuta_chave();
        // vTaskDelay(pdMS_TO_TICKS(1000));
        // le_corrente();
        // comuta_chave();
        // vTaskDelay(pdMS_TO_TICKS(1000));
    }

}*/

void app_main(void){

    timer_de_presenca = xTimerCreate("Temporizador", pdMS_TO_TICKS(2000), pdFALSE, NULL, desliga_lampada);
    timer_do_ar = xTimerCreate("Temporizador", pdMS_TO_TICKS(TEMPO_DE_ESPERA_DO_AR), pdFALSE, NULL, desliga_ar);

    ir_rx_init();

    while (1)
    {
        ir_receive_once();
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    // while(1){

    // printf("%d\n", gpio_get_level(IR_RX_GPIO));
    // vTaskDelay(pdMS_TO_TICKS(10));
    // }
    // Configuração do ADC
    // adc1_config_width(ADC_WIDTH_BIT_12); // resolução: 0–4095
    // adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11); 
    // CHANNEL_6 = GPIO34
    // gpio_set_direction(PRESENCA, GPIO_MODE_INPUT);


    // gpio_set_direction(INTERRUPT_1, GPIO_MODE_OUTPUT);
    // gpio_set_direction(INTERRUPT_2, GPIO_MODE_OUTPUT);
    // gpio_set_level(INTERRUPT_1,1);
    // gpio_set_level(INTERRUPT_2,0);
    
    gpio_set_pull_mode(GPIO_NUM_15, GPIO_PULLDOWN_ONLY);
    gpio_set_direction(PRESENCA, GPIO_MODE_INPUT);
    gpio_set_intr_type(PRESENCA, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PRESENCA,detecta_presenca,NULL);

    // xTaskCreate(loop, "Leitor de corrente", 2048, NULL, 2, NULL);

}











