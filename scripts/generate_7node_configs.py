#!/usr/bin/env python3
from pathlib import Path
import re
import subprocess
import sys

root = Path(sys.argv[1] if len(sys.argv) > 1 else "~ /librightstuff".replace(" ", "")).expanduser().resolve()
out = root / "benchmarks" / "conf-7node"
out.mkdir(parents=True, exist_ok=True)

base = (root / "hotstuff.conf").read_text().splitlines()
common_lines = [line for line in base if not line.startswith("replica =")]
replicas, secrets = [], []
for idx in range(7):
    key = subprocess.check_output([str(root / "hotstuff-keygen")], cwd=root, text=True)
    tls = subprocess.check_output([str(root / "hotstuff-tls-keygen")], cwd=root, text=True)
    get = lambda pattern, text: re.search(pattern, text).group(1)
    pub, sec = get(r"pub:([0-9a-f]+)", key), get(r"sec:([0-9a-f]+)", key)
    crt = get(r"crt:([0-9a-f]+)", tls)
    tls_sec, cid = get(r"sec:([0-9a-f]+)", tls), get(r"cid:([0-9a-f]+)", tls)
    replicas.append(f"replica = 127.0.0.1:{10000 + idx};{20000 + idx}, {pub}, {cid}")
    secrets.append((sec, tls_sec, crt))

common = "\n".join(common_lines + replicas) + "\n"
(out / "hotstuff-7.conf").write_text(common)
for idx, (sec, tls_sec, crt) in enumerate(secrets):
    (out / f"hotstuff-sec{idx}.conf").write_text(
        common + f"privkey = {sec}\ntls-privkey = {tls_sec}\ntls-cert = {crt}\nidx = {idx}\n"
    )
print(out)
