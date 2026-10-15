import random
import unittest

from embsim.kernel import Kernel, MAX_TIME


class KernelTests(unittest.TestCase):
    def setUp(self):
        self.kernel = Kernel()
        self.addCleanup(self.kernel.close)
        self.net = self.kernel.add_net()

    def test_drive_resolution(self):
        k = self.kernel
        pull = k.add_driver(self.net, 0)
        a, b = k.add_driver(self.net), k.add_driver(self.net)
        self.assertEqual(k.read(self.net), "Z")
        k.schedule(0, pull, 0)
        k.schedule(10, a, 1)
        k.schedule(20, b, 0)
        k.schedule(30, a, 2)
        k.schedule(40, b, 2)
        for time, expected in [(0, "0"), (9, "0"), (10, "1"), (20, "X"), (30, "0"), (40, "0")]:
            k.advance(time)
            self.assertEqual(k.read(self.net), expected)

    def test_equal_time_order_and_past_rejection(self):
        k = self.kernel
        driver = k.add_driver(self.net)
        k.schedule(12, driver, 0)
        k.schedule(10, driver, 0)
        k.schedule(10, driver, 1)
        k.advance(10)
        self.assertEqual(k.read(self.net), "1")
        for invalid in (-1, MAX_TIME + 1, True, 1.5):
            with self.assertRaises(ValueError):
                k.advance(invalid)
        with self.assertRaises(ValueError):
            k.schedule(9, driver, 0)
        with self.assertRaises(ValueError):
            k.advance(9)
        self.assertEqual(k.now, 10)
        k.advance(12)
        self.assertEqual(k.read(self.net), "0")
        k.advance(MAX_TIME)
        self.assertEqual(k.now, MAX_TIME)

    def test_capacity_failure_is_atomic(self):
        k = self.kernel
        driver = k.add_driver(self.net)
        for i in range(8192):
            k.schedule(i, driver, i % 2)
        with self.assertRaisesRegex(ValueError, "capacity"):
            k.schedule(0, driver, 3)
        k.advance(8191)
        self.assertEqual(k.read(self.net), "1")
        k.schedule(8192, driver, 0)
        k.advance(8192)
        self.assertEqual(k.read(self.net), "0")

    def test_random_schedule_matches_independent_resolution(self):
        k = self.kernel
        rng = random.Random(51)
        strengths = [0, 1, 1, 0]
        drivers = [k.add_driver(self.net, s) for s in strengths]
        events = [(rng.randrange(80), i, rng.randrange(4), rng.randrange(4)) for i in range(300)]
        for at, _, driver, value in events:
            k.schedule(at, drivers[driver], value)
        values = [2] * 4
        ordered = sorted(events)
        for time in range(80):
            for at, _, driver, value in ordered:
                if at == time:
                    values[driver] = value
            active = [(strengths[i], value) for i, value in enumerate(values) if value != 2]
            strongest = max((s for s, _ in active), default=-1)
            resolved = {v for s, v in active if s == strongest}
            expected = "Z" if not resolved else "X" if len(resolved) > 1 else ("0", "1", "Z", "X")[resolved.pop()]
            k.advance(time)
            self.assertEqual(k.read(self.net), expected)

    def test_instances_are_independent_and_closed_calls_fail(self):
        other = Kernel()
        self.addCleanup(other.close)
        net = other.add_net()
        driver = other.add_driver(net)
        other.schedule(0, driver, 1)
        other.advance(0)
        self.assertEqual(other.read(net), "1")
        self.assertEqual(self.kernel.read(self.net), "Z")
        other.close()
        other.close()
        with self.assertRaises(RuntimeError):
            other.advance(0)
