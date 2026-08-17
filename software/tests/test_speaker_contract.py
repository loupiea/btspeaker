from pathlib import Path
import unittest


PROJECT = Path(__file__).resolve().parents[1]
HEADER = PROJECT / "bsp" / "include" / "bsp_speaker.h"
SOURCE = PROJECT / "bsp" / "src" / "bsp_speaker.c"
COMPONENT_CMAKE = PROJECT / "bsp" / "CMakeLists.txt"
MAIN = PROJECT / "main" / "main.c"


class SpeakerContractTest(unittest.TestCase):
    def test_public_api_exists(self):
        text = HEADER.read_text(encoding="utf-8")
        self.assertIn("esp_err_t bsp_speaker_init(void);", text)
        self.assertNotIn("bsp_speaker_run_self_test", text)

    def test_driver_uses_i2s0_and_default_volume_5_of_50(self):
        text = SOURCE.read_text(encoding="utf-8")
        self.assertIn("BSP_SPEAKER_DEFAULT_VOLUME = 5", text)
        self.assertIn("SPEAKER_MAX_VOLUME = 50", text)
        self.assertIn("I2S_NUM_0", text)
        self.assertIn("I2S_ROLE_MASTER", text)
        self.assertIn("I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG", text)
        self.assertIn("BSP_AMP_BCLK_GPIO", text)
        self.assertIn("BSP_AMP_LRCLK_GPIO", text)
        self.assertIn("BSP_AMP_DOUT_GPIO", text)
        self.assertIn("BSP_AMP_SD_GPIO", text)
        self.assertIn("gpio_set_level(BSP_AMP_SD_GPIO, 1)", text)
        self.assertIn("i2s_channel_write", text)
        self.assertNotIn("SPEAKER_TEST_TONE_HZ", text)
        self.assertNotIn("SPEAKER_TEST_VOLUME", text)
        self.assertNotIn("SPEAKER_TEST_DURATION_MS", text)

    def test_driver_sends_16bit_pcm_in_32bit_i2s_slots(self):
        text = SOURCE.read_text(encoding="utf-8")
        self.assertIn("I2S_DATA_BIT_WIDTH_32BIT", text)
        self.assertIn("int32_t scaled_samples", text)
        self.assertIn("<< 16", text)
        self.assertIn("sizeof(scaled_samples[0])", text)

    def test_driver_logs_first_speaker_write_for_audio_debug(self):
        text = SOURCE.read_text(encoding="utf-8")
        self.assertIn("s_write_log_count", text)
        self.assertIn("Speaker write:", text)
        self.assertIn("sample_peak", text)
        self.assertIn("scaled_peak", text)
        self.assertIn("amp_sd=%d", text)
        self.assertIn("bytes_written=%u", text)

    def test_driver_reconfigures_amp_sd_when_starting_playback(self):
        text = SOURCE.read_text(encoding="utf-8")
        self.assertIn("speaker_configure_amp_sd", text)
        self.assertIn("speaker_enable_amplifier", text)
        self.assertIn("gpio_config(&amp_sd_config)", text)
        self.assertIn("gpio_set_level(BSP_AMP_SD_GPIO, 1)", text)
        self.assertIn("Amplifier enabled: SD=%d", text)

    def test_component_compiles_driver(self):
        text = COMPONENT_CMAKE.read_text(encoding="utf-8")
        self.assertIn('"src/bsp_speaker.c"', text)
        self.assertIn("esp_driver_i2s", text)

    def test_main_does_not_run_speaker_test(self):
        text = MAIN.read_text(encoding="utf-8")
        self.assertNotIn("bsp_speaker_run_self_test()", text)
        self.assertNotIn("Speaker self-test failed", text)


if __name__ == "__main__":
    unittest.main()
