from pathlib import Path
import unittest


PROJECT = Path(__file__).resolve().parents[1]
PARTITIONS = PROJECT / "partitions.csv"
SDKCONFIG_DEFAULTS = PROJECT / "sdkconfig.defaults"


class PartitionContractTest(unittest.TestCase):
    def test_factory_partition_is_large_enough_for_bluetooth(self):
        text = PARTITIONS.read_text(encoding="utf-8")
        self.assertIn("factory, app, factory, 0x10000, 2M", text)

    def test_sdkconfig_defaults_select_custom_partition_table(self):
        text = SDKCONFIG_DEFAULTS.read_text(encoding="utf-8")
        self.assertIn("CONFIG_PARTITION_TABLE_CUSTOM=y", text)
        self.assertIn('CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"', text)


if __name__ == "__main__":
    unittest.main()
