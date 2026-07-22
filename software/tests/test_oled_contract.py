from pathlib import Path
import unittest


PROJECT = Path(__file__).resolve().parents[1]
HEADER = PROJECT / "bsp" / "include" / "bsp_oled.h"
SOURCE = PROJECT / "bsp" / "src" / "bsp_oled.c"
COMPONENT_CMAKE = PROJECT / "bsp" / "CMakeLists.txt"
MAIN = PROJECT / "main" / "main.c"


class OledContractTest(unittest.TestCase):
    def test_public_api_exists(self):
        text = HEADER.read_text(encoding="utf-8")
        self.assertIn("esp_err_t bsp_oled_init(void);", text)
        self.assertIn("esp_err_t bsp_oled_run_self_test(void);", text)

    def test_driver_uses_i2c_and_ssd1306_defaults(self):
        text = SOURCE.read_text(encoding="utf-8")
        self.assertIn("I2C_NUM_0", text)
        self.assertIn("BSP_I2C_SDA_GPIO", text)
        self.assertIn("BSP_I2C_SCL_GPIO", text)
        self.assertIn("OLED_I2C_ADDRESS = 0x3C", text)
        self.assertIn("OLED_CONTROL_COMMAND = 0x00", text)
        self.assertIn("OLED_CONTROL_DATA = 0x40", text)
        self.assertIn("OLED_WIDTH = 128", text)
        self.assertIn("OLED_PAGES = 8", text)
        self.assertIn("0xAF", text)
        self.assertIn("bsp_oled_run_self_test", text)

    def test_component_compiles_driver(self):
        text = COMPONENT_CMAKE.read_text(encoding="utf-8")
        self.assertIn('"src/bsp_oled.c"', text)

    def test_main_runs_oled_test(self):
        text = MAIN.read_text(encoding="utf-8")
        self.assertIn('#include "bsp_oled.h"', text)
        self.assertIn("bsp_oled_run_self_test()", text)


if __name__ == "__main__":
    unittest.main()
