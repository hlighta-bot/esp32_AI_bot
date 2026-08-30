#include <Arduino.h>
#include <driver/i2s.h>

// ============================================================
// I2S
// ============================================================

#define I2S_PORT I2S_NUM_1

#define I2S_BCLK 16
#define I2S_LRC  17
#define I2S_DIN  15

// ============================================================
// Audio
// ============================================================

#define SAMPLE_RATE 16000
#define BITS_PER_SAMPLE 16

// ============================================================
// Serial
// ============================================================

#define SERIAL_BAUD 921600

// ============================================================
// 数据块
// ============================================================

#define PCM_CHUNK_SIZE 4096

uint8_t pcmBuffer[PCM_CHUNK_SIZE];

// Mono -> Stereo
int16_t stereoBuffer[PCM_CHUNK_SIZE];


// ============================================================
// I2S 初始化
// ============================================================

void setupI2S()
{
    i2s_config_t config =
    {
        .mode =
            (i2s_mode_t)(
                I2S_MODE_MASTER |
                I2S_MODE_TX
            ),

        .sample_rate =
            SAMPLE_RATE,

        .bits_per_sample =
            I2S_BITS_PER_SAMPLE_16BIT,

        .channel_format =
            I2S_CHANNEL_FMT_RIGHT_LEFT,

        .communication_format =
            I2S_COMM_FORMAT_STAND_I2S,

        .intr_alloc_flags =
            ESP_INTR_FLAG_LEVEL1,

        .dma_buf_count =
            8,

        .dma_buf_len =
            256,

        .use_apll =
            false,

        .tx_desc_auto_clear =
            true,

        .fixed_mclk =
            0
    };


    i2s_pin_config_t pins =
    {
        .bck_io_num =
            I2S_BCLK,

        .ws_io_num =
            I2S_LRC,

        .data_out_num =
            I2S_DIN,

        .data_in_num =
            I2S_PIN_NO_CHANGE
    };


    esp_err_t result;


    result =
        i2s_driver_install(
            I2S_PORT,
            &config,
            0,
            NULL
        );


    if (result != ESP_OK)
    {
        Serial.printf(
            "I2S driver failed: %d\n",
            result
        );

        while (true)
        {
            delay(1000);
        }
    }


    result =
        i2s_set_pin(
            I2S_PORT,
            &pins
        );


    if (result != ESP_OK)
    {
        Serial.printf(
            "I2S pin failed: %d\n",
            result
        );

        while (true)
        {
            delay(1000);
        }
    }


    i2s_zero_dma_buffer(
        I2S_PORT
    );
}


// ============================================================
// 接收指定数量的数据
// ============================================================

bool receiveBytes(
    uint8_t *buffer,
    size_t size
)
{
    size_t received = 0;

    while (received < size)
    {
        size_t n =
            Serial.readBytes(
                buffer + received,
                size - received
            );

        if (n > 0)
        {
            received += n;
        }
    }

    return true;
}


// ============================================================
// 接收 uint32 little endian
// ============================================================

uint32_t receiveUint32()
{
    uint8_t b[4];

    receiveBytes(
        b,
        4
    );

    return
        ((uint32_t)b[0]) |
        ((uint32_t)b[1] << 8) |
        ((uint32_t)b[2] << 16) |
        ((uint32_t)b[3] << 24);
}


// ============================================================
// 播放一个 PCM 数据块
// ============================================================

void playChunk(
    uint8_t *data,
    size_t bytes
)
{
    int16_t *mono =
        (int16_t *)data;

    size_t samples =
        bytes / 2;


    // Mono -> Stereo

    for (
        size_t i = 0;
        i < samples;
        i++
    )
    {
        stereoBuffer[i * 2] =
            mono[i];

        stereoBuffer[i * 2 + 1] =
            mono[i];
    }


    size_t bytesToWrite =
        samples * 4;


    size_t bytesWritten;


    i2s_write(
        I2S_PORT,
        stereoBuffer,
        bytesToWrite,
        &bytesWritten,
        portMAX_DELAY
    );
}


// ============================================================
// 播放 PCM
// ============================================================

void playPCM(
    uint32_t dataSize
)
{
    uint32_t remaining =
        dataSize;


    while (remaining > 0)
    {
        size_t chunkSize =
            min(
                (uint32_t)PCM_CHUNK_SIZE,
                remaining
            );


        // 16 bit PCM 必须偶数

        if (chunkSize & 1)
        {
            chunkSize--;
        }


        // 接收数据

        receiveBytes(
            pcmBuffer,
            chunkSize
        );


        // 播放

        playChunk(
            pcmBuffer,
            chunkSize
        );


        remaining -=
            chunkSize;


        // ====================================================
        // 非常重要：
        //
        // 播放过程中只发送 ACK
        //
        // 不发送任何文字
        // ====================================================

        Serial.write(
            'A'
        );

        Serial.write(
            'C'
        );

        Serial.write(
            'K'
        );

        Serial.flush();
    }
}


// ============================================================
// setup
// ============================================================

void setup()
{
    Serial.begin(
        SERIAL_BAUD
    );

    delay(1000);


    Serial.println();
    Serial.println(
        "================================"
    );

    Serial.println(
        "ESP32-S3 WAV AUDIO PLAYER"
    );

    Serial.println(
        "================================"
    );

    Serial.printf(
        "Sample Rate: %d Hz\n",
        SAMPLE_RATE
    );

    Serial.println(
        "Bits: 16"
    );

    Serial.println(
        "Channels: Mono input"
    );


    setupI2S();


    Serial.println(
        "I2S initialized."
    );

    Serial.println(
        "READY"
    );
}


// ============================================================
// loop
// ============================================================

void loop()
{
    // 等待 PLAY

    if (Serial.available() >= 4)
    {
        char command[4];


        Serial.readBytes(
            command,
            4
        );


        if (
            command[0] == 'P' &&
            command[1] == 'L' &&
            command[2] == 'A' &&
            command[3] == 'Y'
        )
        {
            // 接收 PCM 长度

            uint32_t dataSize =
                receiveUint32();


            // 清理可能残留的数据
            //
            // 这里不能清理，因为紧接着就是 PCM。
            //
            // 所以什么都不做。


            playPCM(
                dataSize
            );
        }
    }


    delay(1);
}