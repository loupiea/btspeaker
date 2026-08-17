from pathlib import Path
import unittest


PROJECT = Path(__file__).resolve().parents[1]
AUX_HEADER = PROJECT / "bsp" / "include" / "bsp_aux.h"
AUX_SOURCE = PROJECT / "bsp" / "src" / "bsp_aux.c"
SPEAKER_HEADER = PROJECT / "bsp" / "include" / "bsp_speaker.h"
SPEAKER_SOURCE = PROJECT / "bsp" / "src" / "bsp_speaker.c"
PINS_HEADER = PROJECT / "bsp" / "include" / "bsp_pins.h"
COMPONENT_CMAKE = PROJECT / "bsp" / "CMakeLists.txt"
MAIN = PROJECT / "main" / "main.c"


class AuxContractTest(unittest.TestCase):
    def test_public_api_exists(self):
        text = AUX_HEADER.read_text(encoding="utf-8")
        self.assertIn("esp_err_t bsp_aux_init(void);", text)
        self.assertIn("esp_err_t bsp_aux_start(void);", text)
        self.assertIn("void bsp_aux_stop(void);", text)

    def test_aux_uses_es8388_i2c_and_i2s1_input_pins(self):
        text = AUX_SOURCE.read_text(encoding="utf-8")
        pins = PINS_HEADER.read_text(encoding="utf-8")
        self.assertIn("ES8388_I2C_ADDRESS = 0x10", text)
        self.assertIn("es8388_probe_address", text)
        self.assertIn("ES8388 probe: address=0x%02X", text)
        self.assertIn("ES8388 register write failed", text)
        self.assertIn("I2C_NUM_0", text)
        self.assertIn("I2S_NUM_1", text)
        self.assertIn("#define BSP_AUX_MCLK_GPIO         GPIO_NUM_0", pins)
        self.assertIn(".mclk = BSP_AUX_MCLK_GPIO", text)
        self.assertIn("BSP_AUX_MCLK_GPIO", text)
        self.assertIn("BSP_AUX_BCLK_GPIO", text)
        self.assertIn("BSP_AUX_LRCLK_GPIO", text)
        self.assertIn("BSP_AUX_DIN_GPIO", text)
        self.assertIn("I2S_ROLE_MASTER", text)
        self.assertIn("I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG", text)
        self.assertIn("while (!s_stop_requested)", text)
        self.assertIn("xTaskCreatePinnedToCore", text)
        self.assertIn("aux_sample_peak", text)
        self.assertIn("silent_blocks", text)
        self.assertIn("AUX samples:", text)
        self.assertIn("bsp_speaker_write", text)

    def test_speaker_exposes_streaming_helpers(self):
        header = SPEAKER_HEADER.read_text(encoding="utf-8")
        source = SPEAKER_SOURCE.read_text(encoding="utf-8")
        self.assertIn("esp_err_t bsp_speaker_start(void);", header)
        self.assertIn("esp_err_t bsp_speaker_write", header)
        self.assertIn("esp_err_t bsp_speaker_stop(void);", header)
        self.assertIn("bsp_speaker_start", source)
        self.assertIn("bsp_speaker_write", source)
        self.assertIn("bsp_speaker_stop", source)

    def test_component_compiles_aux_driver(self):
        text = COMPONENT_CMAKE.read_text(encoding="utf-8")
        self.assertIn('"src/bsp_aux.c"', text)
        self.assertIn("esp_driver_i2s", text)

    def test_main_starts_and_stops_aux_playback(self):
        text = MAIN.read_text(encoding="utf-8")
        self.assertIn('#include "bsp_aux.h"', text)
        self.assertIn("bsp_aux_start()", text)
        self.assertIn("bsp_aux_stop()", text)
        self.assertIn("AUX playback start failed", text)


if __name__ == "__main__":
    unittest.main()
