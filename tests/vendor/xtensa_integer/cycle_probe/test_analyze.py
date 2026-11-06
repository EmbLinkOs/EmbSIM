#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Protocol rejection checks; synthetic cycles never qualify hardware timing."""
import json
import unittest
from analyze import analyze,COUNTS,CASES,PREFIX


def records():
    rows=[dict(schema=1,kind='context',idf='synthetic-test',chip_model=1,revision_code=3,placement='IRAM',reported_cpu_hz=240000000,timing_model_qualified=False)]
    rows += [dict(schema=1,kind='sample',core=c,loop_buffer=m,case=k,iterations=n,repeat=r,raw_cycles=n+10+r)
             for c in range(2) for m in range(2) for k in CASES for n in COUNTS for r in range(17)]
    rows.append(dict(schema=1,kind='complete',samples=3672,timing_model_qualified=False))
    return rows


def encode(rows):return 'boot log ignored\n'+'\n'.join(PREFIX+json.dumps(r) for r in rows)


class CaptureProtocol(unittest.TestCase):
    def test_complete_keeps_scope_and_outliers(self):
        rows=records();rows[1]['raw_cycles']=1000000
        report=analyze(encode(rows))
        self.assertFalse(report['timing_model_qualified']);self.assertFalse(report['origin_authenticated'])
        self.assertEqual(report['samples'],3672);self.assertEqual(report['groups'][0]['maximum'],1000000)
        self.assertEqual(report['groups'][0]['median'],20)
    def test_missing_and_duplicate(self):
        rows=records()
        for bad in [rows[:-1],rows[:1]+rows[2:],rows+[rows[1]],rows+rows]:
            with self.assertRaises(ValueError):analyze(encode(bad))
    def test_invalid_or_falsely_qualified(self):
        for key,value in [('core',2),('loop_buffer',True),('repeat',17),('raw_cycles',0),('raw_cycles',True),('case','unknown'),('iterations',0)]:
            rows=records();rows[1][key]=value
            with self.assertRaises(ValueError):analyze(encode(rows))
        rows=records();rows[0]['timing_model_qualified']=True
        with self.assertRaises(ValueError):analyze(encode(rows))
    def test_malformed_or_failed(self):
        with self.assertRaises(ValueError):analyze(PREFIX+'{broken')
        rows=records();rows.append(dict(schema=1,kind='error'))
        with self.assertRaises(ValueError):analyze(encode(rows))


if __name__=='__main__':unittest.main()
