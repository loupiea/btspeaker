from pathlib import Path
import unittest


PROJECT = Path(__file__).resolve().parents[1]
MAIN = PROJECT / "main" / "main.c"
SD_HEADER = PROJECT / "bsp" / "include" / "bsp_sd.h"
SD_SOURCE = PROJECT / "bsp" / "src" / "bsp_sd.c"
COMPONENT_CMAKE = PROJECT / "bsp" / "CMakeLists.txt"


class TfContractTest(unittest.TestCase):
    def test_sd_card_test_api_exists(self):
        header = SD_HEADER.read_text(encoding="utf-8")
        self.assertIn("esp_err_t bsp_sd_mount(void);", header)
        self.assertIn("void bsp_sd_print_info(void);", header)
        self.assertIn("esp_err_t bsp_sd_list_root(void);", header)
        self.assertIn("esp_err_t bsp_sd_run_file_test(void);", header)
        self.assertIn("esp_err_t bsp_sd_run_cmd0_probe(void);", header)
        self.assertIn("esp_err_t bsp_sd_run_mp3_read_test(void);", header)

    def test_sd_driver_mounts_lists_and_runs_file_test(self):
        source = SD_SOURCE.read_text(encoding="utf-8")
        self.assertIn('BSP_SD_MOUNT_POINT "/sdcard"', source)
        self.assertIn("esp_vfs_fat_sdspi_mount", source)
        self.assertIn("SD root directory:", source)
        self.assertIn("SD write/read test passed", source)
        self.assertIn("btspeaker_test.txt", source)
        self.assertIn("SD CMD0 probe: sending 80 dummy clocks", source)
        self.assertIn("SD CMD0 probe rx[%u]=0x%02X", source)
        self.assertIn("SD CMD0 probe result: R1=0x%02X", source)
        self.assertIn("SD CS is active-low in SPI mode", source)
        self.assertIn("run_cmd0_probe_sequence", source)
        self.assertIn("active_low", source)
        self.assertIn("active_high_check", source)
        self.assertIn("SD CMD0 active-low probe result", source)
        self.assertIn("SD CMD0 active-high check result", source)
        self.assertIn("Running SD CMD0 pre-mount probe", source)
        self.assertIn("SD CMD0 pre-mount probe failed", source)
        self.assertIn("SD CMD0 hardware-SPI pre-mount probe failed", source)
        self.assertIn("run_cmd0_bitbang_probe", source)
        self.assertIn("sd_bitbang_transfer_byte", source)
        self.assertIn("SD CMD0 bitbang probe", source)
        self.assertIn("SD CMD0 bitbang rx[%u]=0x%02X", source)
        self.assertIn("SD_CMD0_PROBE_READ_BYTES", source)
        self.assertIn("SD_CMD0_BITBANG_HALF_PERIOD_US", source)
        self.assertIn("0x40, 0x00, 0x00, 0x00, 0x00, 0x95", source)

    def test_sd_driver_reads_music_mp3_for_tf_mode(self):
        source = SD_SOURCE.read_text(encoding="utf-8")
        self.assertIn('BSP_SD_MUSIC_FILE   BSP_SD_MOUNT_POINT "/MUSIC.MP3"', source)
        self.assertIn("BSP_SD_MP3_READ_BUFFER_SIZE", source)
        self.assertIn("BSP_SD_MP3_READ_TEST_MAX_BYTES", source)
        self.assertIn("bsp_sd_run_mp3_read_test", source)
        self.assertIn("TF MP3 read test started", source)
        self.assertIn("TF MP3 header", source)
        self.assertIn("TF MP3 read speed", source)
        self.assertIn("fread", source)

    def test_component_compiles_sd_driver(self):
        text = COMPONENT_CMAKE.read_text(encoding="utf-8")
        self.assertIn('"src/bsp_sd.c"', text)
        self.assertIn("fatfs", text)
        self.assertIn("sdmmc", text)

    def test_main_runs_tf_wav_player_in_tf_mode(self):
        text = MAIN.read_text(encoding="utf-8")
        self.assertIn('#include "bsp_tf_player.h"', text)
        self.assertIn("mode == BSP_MODE_TF", text)
        self.assertIn("bsp_tf_player_start()", text)
        self.assertIn("bsp_tf_player_stop()", text)
        self.assertIn("previous_mode == BSP_MODE_TF", text)
        self.assertIn("TF playback start failed", text)


if __name__ == "__main__":
    unittest.main()
