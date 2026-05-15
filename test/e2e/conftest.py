import os
from pathlib import Path

import pytest
from tontester.install import Install

REPO_ROOT = Path(__file__).resolve().parents[2]


def pytest_addoption(parser: pytest.Parser):
    parser.addoption(
        "--build-dir",
        default=None,
        help="Path to the ton build directory (default: $TON_BUILD_DIR or <repo>/build)",
    )


@pytest.fixture(scope="session")
def install(pytestconfig: pytest.Config):
    build_dir = (
        pytestconfig.getoption("--build-dir")
        or os.environ.get("TON_BUILD_DIR")
        or str(REPO_ROOT / "build")
    )
    inst = Install(Path(build_dir), REPO_ROOT)
    inst.tonlibjson.client_set_verbosity_level(3)
    return inst
