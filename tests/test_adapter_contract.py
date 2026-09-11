"""Source boundary regression; actual NimBLE behavior needs the field test."""
from pathlib import Path
import re
import unittest

class AdapterContract(unittest.TestCase):
    def test_peripheral_mtu_exchange_does_not_abort_connection(self):
        source = (Path(__file__).resolve().parents[1] / 'src/esp32.cpp').read_text()
        source = re.sub(r'\s+', '', source)
        self.assertIn('client_->connect(NimBLEAddress(std::string(t.address),'
                      'std::uint8_t(t.type)),true,false,false)', source)
        self.assertNotIn('client_->exchangeMTU()', source)

if __name__ == '__main__':
    unittest.main()
