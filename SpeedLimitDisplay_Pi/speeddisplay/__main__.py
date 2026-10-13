"""python -m speeddisplay [-c config.ini] [run|newkey]"""

import argparse
import logging

from . import config, service


def main():
    ap = argparse.ArgumentParser(prog="speeddisplay")
    ap.add_argument("-c", "--config", default="/etc/speeddisplay/config.ini")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("command", nargs="?", default="run", choices=["run", "newkey"])
    args = ap.parse_args()
    logging.basicConfig(level=logging.DEBUG if args.verbose else logging.INFO,
                        format="%(asctime)s %(levelname)s %(message)s")
    cfg = config.load(args.config)
    if args.command == "newkey":
        service.new_key(cfg)
    else:
        service.run(cfg)


if __name__ == "__main__":
    main()
