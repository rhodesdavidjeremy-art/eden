from __future__ import annotations

import argparse
import sys

from .config import load_config
from .server import Application, StdioServer


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="eden_ai", description="Eden Research Reader AI sidecar")
    parser.add_argument("--config", default="", help="path to a JSON configuration file")
    args = parser.parse_args(argv)
    sys.stdin.reconfigure(encoding="utf-8")
    sys.stdout.reconfigure(encoding="utf-8")
    application = Application(load_config(args.config or None))
    server = StdioServer(application, sys.stdin, sys.stdout, workers=application.config.max_workers)
    server.serve()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
