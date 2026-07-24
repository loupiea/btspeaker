from pathlib import Path
import unittest


PROJECT = Path(__file__).resolve().parents[1]
CH376_HEADER = PROJECT / "bsp" / "include" / "bsp_ch376.h"
CH376_SOURCE = PROJECT / "bsp" / "src" / "bsp_ch376.c"
USB_HEADER = PROJECT / "bsp" / "include" / "bsp_usb_player.h"
USB_SOURCE = PROJECT / "bsp" / "src" / "bsp_usb_player.c"
COMPONENT_CMAKE = PROJECT / "bsp" / "CMakeLists.txt"
MAIN = PROJECT / "main" / "main.c"


class UsbPlayerContractTest(unittest.TestCase):
    def test_ch376_file_read_api_exists(self):
        header = CH376_HEADER.read_text(encoding="utf-8")
        self.assertIn("esp_err_t bsp_ch376_usb_disk_mount(void);", header)
        self.assertIn("esp_err_t bsp_ch376_file_open", header)
        self.assertIn("esp_err_t bsp_ch376_file_read", header)
        self.assertIn("void bsp_ch376_file_close(void);", header)

    def test_ch376_uses_usb_host_file_commands(self):
        source = CH376_SOURCE.read_text(encoding="utf-8")
        self.assertIn("CH376_CMD_SET_USB_MODE = 0x15", source)
        self.assertIn("CH376_CMD_DISK_CONNECT = 0x30", source)
        self.assertIn("CH376_CMD_DISK_MOUNT = 0x31", source)
        self.assertIn("CH376_CMD_SET_FILE_NAME = 0x2F", source)
        self.assertIn("CH376_CMD_FILE_OPEN = 0x32", source)
        self.assertIn("CH376_CMD_BYTE_READ = 0x3A", source)
        self.assertIn("CH376_CMD_BYTE_RD_GO = 0x3B", source)
        self.assertIn("CH376_CMD_RD_USB_DATA0 = 0x27", source)

    def test_ch376_mount_waits_for_usb_connection_and_retries(self):
        source = CH376_SOURCE.read_text(encoding="utf-8")
        self.assertIn("CH376_STATUS_USB_INT_CONNECT = 0x15", source)
        self.assertIn("CH376_STATUS_USB_INT_USB_READY = 0x18", source)
        self.assertIn("CH376_MOUNT_ATTEMPTS", source)
        self.assertIn("DISK_CONNECT", source)
        self.assertIn("DISK_MOUNT attempt", source)

    def test_usb_player_api_and_wav_contract(self):
        header = USB_HEADER.read_text(encoding="utf-8")
        source = USB_SOURCE.read_text(encoding="utf-8")
        self.assertIn("esp_err_t bsp_usb_player_start(void);", header)
        self.assertIn("void bsp_usb_player_stop(void);", header)
        self.assertIn('"MUSIC.WAV"', source)
        self.assertIn("RIFF", source)
        self.assertIn("WAVE", source)
        self.assertIn("bsp_speaker_set_sample_rate", source)
        self.assertIn("bsp_speaker_write", source)
        self.assertIn("xTaskCreate", source)

    def test_component_compiles_usb_player(self):
        text = COMPONENT_CMAKE.read_text(encoding="utf-8")
        self.assertIn('"src/bsp_usb_player.c"', text)

    def test_main_starts_and_stops_usb_player_on_mode_changes(self):
        text = MAIN.read_text(encoding="utf-8")
        self.assertIn('#include "bsp_usb_player.h"', text)
        self.assertIn("bsp_usb_player_start()", text)
        self.assertIn("bsp_usb_player_stop()", text)
        self.assertIn("mode == BSP_MODE_USB", text)


if __name__ == "__main__":
    unittest.main()
