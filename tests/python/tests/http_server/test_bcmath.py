from python.lib.testcase import KphpServerAutoTestCase


class TestBCmath(KphpServerAutoTestCase):
    def _bcmath_request(self, type, arg1, arg2, scale=0):
        resp = self.kphp_server.http_post(
            uri="/test_bcmath",
            json={
                "type": type,
                "arg1": arg1,
                "arg2": arg2,
                "scale": scale
            })
        self.assertEqual(resp.status_code, 200)
        return resp.text

    def test_bcmath(self):
        self.assertEqual(self._bcmath_request("bcadd", "0", "-0.1"), "0")
        self.assertEqual(self._bcmath_request("bcadd", "0", "-0.1", 1), "-0.1")
        self.assertEqual(self._bcmath_request("bcadd", "0", "-0.1", 2), "-0.10")

        self.assertEqual(self._bcmath_request("bcmul", "0.9", "-0.1"), "0")
        self.assertEqual(self._bcmath_request("bcmul", "0.9", "-0.1", 1), "0.0")
        self.assertEqual(self._bcmath_request("bcmul", "0.9", "-0.1", 2), "-0.09")

        self.assertEqual(self._bcmath_request("bcmul", "0.90", "-0.1"), "0")
        self.assertEqual(self._bcmath_request("bcmul", "0.90", "-0.1", 1), "0.0")
        self.assertEqual(self._bcmath_request("bcmul", "0.90", "-0.1", 2), "-0.09")
