#!/usr/bin/env python3
"""
weather_etl/etl.py

Scheduled job: runs every 6 hours, triggered by EventBridge.
Downloads processed .npy weather arrays from S3, computes CCH edge weights,
writes weights.bin, uploads back to S3 for router_server to pick up.

TODO: replace compute_weights_python() with a pybind11 call to
      maritime_weather_etl.WeightsWriter.compute() once bindings exist.
      The C++ version is orders of magnitude faster for ~18M edges.
"""

import argparse
import logging
import struct
import tempfile
import time
from pathlib import Path

import boto3
import numpy as np

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger(__name__)

VARIABLES = [
    "sigwh", "pwh", "pwp", "pwd",
    "pswh", "pswp", "pswd",
    "wsh",  "wsp",
    "was",  "wad", "wsd",
    "ocs",  "sst",
]

WEIGHTS_MAGIC   = 0x54484757   # "WGHT" LE — must match WeightsHeader in C++
WEIGHTS_VERSION = 1


def fetch_npy_files(s3: object, bucket: str, prefix: str, local_dir: Path) -> list[str]:
    paginator = s3.get_paginator("list_objects_v2")
    fetched = []
    for page in paginator.paginate(Bucket=bucket, Prefix=prefix):
        for obj in page.get("Contents", []):
            key = obj["Key"]
            if not key.endswith(".npy"):
                continue
            dest = local_dir / Path(key).name
            s3.download_file(bucket, key, str(dest))
            fetched.append(str(dest))
            log.info("Downloaded %s", key)
    return fetched


def load_graph_csr(graph_bin_path: str) -> dict:
    """
    Minimal binary reader for graph.bin.
    Matches the GraphHeader layout in lib/include/maritime/static_graph.hpp.
    """
    with open(graph_bin_path, "rb") as f:
        magic, version, n_nodes, n_edges = struct.unpack("<IIII", f.read(16))
    if magic != 0x4752414D:
        raise ValueError(f"Bad graph.bin magic: 0x{magic:08X}")

    dtype_map = {
        "lat":          ("float32", n_nodes),
        "lon":          ("float32", n_nodes),
        "depth":        ("float16", n_nodes),
        "row_ptr":      ("uint32",  n_nodes + 1),
        "col_idx":      ("uint32",  n_edges),
        "base_dist_nm": ("float32", n_edges),
    }

    result = {"n_nodes": n_nodes, "n_edges": n_edges}
    offset = 16
    with open(graph_bin_path, "rb") as f:
        f.seek(offset)
        for name, (dtype, count) in dtype_map.items():
            result[name] = np.frombuffer(f.read(count * np.dtype(dtype).itemsize), dtype=dtype).copy()

    return result


def compute_weights(
    graph: dict,
    sigwh: np.ndarray,
    was: np.ndarray,
    wad: np.ndarray,
) -> np.ndarray:
    """
    Proxy FOC cost: distance × (1 + sigwh/3 + headwind_factor), scaled to uint32.
    Matches WeightsWriter::compute() in weather_etl/src/weights_writer.cpp.

    headwind_factor = max(0, -was · cos(hdg - wad_rad)) / WIND_REF_SPEED
    where hdg is the great-circle bearing from source to dest node and
    wad uses the "going-to" convention (0° = North, 90° = East).

    NOTE: this is O(n_edges) in pure Python — ~18M iterations for a global
    0.25° graph. Replace with pybind11 call to C++ when bindings exist.
    """
    import math

    n_edges = graph["n_edges"]
    weights = np.empty(n_edges, dtype=np.uint32)

    row_ptr      = graph["row_ptr"]
    col_idx      = graph["col_idx"]
    base_dist_nm = graph["base_dist_nm"]
    lat_arr      = graph["lat"]
    lon_arr      = graph["lon"]

    SCALE          = 1000.0
    MAX_W          = 0xFFFFFFFE
    WIND_REF_SPEED = 20.0

    for u in range(graph["n_nodes"]):
        lat_u = float(lat_arr[u])
        lon_u = float(lon_arr[u])
        wx_lon_u = lon_u if lon_u >= 0.0 else lon_u + 360.0

        lat_i = max(0, min(int((90.0 - lat_u) / 0.25), 720))
        lon_i = int(wx_lon_u / 0.25) % 1440

        sig_wh_val = sigwh[lat_i, lon_i]
        sig_wh = 0.0 if np.isnan(sig_wh_val) else float(sig_wh_val)

        was_val_raw = was[lat_i, lon_i]
        was_val = 0.0 if np.isnan(was_val_raw) else float(was_val_raw)

        wad_val_raw = wad[lat_i, lon_i]
        wad_rad = 0.0 if np.isnan(wad_val_raw) else math.radians(float(wad_val_raw))

        for e in range(row_ptr[u], row_ptr[u + 1]):
            v = int(col_idx[e])
            lat_v = float(lat_arr[v])
            lon_v = float(lon_arr[v])

            # Great-circle bearing from u to v (radians, clockwise from North)
            dlon = math.radians(lon_v - lon_u)
            rlat_u = math.radians(lat_u)
            rlat_v = math.radians(lat_v)
            hdg = math.atan2(
                math.sin(dlon) * math.cos(rlat_v),
                math.cos(rlat_u) * math.sin(rlat_v)
                - math.sin(rlat_u) * math.cos(rlat_v) * math.cos(dlon),
            )

            # Headwind penalty: positive when vessel heads into the wind
            headwind_f = max(0.0, -was_val * math.cos(hdg - wad_rad)) / WIND_REF_SPEED

            proxy = float(base_dist_nm[e]) * (1.0 + sig_wh / 3.0 + headwind_f)
            w = max(1, min(int(proxy * SCALE), MAX_W))
            weights[e] = w

    return weights


def write_weights_bin(weights: np.ndarray, base_epoch: int, out_path: str):
    """
    Writes weights.bin matching WeightsHeader in weather_etl/src/weights_writer.hpp.
    Header: magic(4) + version(4) + n_edges(4) + reserved(4) + base_epoch(8) = 24 bytes
    """
    with open(out_path, "wb") as f:
        f.write(struct.pack("<IIIIq",
                            WEIGHTS_MAGIC, WEIGHTS_VERSION,
                            len(weights), 0, base_epoch))
        f.write(weights.tobytes())


def run(args):
    s3 = boto3.client("s3")

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)

        # 1. Download .npy files
        log.info("Fetching .npy files from s3://%s/%s", args.npy_bucket, args.npy_prefix)
        fetch_npy_files(s3, args.npy_bucket, args.npy_prefix, tmp)

        # 2. Load weather variables needed for weight computation
        for required in ("sigwh.npy", "was.npy", "wad.npy"):
            if not (tmp / required).exists():
                raise FileNotFoundError(f"{required} not found — required for weight computation")
        sigwh = np.load(str(tmp / "sigwh.npy"))
        was   = np.load(str(tmp / "was.npy"))
        wad   = np.load(str(tmp / "wad.npy"))
        log.info("sigwh shape: %s  dtype: %s", sigwh.shape, sigwh.dtype)

        # 3. Download graph.bin for CSR structure
        graph_local = tmp / "graph.bin"
        s3.download_file(args.graph_bucket, args.graph_key, str(graph_local))
        graph = load_graph_csr(str(graph_local))
        log.info("Graph: %d nodes, %d edges", graph["n_nodes"], graph["n_edges"])

        # 4. Compute weights
        log.info("Computing edge weights...")
        t0 = time.perf_counter()
        weights = compute_weights(graph, sigwh, was, wad)
        log.info("Weights computed in %.1f s", time.perf_counter() - t0)

        # 5. Write weights.bin
        weights_local = str(tmp / "weights.bin")
        base_epoch = int(time.time())
        write_weights_bin(weights, base_epoch, weights_local)
        log.info("weights.bin written: %d edges, base_epoch=%d", len(weights), base_epoch)

        # 6. Upload weights.bin to S3
        s3.upload_file(weights_local, args.out_bucket, args.out_key)
        log.info("Uploaded to s3://%s/%s", args.out_bucket, args.out_key)


def _local_main(argv=None) -> int:
    """
    Local mode: delegates to the maritime-weights-writer C++ binary.
    Faster than the Python compute path (avoids 18M Python iterations).
    Use this for local development or when graph artifacts are on disk.
    """
    import subprocess
    parser = argparse.ArgumentParser(
        description="Maritime weather ETL — local mode (calls C++ binary)"
    )
    parser.add_argument("--graph",  required=True)
    parser.add_argument("--flags",  required=True)
    parser.add_argument("--snap",   required=True)
    parser.add_argument("--npy",    required=True, help="directory containing {var}.npy files")
    parser.add_argument("--out",    required=True, help="output path for weights.bin")
    parser.add_argument("--binary", default="maritime-weights-writer")
    parser.add_argument("--step",   default="0")
    parser.add_argument("--epoch",  default=str(int(time.time())))
    args = parser.parse_args(argv)

    cmd = [
        args.binary,
        "--graph",  args.graph,
        "--flags",  args.flags,
        "--snap",   args.snap,
        "--npy",    args.npy,
        "--out",    args.out,
        "--step",   args.step,
        "--epoch",  args.epoch,
    ]
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    import sys as _sys
    # If --local flag is present, use the C++ binary path; otherwise use S3 mode.
    if "--local" in _sys.argv:
        _sys.argv.remove("--local")
        _sys.exit(_local_main())
    else:
        parser = argparse.ArgumentParser()
        parser.add_argument("--npy-bucket",   required=True, help="S3 bucket containing .npy files")
        parser.add_argument("--npy-prefix",   required=True, help="S3 prefix for .npy files (e.g. process/2026/06/09/18/)")
        parser.add_argument("--graph-bucket", required=True, help="S3 bucket containing graph.bin")
        parser.add_argument("--graph-key",    default="artifacts/graph.bin")
        parser.add_argument("--out-bucket",   required=True, help="S3 bucket for weights.bin output")
        parser.add_argument("--out-key",      default="weights/weights.bin")
        run(parser.parse_args())