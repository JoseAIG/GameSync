"""Builds a PS2 card with the server's own code and checks one the C side wrote.

    python ps5/tests/ps2mc_fixture.py make  <card.ps2> <expected.txt>
    python ps5/tests/ps2mc_fixture.py check <card.ps2>
"""
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "server"))

from app.services import ps2mc  # noqa: E402
from app.services.ps2_cards import add_ecc, strip_ecc  # noqa: E402

GAMES = {
    "BASLUS-20312": [("icon.sys", b"PS2D" + bytes(960)), ("BASLUS-20312", bytes(range(256)) * 40)],
    "BESLES-50330SAVE": [("view.ico", b"\x01" * 5000), ("data", b"x" * 1024), ("empty", b"")],
    "BADATA-SYSTEM": [("history", b"h" * 700)],
}


def main():
    if sys.argv[1] == "make":
        card = ps2mc.build_card(GAMES, 1690000000)
        open(sys.argv[2], "wb").write(add_ecc(card))
        with open(sys.argv[3], "w") as f:
            for name, files in GAMES.items():
                f.write(f"{name} {hashlib.sha256(ps2mc.build_p2fd(name, files)).hexdigest()}\n")
    else:
        games = ps2mc.parse_card(strip_ecc(open(sys.argv[2], "rb").read()))
        assert set(games) == set(GAMES) | {"BASLUS-99999"}, games.keys()
        assert games["BASLUS-20312"] == [("replaced.bin", b"\xab" * 2049)]
        assert games["BESLES-50330SAVE"] == GAMES["BESLES-50330SAVE"]
        assert games["BADATA-SYSTEM"] == GAMES["BADATA-SYSTEM"]
        assert games["BASLUS-99999"][1][1] == bytes((i * 7) & 255 for i in range(3000))
        print("python side ok")


main()
