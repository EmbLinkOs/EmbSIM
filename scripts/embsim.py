"""Run clients from a checkout without setting PYTHONPATH."""
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "python"))

from embsim.__main__ import main

if __name__ == "__main__":
    sys.exit(main())
