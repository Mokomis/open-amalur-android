"""Run the retained catalog and lifter profile on one local PE32 input."""
import argparse
from pathlib import Path
import re
import sys

import catalog
import gen_module
import lift

import cpu_additions
import cpuflags_adapter
import noreturn_adapter
import qualified_sse_adapter
import runner_extensions
import setjmp_adapter


def main():
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("module", choices=("amalur", "bink", "compiler43"))
    cli.add_argument("input", type=Path)
    cli.add_argument("--llvm", type=Path, required=True)
    cli.add_argument("--output", type=Path, required=True)
    cli.add_argument("--include-dir", type=Path, required=True)
    args = cli.parse_args()

    args.output.mkdir(parents=True, exist_ok=False)
    sys.argv = ["catalog", str(args.input), "--llvm", str(args.llvm), "--out", str(args.output)]
    catalog.main()

    lift.SCRIPT_CALL = -1
    lift.FONT_PAGE = -1
    original_addr = lift.Op._addr

    def checked_addr(expression):
        try:
            return original_addr(re.sub(r"\beiz\b", "0", expression))
        except ValueError as error:
            raise lift.Unsupported(f"address parser: {error}") from error

    lift.Op._addr = staticmethod(checked_addr)
    cpuflags_adapter.install(lift)
    runner_extensions.install(lift)
    if args.module != "amalur":
        cpu_additions.install(lift)
    qualified_sse_adapter.install(lift, args.include_dir)
    gen_module.HEADER += '#include "oa_qualified_sse.h"\n'
    if args.module == "amalur":
        noreturn_adapter.install(lift, args.output)
        setjmp_adapter.install(lift, args.output)
    else:
        gen_module.HEADER += '#include "cpu_additions.h"\n'
    sys.argv = ["gen_module", str(args.input), "--llvm", str(args.llvm),
                "--catalog", str(args.output / "catalog.json"), "--name", args.module,
                "--out", str(args.output), "--shard-bytes", "1000000"]
    gen_module.main()


if __name__ == "__main__":
    main()
