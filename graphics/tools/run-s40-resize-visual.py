#!/data/data/com.termux/files/usr/bin/python3
"""Run the pinned S40 resize demo on an already-active verified S39 desktop."""
import argparse
import os
from pathlib import Path
import sys


def run():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('demo', choices=('zink', 'vulkan'))
    parser.add_argument('--auto-resize', action='store_true',
                        help='Move and resize only the launched demo, then leave it open')
    args = parser.parse_args()
    runner = Path(__file__).resolve().with_name('run-s24-wrapper-visual.py')
    command = [sys.executable, str(runner), args.demo, '--s26', '--require-s39',
               '--coalesced-glx' if args.demo == 'zink' else '--coalesced-xcb']
    if args.auto_resize:
        command.append('--auto-resize')
    os.execv(sys.executable, command)


if __name__ == '__main__':
    run()
