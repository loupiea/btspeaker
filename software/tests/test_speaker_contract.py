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
        self.assertIn("esp_err_t bsp_speaker_run_self_test(void);", text)

    def test_driver_uses_i2s0_and_default_volume_5_of_50(self):
        text = SOURCE.read_text(encoding="utf-8")
        self.assertIn("BSP_SPEAKER_DEFAULT_VOLUME = 5", text)
        self.assertIn("SPEAKER_MAX_VOLUME = 50", text)
        self.assertIn("volume=%d/50", text)
        self.assertIn("I2S_NUM_0", text)
        self.assertIn("I2S_ROLE_MASTER", text)
        self.assertIn("I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG", text)
        self.assertIn("BSP_AMP_BCLK_GPIO", text)
        self.assertIn("BSP_AMP_LRCLK_GPIO", text)
        self.assertIn("BSP_AMP_DOUT_GPIO", text)
        self.assertIn("BSP_AMP_SD_GPIO", text)
        self.assertIn("gpio_set_level(BSP_AMP_SD_GPIO, 1)", text)
        self.assertIn("i2s_channel_write", text)

    def test_component_compiles_driver(self):
        text = COMPONENT_CMAKE.read_text(encoding="utf-8")
        self.assertIn('"src/bsp_speaker.c"', text)
        self.assertIn("esp_driver_i2s", text)

    def test_main_runs_speaker_test(self):
        text = MAIN.read_text(encoding="utf-8")
        self.assertIn('#include "bsp_speaker.h"', text)
        self.assertIn("bsp_speaker_run_self_test()", text)


if __name__ == "__main__":
    unittest.main()
