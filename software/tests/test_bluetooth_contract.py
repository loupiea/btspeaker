from pathlib import Path
import unittest


PROJECT = Path(__file__).resolve().parents[1]
BT_HEADER = PROJECT / "bsp" / "include" / "bsp_bluetooth.h"
BT_SOURCE = PROJECT / "bsp" / "src" / "bsp_bluetooth.c"
COMPONENT_CMAKE = PROJECT / "bsp" / "CMakeLists.txt"
MAIN = PROJECT / "main" / "main.c"
SDKCONFIG_DEFAULTS = PROJECT / "sdkconfig.defaults"


class BluetoothContractTest(unittest.TestCase):
    def test_public_api_exists(self):
        text = BT_HEADER.read_text(encoding="utf-8")
        self.assertIn("esp_err_t bsp_bluetooth_a2dp_sink_start(void);", text)

    def test_a2dp_sink_uses_classic_bt_and_speaker_output(self):
        text = BT_SOURCE.read_text(encoding="utf-8")
        self.assertIn('BT_SPEAKER_DEVICE_NAME[] = "BT Speaker"', text)
        self.assertIn("esp_bt_controller_mem_release(ESP_BT_MODE_BLE)", text)
        self.assertIn("esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)", text)
        self.assertIn("esp_bluedroid_init", text)
        self.assertIn("esp_a2d_sink_init", text)
        self.assertIn("esp_a2d_sink_register_data_callback", text)
        self.assertIn("esp_bt_gap_set_scan_mode", text)
        self.assertIn("bsp_speaker_write", text)

    def test_component_compiles_bluetooth_driver(self):
        text = COMPONENT_CMAKE.read_text(encoding="utf-8")
        self.assertIn('"src/bsp_bluetooth.c"', text)
        self.assertIn("bt", text)
        self.assertIn("nvs_flash", text)

    def test_sdkconfig_defaults_enable_classic_a2dp(self):
        text = SDKCONFIG_DEFAULTS.read_text(encoding="utf-8")
        self.assertIn("CONFIG_BT_ENABLED=y", text)
        self.assertIn("CONFIG_BT_CLASSIC_ENABLED=y", text)
        self.assertIn("CONFIG_BT_A2DP_ENABLE=y", text)

    def test_main_can_start_bluetooth_and_skips_aux_self_test(self):
        text = MAIN.read_text(encoding="utf-8")
        self.assertIn('#include "bsp_bluetooth.h"', text)
        self.assertIn("bsp_bluetooth_a2dp_sink_start()", text)
        self.assertIn("if (mode == BSP_MODE_BLUETOOTH)", text)
        self.assertIn("current_mode = BSP_MODE_USB", text)
        self.assertNotIn("bsp_aux_run_self_test()", text)


if __name__ == "__main__":
    unittest.main()
