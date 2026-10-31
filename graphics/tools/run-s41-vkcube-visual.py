#!/data/data/com.termux/files/usr/bin/python3
"""Run pinned S41 vkcube on an already-active verified S39 desktop."""
import argparse
import os
from pathlib import Path
import sys


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--auto-resize', action='store_true',
                        help='Move and resize only the launched vkcube, then leave it open')
    args = parser.parse_args()
    runner = Path(__file__).resolve().with_name('run-s24-wrapper-visual.py')
    command = [sys.executable, str(runner), 'vulkan', '--s41', '--s26',
               '--coalesced-xcb', '--require-s39']
    if args.auto_resize:
        command.append('--auto-resize')
    os.execv(sys.executable, command)


if __name__ == '__main__':
    run()
