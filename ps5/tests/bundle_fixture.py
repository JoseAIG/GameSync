"""Cross-checks the C bundle code with the server's own bundle.py.

    python ps5/tests/bundle_fixture.py check <client.3dss> <client hash>
    python ps5/tests/bundle_fixture.py make  <server.3dss>   (prints the hash)
"""
import hashlib
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "server"))

from app.models.save import BundleFile, SaveBundle  # noqa: E402
from app.services.bundle import create_bundle, parse_bundle  # noqa: E402


def main():
    if sys.argv[1] == "check":
        bundle = parse_bundle(open(sys.argv[2], "rb").read())
        assert bundle.title_id_str == "WII_RMCE", bundle.title_id_str
        assert [f.path for f in bundle.files] == ["a-b", "banner.bin", "sub/a.dat"]
        # the hash storage.store_save computes on upload
        server_hash = hashlib.sha256(b"".join(f.data for f in bundle.files)).hexdigest()
        assert server_hash == sys.argv[3], (server_hash, sys.argv[3])
        print("python side ok")
    else:
        files = {"dir/inner.bin": b"\x01\x02" * 500, "zz.sav": b"save", "a.txt": b""}
        bundle = SaveBundle(
            title_id=0,
            timestamp=1700000000,
            files=[BundleFile(path=p, size=len(d), sha256=hashlib.sha256(d).digest(), data=d)
                   for p, d in sorted(files.items())],
            title_id_str="NSW_0100000000010000",
        )
        open(sys.argv[2], "wb").write(create_bundle(bundle))
        print(hashlib.sha256(b"".join(d for _, d in sorted(files.items()))).hexdigest(), end="")


main()
