import argparse
import json
import sys

from .project import Project, load
from .server import serve


def main():
    parser = argparse.ArgumentParser(description="EmbSIM digital interaction foundation")
    sub = parser.add_subparsers(dest="command", required=True)
    run = sub.add_parser("run", help="run through a virtual timestamp and emit JSON")
    run.add_argument("project")
    run.add_argument("--until-ns", type=int, required=True)
    ui = sub.add_parser("serve", help="start the local graphical interface")
    ui.add_argument("project")
    ui.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    try:
        data = load(args.project)
        if args.command == "serve":
            if not 0 <= args.port <= 65535:
                raise ValueError("port must be between 0 and 65535")
            serve(data, args.port)
        else:
            project = Project(data)
            try:
                project.advance(args.until_ns)
                print(json.dumps(project.snapshot(), sort_keys=True, indent=2))
            finally:
                project.close()
    except (ValueError, TypeError, OSError, RuntimeError) as error:
        print(f"embsim: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
