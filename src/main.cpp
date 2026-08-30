#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

#define I2S_PORT I2S_NUM_1

#define I2S_BCLK 16
#define I2S_LRC  17
#define I2S_DIN  15

#define SAMPLE_RATE 16000
#define TEST_FREQ   440

#define BUFFER_SAMPLES 512

int16_t buffer[BUFFER_SAMPLES * 2];

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("================================");
    Serial.println("ESP32-S3 I2S AUDIO TEST");
    Serial.println("================================");

    i2s_config_t config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pins = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRC,
        .data_out_num = I2S_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    esp_err_t result = i2s_driver_install(
        I2S_PORT,
        &config,
        0,
        NULL
    );

    if (result != ESP_OK) {
        Serial.printf("I2S driver failed: %d\n", result);
        return;
    }

    result = i2s_set_pin(I2S_PORT, &pins);

    if (result != ESP_OK) {
        Serial.printf("I2S pin failed: %d\n", result);
        return;
    }

    i2s_zero_dma_buffer(I2S_PORT);

    Serial.println("I2S driver initialized");
    Serial.println("GPIO15 = DIN");
    Serial.println("GPIO16 = BCLK");
    Serial.println("GPIO17 = LRC");
    Serial.println("Playing 440Hz...");
}

void loop() {

    
    static float phase = 0.0f;

    const float phaseIncrement =
        2.0f * PI * TEST_FREQ / SAMPLE_RATE;

    // 左右声道交错
    for (int i = 0; i < BUFFER_SAMPLES; i++) {

        int16_t sample =
            (int16_t)(16000.0f * sinf(phase));

        phase += phaseIncrement;

        if (phase >= 2.0f * PI) {
            phase -= 2.0f * PI;
        }

        // Left
        buffer[i * 2] = sample;

        // Right
        buffer[i * 2 + 1] = sample;
    }

    size_t bytesWritten;

    i2s_write(
        I2S_PORT,
        buffer,
        sizeof(buffer),
        &bytesWritten,
        portMAX_DELAY
    );
}