import gzip
import http.client
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading
import unittest
from urllib.parse import urlencode

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS / "web"))
from build_web import build, generate
import preview_server as server


class WebTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.httpd = server.ThreadingHTTPServer(('127.0.0.1', 0), server.PreviewHandler)
        cls.thread = threading.Thread(target=cls.httpd.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.httpd.shutdown()
        cls.httpd.server_close()
        cls.thread.join()

    def setUp(self):
        with server.STATE_LOCK:
            server.STATE = server.PreviewState()

    def request(self, path='/', headers=None, fields=None):
        conn = http.client.HTTPConnection(*self.httpd.server_address, timeout=5)
        try:
            body = None if fields is None else urlencode(fields)
            conn.request('GET' if fields is None else 'POST', path, body, headers or {})
            response = conn.getresponse()
            return response.status, dict(response.getheaders()), response.read()
        finally:
            conn.close()

    def test_compression_is_deterministic_and_round_trips(self):
        plain, compressed = build(server.TEMPLATE)
        self.assertEqual(gzip.decompress(compressed), plain)
        self.assertEqual(build(server.TEMPLATE), (plain, compressed))
        self.assertLess(len(plain), server.TEMPLATE.stat().st_size)
        self.assertLess(len(compressed), len(plain) // 2)
        self.assertNotIn(b'{{', plain)
        self.assertIn('読込中'.encode(), plain)

    def test_no_js_comments_survive_minification(self):
        # minify_html doesn't reliably strip every `//` line comment (one
        # placed right before a `function` declaration survived once even
        # though otherwise-identical comments elsewhere were stripped) --
        # catch any future regression generically rather than by string.
        plain, _ = build(server.TEMPLATE)
        script = plain[plain.find(b'<script>'):plain.find(b'</script>')]
        comments = re.findall(rb'//[^\n]*', script)
        self.assertEqual(comments, [], f'JS comment(s) survived minification: {comments}')

    def test_negotiation_and_cache_variants(self):
        status, headers, plain = self.request()
        self.assertEqual(status, 200)
        self.assertNotIn('Content-Encoding', headers)
        identity_tag = headers['ETag']
        status, headers, compressed = self.request(headers={'Accept-Encoding': 'br, gzip'})
        self.assertEqual(headers['Content-Encoding'], 'gzip')
        self.assertEqual(headers['Vary'], 'Accept-Encoding')
        self.assertEqual(gzip.decompress(compressed), plain)
        self.assertNotEqual(headers['ETag'], identity_tag)
        gzip_tag = headers['ETag']
        status, headers, body = self.request(headers={
            'Accept-Encoding': 'gzip', 'If-None-Match': 'W/' + gzip_tag})
        self.assertEqual((status, body), (304, b''))
        status, _, _ = self.request(headers={
            'Accept-Encoding': 'identity', 'If-None-Match': gzip_tag})
        self.assertEqual(status, 200)
        for header in ['gzip;q=0', 'br', '*;q=1,gzip;q=0']:
            status, headers, body = self.request(headers={'Accept-Encoding': header})
            self.assertEqual((status, body), (200, plain))
            self.assertNotIn('Content-Encoding', headers)
        status, _, _ = self.request(headers={'Accept-Encoding': 'gzip;q=0,identity;q=0'})
        self.assertEqual(status, 406)

    def test_state_not_cached_and_mutation_returns_json(self):
        status, headers, body = self.request('/state')
        self.assertEqual(headers['Cache-Control'], 'no-store')
        self.assertTrue(json.loads(body)['light'])
        status, headers, body = self.request('/action', {'Accept': 'application/json'},
                                              {'target': 'light', 'state': 'off'})
        self.assertEqual(status, 200)
        self.assertNotIn('Location', headers)
        state = json.loads(body)
        self.assertFalse(state['light'])
        self.assertFalse(state['switch'])
        self.assertTrue(state['message'])
        self.assertEqual(json.loads(self.request('/state')[2])['message'], '')

    def test_settings_json_preserves_special_characters(self):
        name = '照明 "<&>\\テスト'
        status, _, body = self.request('/settings', {'Accept': 'application/json'}, {
            'device_name': name, 'hostname': 'preview', 'timeout': '601',
            'ambient_threshold': '42'})
        self.assertEqual(status, 200)
        state = json.loads(body)
        self.assertEqual(state['device_name'], name)
        self.assertEqual(state['timeout'], 601)
        _, _, body = self.request('/settings', {'Accept': 'application/json'}, {
            'device_name': '', 'hostname': 'preview', 'timeout': '0', 'ambient_threshold': '101'})
        state = json.loads(body)
        self.assertTrue(state['error'])
        self.assertEqual(state['device_name'], name)
        self.assertEqual(state['timeout'], 601)

    def test_legacy_post_redirect_and_optional_night(self):
        status, headers, _ = self.request('/action', fields={'target': 'night_feature', 'state': 'off'})
        self.assertEqual(status, 303)
        self.assertEqual(headers['Location'], '/')
        state = json.loads(self.request('/state')[2])
        self.assertFalse(state['night_feature'])
        self.assertFalse(state['night'])

    def test_remove_last_fabric_preserves_wifi_without_opening_commissioning(self):
        info = json.loads(self.request('/device-info')[2])
        fabric = info['fabrics'][0]
        _, _, body = self.request('/matter', {'Accept': 'application/json'},
                                  dict(fabric, action='remove'))
        self.assertFalse(json.loads(body)['error'])
        info = json.loads(self.request('/device-info')[2])
        self.assertEqual(info['fabrics'], [])
        self.assertTrue(info['connected'])
        self.assertFalse(info['commissioning_open'])
        self.assertEqual(info['ipv4'], '192.0.2.10')

    def test_stale_or_invalid_fabric_removal_is_rejected(self):
        original = json.loads(self.request('/device-info')[2])['fabrics']
        for override in ({'index': '1x'}, {'index': '0'}, {'index': '255'},
                         {'fabric_id': '0x0000000000000000'},
                         {'node_id': ''}, {'vendor_id': ''}):
            with self.subTest(override=override):
                _, _, body = self.request('/matter', {'Accept': 'application/json'},
                                          {**original[0], 'action': 'remove', **override})
                self.assertTrue(json.loads(body)['error'])
                self.assertEqual(json.loads(self.request('/device-info')[2])['fabrics'], original)

    def test_commissioning_open_duplicate_and_expiry(self):
        _, _, body = self.request('/matter', {'Accept': 'application/json'},
                                  {'action': 'commission'})
        self.assertFalse(json.loads(body)['error'])
        self.assertTrue(json.loads(self.request('/device-info')[2])['commissioning_open'])
        with server.STATE_LOCK:
            deadline = server.STATE.commissioning_until
        self.request('/matter', {'Accept': 'application/json'}, {'action': 'commission'})
        with server.STATE_LOCK:
            self.assertEqual(server.STATE.commissioning_until, deadline)
            server.STATE.commissioning_until = 0
        self.assertFalse(json.loads(self.request('/device-info')[2])['commissioning_open'])

    def test_actual_cpp_negotiation_and_generated_header(self):
        # Compile the same negotiation code and generated bytes used by ESP-IDF.
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            generate(server.TEMPLATE, tmp / 'web_assets.h')
            test = tmp / 'test.cpp'
            test.write_text(r'''
#include <cassert>
#include <string>
#include "web_asset_http.h"
#include "web_assets.h"
int main() {
  using namespace web_asset;
  assert(quality("", "gzip") == 0);
  assert(quality("", "identity") == 1);
  assert(quality("br, GZIP; q=0.5", "gzip") == .5f);
  assert(quality("*;q=1, gzip;q=0", "gzip") == 0);
  assert(quality("gzip;q=0, *;q=1", "gzip") == 0);
  assert(quality("*;q=0", "identity") == 0);
  assert(quality("*;q=0,identity;q=1", "identity") == 1);
  assert(quality("gzip;q=oops", "gzip") == 0);
  assert(quality("gzip;q=nan", "gzip") == 0);
  assert(quality("gzip;q=2", "gzip") == 0);
  assert(quality("x-gzip", "gzip") == 0);
  assert(etagMatches("W/\"tag\", \"other\"", "\"tag\""));
  assert(etagMatches("*", "\"tag\""));
  assert(!etagMatches("\"other\"", "\"tag\""));
  assert(kWebGzip[0] == 0x1f && kWebGzip[1] == 0x8b);
  assert(kWebIdentityEtag[0] == '"');
  assert(std::string(kWebIdentityEtag) != kWebGzipEtag);
}
''')
            subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(TOOLS.parent / 'main'), '-I' + str(tmp),
                            str(test), '-o', str(tmp / 'test')], check=True)
            subprocess.run([str(tmp / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
