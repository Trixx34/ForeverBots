"""Tests for the pure logic of the support site (no database, no worldserver). Run:  python3 -m unittest contrib/support_site/test_support_server.py -v"""
import os
import sys
import types
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if 'pymysql' not in sys.modules:                  # only the import is needed here
    try:
        import pymysql      # noqa: F401
    except ImportError:
        stub = types.ModuleType('pymysql')
        stub.cursors = types.SimpleNamespace(DictCursor=object)
        sys.modules['pymysql'] = stub

import support_server as ss      # noqa: E402


class Helpers(unittest.TestCase):
    def test_gold_fee(self):
        self.assertEqual(ss.gold_fee(100, 10), 10)
        self.assertEqual(ss.gold_fee(5, 10), 1)           # at least 1 point while the percent is not 0
        self.assertEqual(ss.gold_fee(100, 0), 0)

    def test_to_int(self):
        self.assertEqual(ss.to_int(' 42 '), 42)
        self.assertEqual(ss.to_int('x', 7), 7)
        self.assertEqual(ss.to_int(None), 0)

    def test_ra_quote_removes_quotes_and_line_breaks(self):
        self.assertEqual(ss.ra_quote('a"b\r\nc'), "a'b  c")

    def test_totp_rfc6238_vector(self):
        secret = 'GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ'       # "12345678901234567890"
        self.assertEqual(ss.totp_code(secret, 59), '287082')
        self.assertEqual(ss.totp_code(secret, 1111111109), '081804')

    def test_totp_check_accepts_neighbour_steps_only(self):
        secret = ss.totp_new_secret()
        now = ss.time.time()
        self.assertTrue(ss.totp_check(secret, ss.totp_code(secret, now)))
        self.assertTrue(ss.totp_check(secret, ss.totp_code(secret, now - ss.TOTP_STEP)))
        self.assertFalse(ss.totp_check(secret, ss.totp_code(secret, now - 5 * ss.TOTP_STEP)))
        self.assertFalse(ss.totp_check(secret, '12'))

    def test_rate_limit(self):
        key = 'test-' + str(ss.time.time())
        self.assertTrue(all(ss.rate_ok('1.2.3.4', key, 3) for _ in range(3)))
        self.assertFalse(ss.rate_ok('1.2.3.4', key, 3))
        self.assertTrue(ss.rate_ok('5.6.7.8', key, 3))


class Login(unittest.TestCase):
    """Handler.login: staff guessed from the address must confirm a code; others get a weak session."""

    def setUp(self):
        self.calls = []
        self.saved = (ss.staff_level, ss.start_session, ss.Handler.send_code, ss.Handler.redirect)
        ss.CFG['staff_min_level'] = 2
        ss.start_session = lambda row, weak=False: self.calls.append(('session', weak)) or 'sid'
        ss.Handler.send_code = lambda h, row, target: self.calls.append(('code', target))
        ss.Handler.redirect = lambda h, target, headers=(): self.calls.append(('redirect', target))
        self.handler = object.__new__(ss.Handler)
        self.row = {'accountId': 1, 'realmId': 1}

    def tearDown(self):
        ss.staff_level, ss.start_session, ss.Handler.send_code, ss.Handler.redirect = self.saved

    def test_staff_must_confirm_a_code(self):
        ss.staff_level = lambda a, r: 3
        self.handler.login(self.row, '/')
        self.assertEqual(self.calls, [('code', '/')])

    def test_staff_after_the_code_gets_a_strong_session(self):
        ss.staff_level = lambda a, r: 3
        self.handler.login(self.row, '/', verified=True)
        self.assertEqual(self.calls[0], ('session', False))

    def test_player_guessed_from_the_address_is_weak(self):
        ss.staff_level = lambda a, r: 0
        self.handler.login(self.row, '/')
        self.assertEqual(self.calls[0], ('session', True))

    def test_player_who_confirmed_the_code_is_strong(self):
        ss.staff_level = lambda a, r: 0
        self.handler.login(self.row, '/', verified=True)
        self.assertEqual(self.calls[0], ('session', False))


if __name__ == '__main__':
    unittest.main()
