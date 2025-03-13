#!/usr/bin/env python3

# Copyright 2018-2019 RnD Center "ELVEES", JSC

import subprocess
import unittest
import re
from queue import Queue
from threading import Thread

log_print = False
timeout = 300


def versiontuple(v):
    """Return Linux kernel version tuple

    Release candidates not supported, so no "-rc" in minor.
    "-" in other places means it is modification.

    >>> versiontuple("4.19.106-mcom03-latest.elv.alt1") >= versiontuple("4.19.0")
    True
    >>> versiontuple("4.19.106-mcom03-latest.elv.alt1") >= versiontuple("4.19.106")
    True
    >>> versiontuple("4.19.105-mcom03-latest.elv.alt1") >= versiontuple("4.19.106")
    False
    >>> versiontuple("4.19-rc1") >= versiontuple("4.19") # doctest: +IGNORE_EXCEPTION_DETAIL
    Traceback (most recent call last):
        ...
    AssertionError: Release candidates not supported
    >>> versiontuple("4.19.0-rc1") >= versiontuple("4.19")
    True
    >>> versiontuple("5.0") >= versiontuple("4.19")
    True
    >>> versiontuple("5.10") >= versiontuple("5.9")
    True
    """
    assert (
        "rc" not in re.findall(r"\d+\.\d+(\.d+)?(-rc\d+)?.*", v)[0][1]
    ), "Release candidates not supported"
    version = v.split("-", 1)
    return tuple(map(int, version[0].split(".")))


def get_kernel_version():
    return versiontuple(subprocess.run(["uname", "-r"], capture_output=True, text=True).stdout)


def generate_image(image, width, height):
    # fmt: off
    subprocess.check_call(["ffmpeg",
                           "-loglevel", "error",
                           "-f", "lavfi",
                           "-i", "testsrc=size={}x{}".format(width, height),
                           "-pix_fmt", "rgb24",
                           "-frames", "1",
                           "-y",  # always overwrite
                           image])
    # fmt: on


def setUpModule():
    if get_kernel_version() < versiontuple("5.4"):
        subprocess.check_call("modprobe -r avico".split())
    try:
        with open("/sys/bus/amba/drivers/dma-pl330/unbind", "wb") as unbind:
            unbind.write(b"37220000.dma")
    except OSError:
        pass  # it's ok in case driver already unbound
    subprocess.check_call("modprobe -r delcore30m".split())
    subprocess.check_call("modprobe delcore30m".split())


def tearDownModule():
    subprocess.check_call("echo 37220000.dma > /sys/bus/amba/drivers/dma-pl330/bind", shell=True)
    if get_kernel_version() < versiontuple("5.4"):
        subprocess.check_call("modprobe avico".split())


class TestcaseDSP(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.images = ["/tmp/image1.png", "/tmp/image2.png"]
        generate_image(cls.images[0], 1280, 720)
        generate_image(cls.images[1], 1920, 1080)

    @classmethod
    def tearDownClass(cls):
        for image in cls.images:
            subprocess.call(["rm", image])

    def exec_command(self, cmd, *args):
        options = {}
        if not log_print:
            options["stdout"] = open("/dev/null", "w")
            options["stderr"] = subprocess.STDOUT
        code = subprocess.call([cmd] + list(args), timeout=timeout, **options)
        if not log_print:
            options["stdout"].close()
        self.assertEqual(code, 0, "return code {} while running {}".format(code, cmd))

    def test_paralleltest1core(self):
        threadnum = 15
        cores = 1
        for _ in range(100):
            self.exec_command("delcore30m-paralleltest", str(threadnum), str(cores))

    def test_paralleltest2cores(self):
        threadnum = 15
        cores = 2
        for _ in range(100):
            self.exec_command("delcore30m-paralleltest", str(threadnum), str(cores))

    def test_dma1core(self):
        cycles = 100
        for _ in range(cycles):
            self.exec_command("delcore30m-inversiontest", "-i", self.images[1])

    def test_dma2core_async(self):
        def exec_command(cmd, *args):
            options = {}
            if not log_print:
                options["stdout"] = open("/dev/null", "w")
                options["stderr"] = subprocess.STDOUT
            code = subprocess.call([cmd] + list(args), timeout=timeout, **options)
            if not log_print:
                options["stdout"].close()
            return code

        def process(image, cycles):
            for _ in range(cycles):
                code = exec_command("delcore30m-inversiontest", "-i", image)
                if code:
                    return code
            return 0

        cycles = [700, 100]
        threads = []
        queue = Queue()
        for core, cycle in enumerate(cycles):
            thread = Thread(
                target=lambda q, arg1, arg2: q.put(process(arg1, arg2)),
                args=(queue, self.images[core], cycle),
            )
            thread.start()
            threads.append(thread)
        for thread in threads:
            thread.join()
        while not queue.empty():
            self.assertEqual(queue.get(), 0, "Failed to get result from queue")

    def test_fibonacci(self):
        self.exec_command("delcore30m-fibonacci", "-i", "10", "-v")


if __name__ == "__main__":
    unittest.main(verbosity=2)
