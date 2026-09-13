import unittest
from benchmark_native import simulation_summary


def clock_rows(hz=60, count=121):
    return [dict(retrace=str(i+1), wall_seconds=str(10+i/hz), interval_ms=str(1000/hz),
                 speed='1.0', fast='0', resynced='0') for i in range(count)]


class SimulationTimingTests(unittest.TestCase):
    def test_actual_cadence_is_independent_of_requested_speed(self):
        for hz in (30, 60, 120):
            result = simulation_summary(clock_rows(hz), 1)
            self.assertAlmostEqual(result['retraces_per_second'], hz)
            self.assertAlmostEqual(result['sixty_tick_hz']['median'], hz)
            self.assertEqual(result['requested_speed_max'], 1)

    def test_missing_ticks_do_not_pass(self):
        rows = clock_rows(); del rows[50]
        with self.assertRaises(ValueError): simulation_summary(rows, 1)

    def test_non_monotonic_clock_does_not_pass(self):
        rows = clock_rows(); rows[10]['wall_seconds'] = rows[9]['wall_seconds']
        with self.assertRaises(ValueError): simulation_summary(rows, 1)

    def test_filter_and_speed_overrides_are_reported(self):
        rows = clock_rows(); rows[0]['fast'] = '1'
        rows[60].update(speed='1.01', resynced='1')
        result = simulation_summary(rows, 2)
        self.assertEqual(result['fast_retraces'], 0)
        self.assertEqual(result['clock_resyncs'], 1)
        self.assertEqual(result['requested_speed_max'], 1.01)


if __name__ == '__main__': unittest.main()
