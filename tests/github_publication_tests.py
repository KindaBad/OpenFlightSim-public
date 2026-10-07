"""The published-tag endpoint and authenticated draft listing differ."""
import json
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from launcher.storage import LauncherError
from scripts.publish_github_release import release_info


def response(data, code=0, error=''):
    return SimpleNamespace(returncode=code, stdout=json.dumps(data), stderr=error)


class ReleaseLookupTests(unittest.TestCase):
    def test_published_release_uses_tag_response(self):
        release = {'tag_name': 'v0.3.0', 'draft': False}
        with patch('scripts.publish_github_release.subprocess.run', return_value=response(release)) as run:
            self.assertEqual(release_info('owner/game', 'v0.3.0'), release)
            self.assertEqual(run.call_count, 1)

    def test_draft_is_found_in_paginated_authenticated_listing(self):
        draft = {'tag_name': 'v0.3.0', 'draft': True, 'id': 123}
        with patch('scripts.publish_github_release.subprocess.run', side_effect=[
            response({}, 1, 'gh: Not Found (HTTP 404)'),
            response([[{'tag_name': 'other'}], [draft]]),
        ]):
            self.assertEqual(release_info('owner/game', 'v0.3.0'), draft)

    def test_missing_tag_returns_none(self):
        with patch('scripts.publish_github_release.subprocess.run', side_effect=[
            response({}, 1, 'gh: Not Found (HTTP 404)'), response([[]]),
        ]):
            self.assertIsNone(release_info('owner/game', 'v0.3.0'))

    def test_network_error_is_not_a_missing_release(self):
        with patch('scripts.publish_github_release.subprocess.run', return_value=response({}, 1, 'HTTP 502')):
            with self.assertRaises(LauncherError):
                release_info('owner/game', 'v0.3.0')

    def test_ambiguous_drafts_block_publication(self):
        with patch('scripts.publish_github_release.subprocess.run', side_effect=[
            response({}, 1, 'gh: Not Found (HTTP 404)'),
            response([[{'tag_name': 'v0.3.0'}, {'tag_name': 'v0.3.0'}]]),
        ]):
            with self.assertRaises(LauncherError):
                release_info('owner/game', 'v0.3.0')


if __name__ == '__main__':
    unittest.main()
