"""Verify portable roots and fail-closed TLS policy with no system CA store."""
import ssl
import unittest
from unittest.mock import patch

from launcher.download import https_context


class CertificateTrust(unittest.TestCase):
    def test_bundle_supplies_roots_when_system_store_is_empty(self):
        with patch.object(ssl.SSLContext, 'load_default_certs') as system_roots:
            context = https_context()
        system_roots.assert_called_once_with()
        self.assertGreater(context.cert_store_stats()['x509_ca'], 100)
        self.assertEqual(context.verify_mode, ssl.CERT_REQUIRED)
        self.assertTrue(context.check_hostname)

    def test_missing_bundle_cannot_fall_back_to_unverified_tls(self):
        with patch('launcher.download.certifi.where', return_value='missing-root-bundle.pem'):
            with self.assertRaises(OSError):
                https_context()


if __name__ == '__main__':
    unittest.main()
