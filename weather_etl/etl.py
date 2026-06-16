#!/usr/bin/env python3
"""
weather_etl/etl.py

Scheduled job: computes CCH edge weights for one calendar voyage date and
writes weights.bin for router_server to consume.

The weather source is the daily-average dataset (see
average_weather_description.md): one energy-averaged snapshot per day,
computed once for 2024 and mapped onto any real calendar date by matching
month/day (2024 is a leap year, so every date — including Feb 29 — resolves).
There is no live forecast cycle to poll anymore; --date selects which day's
average weather to use.

Delegates all graph loading, weather loading, and weight computation to the
maritime_weather_etl pybind11 bindings (see weather_etl/python/bindings.cpp),
which forward to the same C++ implementation used by the maritime-weights-
writer CLI and router_server's plan_voyage(). This script does not
reimplement any of that logic in Python.
"""

import argparse
import datetime
import logging
import tempfile
import time
from pathlib import Path

import boto3

import maritime_weather_etl as mwe

logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
log = logging.getLogger(__name__)

# The 8 fields the average-weather dataset publishes per day — see
# average_weather_description.md Section 3. Wave grid: sigwh, wsh, wsp, wsd,
# pwd, swell_residual (621x1440). Wind grid: was, wad (721x1440).
AVG_WEATHER_FIELDS = ["sigwh", "wsh", "wsp", "wsd", "pwd", "swell_residual", "was", "wad"]

ARTIFACT_FILES = ["graph.bin", "flags.bin", "snap_wave.bin", "snap_wind.bin"]


def fetch_artifacts(s3: object, bucket: str, prefix: str, local_dir: Path) -> dict:
    """Downloads graph.bin/flags.bin/snap_wave.bin/snap_wind.bin from
    s3://bucket/prefix/<file> into local_dir. Returns a dict of local paths."""
    paths = {}
    for name in ARTIFACT_FILES:
        dest = local_dir / name
        s3.download_file(bucket, f"{prefix.rstrip('/')}/{name}", str(dest))
        paths[name] = str(dest)
        log.info("Downloaded %s/%s", prefix, name)
    return paths


def fetch_avg_weather_day(
    s3: object, bucket: str, prefix: str, local_dir: Path,
    year: int, month: int, day: int,
) -> None:
    """Downloads one calendar day's 8 average-weather fields, mapped onto
    2024 (the only year the dataset covers), into
    local_dir/2024/<MM>/<DD>/<field>.npy — the layout AvgWeatherLoader
    expects."""
    mm, dd = f"{month:02d}", f"{day:02d}"
    dest_dir = local_dir / "2024" / mm / dd
    dest_dir.mkdir(parents=True, exist_ok=True)
    for field in AVG_WEATHER_FIELDS:
        dest = dest_dir / f"{field}.npy"
        key = f"{prefix.rstrip('/')}/2024/{mm}/{dd}/{field}.npy"
        s3.download_file(bucket, key, str(dest))
        log.info("Downloaded %s", key)
    log.info("Fetched average weather for %04d-%02d-%02d (mapped onto 2024-%s-%s)",
              year, month, day, mm, dd)


def write_weights_bin(weights, base_epoch: int, out_path: str) -> None:
    mwe.WeightsWriter.write(weights, base_epoch, out_path)


def run(args):
    s3 = boto3.client("s3")

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = Path(tmpdir)

        # 1. Download graph artifacts (graph.bin, flags.bin, snap_wave.bin, snap_wind.bin)
        log.info("Fetching graph artifacts from s3://%s/%s", args.artifacts_bucket, args.artifacts_prefix)
        artifact_paths = fetch_artifacts(s3, args.artifacts_bucket, args.artifacts_prefix, tmp)
        graph = mwe.StaticGraph(
            artifact_paths["graph.bin"], artifact_paths["flags.bin"],
            artifact_paths["snap_wave.bin"], artifact_paths["snap_wind.bin"],
        )
        log.info("Graph: %d nodes, %d edges", graph.n_nodes, graph.n_edges)

        # 2. Download the target date's average-weather fields
        weather_dir = tmp / "weather"
        log.info("Fetching average weather for %s from s3://%s/%s",
                  args.date.isoformat(), args.avg_weather_bucket, args.avg_weather_prefix)
        fetch_avg_weather_day(
            s3, args.avg_weather_bucket, args.avg_weather_prefix, weather_dir,
            args.date.year, args.date.month, args.date.day)

        # base_epoch identifies which date's weather this weights.bin was
        # computed from — QueryServer derives the date to display from this
        # field, so it must be the target date's midnight UTC, not "now".
        base_epoch = int(datetime.datetime(
            args.date.year, args.date.month, args.date.day,
            tzinfo=datetime.timezone.utc).timestamp())
        wx = mwe.load_avg_weather_buffer(
            str(weather_dir), args.date.year, args.date.month, args.date.day, base_epoch)

        # 3. Compute weights
        log.info("Computing edge weights...")
        t0 = time.perf_counter()
        weights = mwe.WeightsWriter.compute(graph, wx, args.step)
        log.info("Weights computed in %.1f s", time.perf_counter() - t0)

        # 4. Write weights.bin
        weights_local = str(tmp / "weights.bin")
        write_weights_bin(weights, base_epoch, weights_local)
        log.info("weights.bin written: %d edges, base_epoch=%d", len(weights), base_epoch)

        # 5. Upload weights.bin to S3
        s3.upload_file(weights_local, args.out_bucket, args.out_key)
        log.info("Uploaded to s3://%s/%s", args.out_bucket, args.out_key)


def _local_main(argv=None) -> int:
    """
    Local mode: delegates to the maritime-weights-writer C++ binary.
    Use this for local development or when graph artifacts and the
    average-weather dataset are already on disk (e.g. the mounted dataset
    described in average_weather_description.md).
    """
    import subprocess
    parser = argparse.ArgumentParser(
        description="Maritime weather ETL — local mode (calls C++ binary)"
    )
    parser.add_argument("--graph",            required=True)
    parser.add_argument("--flags",             required=True)
    parser.add_argument("--snap-wave",         required=True)
    parser.add_argument("--snap-wind",         required=True)
    parser.add_argument("--avg-weather-dir",   required=True,
                         help="base directory containing <YYYY>/<MM>/<DD>/<field>.npy")
    parser.add_argument("--date",              required=True, help="YYYY-MM-DD")
    parser.add_argument("--out",               required=True, help="output path for weights.bin")
    parser.add_argument("--binary", default="maritime-weights-writer")
    parser.add_argument("--step",   default="0")
    parser.add_argument("--epoch",  default=None,
                         help="base_epoch override; default (recommended) lets the "
                              "C++ binary derive it from --date (midnight UTC)")
    args = parser.parse_args(argv)

    cmd = [
        args.binary,
        "--graph",           args.graph,
        "--flags",           args.flags,
        "--snap-wave",       args.snap_wave,
        "--snap-wind",       args.snap_wind,
        "--avg-weather-dir", args.avg_weather_dir,
        "--date",            args.date,
        "--out",             args.out,
        "--step",            args.step,
    ]
    if args.epoch is not None:
        cmd += ["--epoch", args.epoch]
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    import sys as _sys
    # If --local flag is present, use the C++ binary path; otherwise use S3 mode.
    if "--local" in _sys.argv:
        _sys.argv.remove("--local")
        _sys.exit(_local_main())
    else:
        parser = argparse.ArgumentParser()
        parser.add_argument("--avg-weather-bucket", required=True,
                             help="S3 bucket containing the average-weather dataset")
        parser.add_argument("--avg-weather-prefix", default="average-weather",
                             help="S3 prefix above <YYYY>/<MM>/<DD>/<field>.npy")
        parser.add_argument("--date", required=True, type=datetime.date.fromisoformat,
                             help="voyage date, YYYY-MM-DD (mapped onto the 2024 dataset)")
        parser.add_argument("--artifacts-bucket", required=True,
                             help="S3 bucket containing graph.bin/flags.bin/snap_wave.bin/snap_wind.bin")
        parser.add_argument("--artifacts-prefix", default="artifacts")
        parser.add_argument("--out-bucket",   required=True, help="S3 bucket for weights.bin output")
        parser.add_argument("--out-key",      default="weights/weights.bin")
        parser.add_argument("--step", type=int, default=0, help="ref_time_step [0..23]")
        run(parser.parse_args())
