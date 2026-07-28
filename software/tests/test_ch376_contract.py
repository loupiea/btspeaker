from pathlib import Path
import unittest


PROJECT = Path(__file__).resolve().parents[1]
HEADER = PROJECT / "bsp" / "include" / "bsp_ch376.h"
SOURCE = PROJECT / "bsp" / "src" / "bsp_ch376.c"
PINS_HEADER = PROJECT / "bsp" / "include" / "bsp_pins.h"
COMPONENT_CMAKE = PROJECT / "bsp" / "CMakeLists.txt"
MAIN = PROJECT / "main" / "main.c"


class Ch376ContractTest(unittest.TestCase):
    def test_public_api_exists(self):
        text = HEADER.read_text(encoding="utf-8")
        self.assertIn("esp_err_t bsp_ch376_init(void);", text)
        self.assertIn(
            "esp_err_t bsp_ch376_check_exist(uint8_t challenge, uint8_t *response);",
            text,
        )
        self.assertIn("esp_err_t bsp_ch376_get_version(uint8_t *version);", text)
        self.assertIn("void bsp_ch376_deinit(void);", text)

    def test_driver_uses_manual_cs_and_required_protocol(self):
        text = SOURCE.read_text(encoding="utf-8")
        self.assertIn("SPI3_HOST", text)
        self.assertIn(".spics_io_num = -1", text)
        self.assertIn(".mode = 0", text)
        self.assertIn(".clock_speed_hz = 1000000", text)
        self.assertIn("CH376_CMD_GET_IC_VER = 0x01", text)
        self.assertIn("CH376_CMD_CHECK_EXIST = 0x06", text)
        self.assertIn("esp_rom_delay_us(CH376_TSC_DELAY_US)", text)
        self.assertIn("gpio_set_level(BSP_SD_CS_GPIO, 1)", text)
        self.assertIn("gpio_set_level(BSP_CH376S_CS_GPIO, 0)", text)
        self.assertIn("gpio_set_level(BSP_CH376S_CS_GPIO, 1)", text)

    def test_check_exist_resets_and_retries_after_mismatch(self):
        text = SOURCE.read_text(encoding="utf-8")
        self.assertIn("CH376_CHECK_EXIST_ATTEMPTS", text)
        self.assertIn("reset_chip()", text)
        self.assertIn("CHECK_EXIST mismatch", text)
        self.assertIn("CHECK_EXIST retry", text)

    def test_hardware_reset_uses_gpio4_active_high_rsti(self):
        pins = PINS_HEADER.read_text(encoding="utf-8")
        source = SOURCE.read_text(encoding="utf-8")
        self.assertIn("#define BSP_CH376S_RST_GPIO       GPIO_NUM_4", pins)
        self.assertIn("CH376_HARD_RESET_HIGH_MS", source)
        self.assertIn("CH376_HARD_RESET_RELEASE_MS", source)
        self.assertIn("hardware_reset_chip", source)
        self.assertIn("gpio_set_level(BSP_CH376S_RST_GPIO, 1)", source)
        self.assertIn("gpio_set_level(BSP_CH376S_RST_GPIO, 0)", source)
        self.assertIn("CH376S hardware reset finished", source)
        self.assertIn("hardware_reset_chip()", source)

    def test_component_compiles_driver(self):
        text = COMPONENT_CMAKE.read_text(encoding="utf-8")
        self.assertIn('"src/bsp_ch376.c"', text)

    def test_main_runs_ch376_test_instead_of_sd_test(self):
        text = MAIN.read_text(encoding="utf-8")
        self.assertIn('#include "bsp_ch376.h"', text)
        self.assertIn("bsp_ch376_init()", text)
        self.assertIn("bsp_ch376_check_exist(0x65, &response)", text)
        self.assertIn("bsp_ch376_get_version(&version)", text)
        self.assertNotIn("bsp_sd_mount", text)


if __name__ == "__main__":
    unittest.main()
