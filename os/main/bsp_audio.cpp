#include "bsp_audio.h"
#include "bsp_43c.h"
#include "bsp_display.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "bsp_audio";

/* Vendor macro (speaker_microphone.h): one std I2S config serving both the
 * TX (ES8311) and RX (ES7210) channels. Slots stay stereo; the codec devices
 * pick their channel counts on open. */
#define BSP_I2S_DUPLEX_CFG(_rate)                                                              \
    {                                                                                          \
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(_rate),                                          \
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,               \
                                                      I2S_SLOT_MODE_STEREO),                   \
        .gpio_cfg = {.mclk = BSP_I2S_MCLK, .bclk = BSP_I2S_BCLK, .ws = BSP_I2S_WS,             \
                     .dout = BSP_I2S_DOUT, .din = BSP_I2S_DIN},                                \
    }

static esp_codec_dev_handle_t s_spk_dev;  /* ES8311 output */
static esp_codec_dev_handle_t s_mic_dev;  /* ES7210 input */
static bool s_ready;
static int s_volume = 70;

esp_err_t bsp_audio_pa_enable(bool on) {
    return bsp_board_ch422g_set_bit(BSP_CH422G_BIT_PA, on);
}

bool bsp_audio_ready(void) {
    return s_ready;
}

esp_err_t bsp_audio_init(void) {
    if (s_ready) return ESP_OK;

    /* One full-duplex I2S channel pair at 16 kHz mono: TX feeds the ES8311,
     * RX captures the ES7210 (vendor 11_speaker_microphone structure). */
    i2s_chan_config_t chan_cfg =
        I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)BSP_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    i2s_chan_handle_t tx_chan = NULL, rx_chan = NULL;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx_chan, &rx_chan), TAG, "i2s chan");

    const i2s_std_config_t std_cfg = BSP_I2S_DUPLEX_CFG(BSP_AUDIO_SAMPLE_RATE);
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx_chan, &std_cfg), TAG, "i2s tx init");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(rx_chan, &std_cfg), TAG, "i2s rx init");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx_chan), TAG, "i2s tx enable");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(rx_chan), TAG, "i2s rx enable");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = BSP_I2S_PORT,
        .rx_handle = rx_chan,
        .tx_handle = tx_chan,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data_if != NULL, ESP_FAIL, TAG, "i2s data if");

    /* ES8311: speaker DAC. PA is on CH422G IO3, not a codec GPIO. */
    audio_codec_i2c_cfg_t spk_i2c_cfg = {
        .port = 0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bsp_display_i2c_bus(),
    };
    const audio_codec_ctrl_if_t *spk_ctrl = audio_codec_new_i2c_ctrl(&spk_i2c_cfg);
    ESP_RETURN_ON_FALSE(spk_ctrl != NULL, ESP_FAIL, TAG, "es8311 ctrl");
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    es8311_codec_cfg_t es8311_cfg = {};
    es8311_cfg.ctrl_if = spk_ctrl;
    es8311_cfg.gpio_if = gpio_if;
    es8311_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC;
    es8311_cfg.pa_pin = GPIO_NUM_NC;
    es8311_cfg.use_mclk = true;
    es8311_cfg.hw_gain.pa_voltage = 5.0;
    es8311_cfg.hw_gain.codec_dac_voltage = 3.3;
    const audio_codec_if_t *spk_codec = es8311_codec_new(&es8311_cfg);
    ESP_RETURN_ON_FALSE(spk_codec != NULL, ESP_FAIL, TAG, "es8311 codec");

    esp_codec_dev_cfg_t spk_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = spk_codec,
        .data_if = data_if,
    };
    s_spk_dev = esp_codec_dev_new(&spk_dev_cfg);
    ESP_RETURN_ON_FALSE(s_spk_dev != NULL, ESP_FAIL, TAG, "es8311 dev");

    /* ES7210: microphone ADC, both array mics selected (vendor default). */
    audio_codec_i2c_cfg_t mic_i2c_cfg = {
        .port = 0,
        .addr = ES7210_CODEC_DEFAULT_ADDR,
        .bus_handle = bsp_display_i2c_bus(),
    };
    const audio_codec_ctrl_if_t *mic_ctrl = audio_codec_new_i2c_ctrl(&mic_i2c_cfg);
    ESP_RETURN_ON_FALSE(mic_ctrl != NULL, ESP_FAIL, TAG, "es7210 ctrl");
    es7210_codec_cfg_t es7210_cfg = {};
    es7210_cfg.ctrl_if = mic_ctrl;
    es7210_cfg.mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2;
    const audio_codec_if_t *mic_codec = es7210_codec_new(&es7210_cfg);
    ESP_RETURN_ON_FALSE(mic_codec != NULL, ESP_FAIL, TAG, "es7210 codec");

    esp_codec_dev_cfg_t mic_dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = mic_codec,
        .data_if = data_if,
    };
    s_mic_dev = esp_codec_dev_new(&mic_dev_cfg);
    ESP_RETURN_ON_FALSE(s_mic_dev != NULL, ESP_FAIL, TAG, "es7210 dev");

    esp_codec_dev_sample_info_t fs_out = {
        .bits_per_sample = 16,
        .channel = 1,
        .sample_rate = BSP_AUDIO_SAMPLE_RATE,
    };
    /* ES7210 captures the stereo mic pair; the query is downmixed to mono. */
    esp_codec_dev_sample_info_t fs_in = {
        .bits_per_sample = 16,
        .channel = 2,
        .sample_rate = BSP_AUDIO_SAMPLE_RATE,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_spk_dev, &fs_out), TAG, "spk open");
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_mic_dev, &fs_in), TAG, "mic open");
    esp_codec_dev_set_out_vol(s_spk_dev, s_volume);
    esp_codec_dev_set_in_gain(s_mic_dev, 24.0);

    s_ready = true;
    ESP_LOGI(TAG, "audio ready (16k/16bit/mono, ES8311+ES7210)");
    return ESP_OK;
}

esp_err_t bsp_audio_record(uint8_t *buf, size_t bytes, uint32_t timeout_ms) {
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    int ret = esp_codec_dev_read(s_mic_dev, buf, bytes);
    (void)timeout_ms; /* esp_codec_dev_read blocks; timeout handled by caller watchdog */
    return ret == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_audio_play(const uint8_t *buf, size_t bytes) {
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (esp_codec_dev_write(s_spk_dev, (void *)buf, bytes) != ESP_CODEC_DEV_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t bsp_audio_set_volume(int volume) {
    if (volume < 0) volume = 0;
    if (volume > 100) volume = 100;
    s_volume = volume;
    if (!s_ready) return ESP_OK;
    return esp_codec_dev_set_out_vol(s_spk_dev, s_volume) == ESP_CODEC_DEV_OK
               ? ESP_OK
               : ESP_FAIL;
}

esp_err_t bsp_audio_set_mic_gain(float gain) {
    if (!s_ready) return ESP_OK;
    return esp_codec_dev_set_in_gain(s_mic_dev, gain) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}
