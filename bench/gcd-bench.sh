#!/bin/sh

perf record ./gcd-bench $@ && perf report
