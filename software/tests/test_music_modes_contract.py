from pathlib import Path
import unittest


PROJECT = Path(__file__).resolve().parents[1]
MAIN = PROJECT / "main" / "main.c"
MODE_HEADER = PROJECT / "bsp" / "include" / "bsp_mode.h"
MODE_SOURCE = PROJECT / "bsp" / "src" / "bsp_mode.c"
TF_HEADER = PROJECT / "bsp" / "include" / "bsp_tf_player.h"
TF_SOURCE = PROJECT / "bsp" / "src" / "bsp_tf_player.c"
AUX_HEADER = PROJECT / "bsp" / "include" / "bsp_aux.h"
AUX_SOURCE = PROJECT / "bsp" / "src" / "bsp_aux.c"
COMPONENT_CMAKE = PROJECT / "bsp" / "CMakeLists.txt"


class MusicModesContractTest(unittest.TestCase):
    def test_source_cycle_contains_only_bluetooth_tf_and_aux(self):
        header = MODE_HEADER.read_text(encoding="utf-8")
        source = MODE_SOURCE.read_text(encoding="utf-8")
        main = MAIN.read_text(encoding="utf-8")

        self.assertNotIn("BSP_MODE_USB", header)
        self.assertNotIn("BSP_MODE_USB", source)
        self.assertNotIn("BSP_MODE_USB", main)
        self.assertNotIn("bsp_usb_player", main)
        self.assertNotIn("bsp_ch376", main)
        self.assertIn("return BSP_MODE_TF;", source)
        self.assertIn("return BSP_MODE_AUX;", source)
        self.assertIn("return BSP_MODE_BLUETOOTH;", source)

    def test_tf_mode_plays_pcm_wav_from_card(self):
        header = TF_HEADER.read_text(encoding="utf-8")
        source = TF_SOURCE.read_text(encoding="utf-8")
        main = MAIN.read_text(encoding="utf-8")
        cmake = COMPONENT_CMAKE.read_text(encoding="utf-8")

        self.assertIn("esp_err_t bsp_tf_player_start(void);", header)
        self.assertIn("void bsp_tf_player_stop(void);", header)
        self.assertIn('"/sdcard/MUSIC.WAV"', source)
        self.assertIn('memcmp(header.riff, "RIFF", 4)', source)
        self.assertIn('memcmp(header.wave, "WAVE", 4)', source)
        self.assertIn('memcmp(chunk.id, "fmt ", 4)', source)
        self.assertIn('memcmp(chunk.id, "data", 4)', source)
        self.assertIn("bits_per_sample == 16", source)
        self.assertIn("bsp_speaker_set_sample_rate", source)
        self.assertIn("bsp_speaker_write", source)
        self.assertIn("xTaskCreatePinnedToCore", source)
        self.assertIn("bsp_tf_player_start()", main)
        self.assertIn("bsp_tf_player_stop()", main)
        self.assertIn('"src/bsp_tf_player.c"', cmake)

    def test_aux_mode_runs_until_stop_requested(self):
        header = AUX_HEADER.read_text(encoding="utf-8")
        source = AUX_SOURCE.read_text(encoding="utf-8")
        main = MAIN.read_text(encoding="utf-8")

        self.assertIn("esp_err_t bsp_aux_start(void);", header)
        self.assertIn("void bsp_aux_stop(void);", header)
        self.assertIn("while (!s_stop_requested)", source)
        self.assertIn("xTaskCreatePinnedToCore", source)
        self.assertIn("bsp_aux_start()", main)
        self.assertIn("bsp_aux_stop()", main)
        self.assertNotIn("bsp_aux_run_self_test()", main)

    def test_usb_playback_sources_are_not_built(self):
        cmake = COMPONENT_CMAKE.read_text(encoding="utf-8")
        self.assertNotIn('"src/bsp_usb_player.c"', cmake)
        self.assertNotIn('"src/bsp_ch376.c"', cmake)


if __name__ == "__main__":
    unittest.main()
