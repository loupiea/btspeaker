from pathlib import Path
import unittest


PROJECT = Path(__file__).resolve().parents[1]
MODE_HEADER = PROJECT / "bsp" / "include" / "bsp_mode.h"
MODE_SOURCE = PROJECT / "bsp" / "src" / "bsp_mode.c"
OLED_HEADER = PROJECT / "bsp" / "include" / "bsp_oled.h"
OLED_SOURCE = PROJECT / "bsp" / "src" / "bsp_oled.c"
VOLUME_HEADER = PROJECT / "bsp" / "include" / "bsp_volume.h"
VOLUME_SOURCE = PROJECT / "bsp" / "src" / "bsp_volume.c"
COMPONENT_CMAKE = PROJECT / "bsp" / "CMakeLists.txt"
MAIN = PROJECT / "main" / "main.c"


class ModeContractTest(unittest.TestCase):
    def test_mode_api_exists(self):
        text = MODE_HEADER.read_text(encoding="utf-8")
        self.assertIn("typedef enum", text)
        self.assertIn("BSP_MODE_BLUETOOTH", text)
        self.assertIn("BSP_MODE_USB", text)
        self.assertIn("BSP_MODE_TF", text)
        self.assertIn("BSP_MODE_AUX", text)
        self.assertIn("bsp_mode_next", text)
        self.assertIn("bsp_mode_name", text)

    def test_mode_cycles_bluetooth_usb_tf_aux(self):
        text = MODE_SOURCE.read_text(encoding="utf-8")
        self.assertIn("case BSP_MODE_BLUETOOTH:", text)
        self.assertIn("return BSP_MODE_USB;", text)
        self.assertIn("case BSP_MODE_USB:", text)
        self.assertIn("return BSP_MODE_TF;", text)
        self.assertIn("case BSP_MODE_TF:", text)
        self.assertIn("return BSP_MODE_AUX;", text)
        self.assertIn("case BSP_MODE_AUX:", text)
        self.assertIn("return BSP_MODE_BLUETOOTH;", text)
        self.assertIn('"Bluetooth"', text)
        self.assertIn('"U Disk"', text)
        self.assertIn('"TF Card"', text)
        self.assertIn('"AUX"', text)

    def test_oled_exposes_text_display_for_mode(self):
        header = OLED_HEADER.read_text(encoding="utf-8")
        source = OLED_SOURCE.read_text(encoding="utf-8")
        self.assertIn("esp_err_t bsp_oled_clear(void);", header)
        self.assertIn("esp_err_t bsp_oled_show_lines", header)
        self.assertIn("oled_draw_text_line", source)
        self.assertIn("font5x7_for_char", source)

    def test_component_compiles_mode_driver(self):
        text = COMPONENT_CMAKE.read_text(encoding="utf-8")
        self.assertIn('"src/bsp_mode.c"', text)
        self.assertIn('"src/bsp_volume.c"', text)

    def test_volume_api_clamps_and_defaults_to_5_of_50(self):
        header = VOLUME_HEADER.read_text(encoding="utf-8")
        source = VOLUME_SOURCE.read_text(encoding="utf-8")
        self.assertIn("BSP_VOLUME_DEFAULT", header)
        self.assertIn("BSP_VOLUME_MAX", header)
        self.assertIn("uint8_t bsp_volume_get(void);", header)
        self.assertIn("void bsp_volume_set(uint8_t volume);", header)
        self.assertIn("void bsp_volume_adjust(int16_t delta);", header)
        self.assertIn("BSP_VOLUME_DEFAULT = 5", header)
        self.assertIn("BSP_VOLUME_MAX = 50", header)
        self.assertIn("s_volume = BSP_VOLUME_DEFAULT", source)
        self.assertIn("if (volume > BSP_VOLUME_MAX)", source)

    def test_main_initializes_input_and_switches_mode_on_source_button(self):
        text = MAIN.read_text(encoding="utf-8")
        self.assertIn('#include "bsp_input.h"', text)
        self.assertIn('#include "bsp_mode.h"', text)
        self.assertIn('#include "bsp_volume.h"', text)
        self.assertIn("bsp_input_init()", text)
        self.assertIn("BSP_MODE_BLUETOOTH", text)
        self.assertIn("current_mode = BSP_MODE_USB", text)
        self.assertIn("BSP_MODE_TF", text)
        self.assertIn("input_event.source_pressed", text)
        self.assertIn("bsp_mode_next(current_mode)", text)
        self.assertIn("bsp_oled_show_lines", text)
        self.assertIn("Mode:", text)
        self.assertIn("pdMS_TO_TICKS(10)", text)

    def test_main_uses_encoder_to_adjust_volume_and_oled_displays_volume(self):
        text = MAIN.read_text(encoding="utf-8")
        self.assertIn("input_event.encoder_delta", text)
        self.assertIn("bsp_volume_adjust(input_event.encoder_delta)", text)
        self.assertIn("display_status(current_mode)", text)
        self.assertIn("snprintf(volume_line", text)
        self.assertIn("VOL %u/%u", text)


if __name__ == "__main__":
    unittest.main()
