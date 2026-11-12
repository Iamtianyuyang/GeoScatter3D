import importlib.util
import pathlib
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "scripts/check_include_deps.py"
spec = importlib.util.spec_from_file_location("check_include_deps", SCRIPT)
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


class EngineDependencyTests(unittest.TestCase):
    def test_engine_layers_and_modules(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)

            def source(path, includes):
                target = root / "engine" / path
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text("".join(f'#include "engine/{item}"\n' for item in includes))

            source("data/format/read.cpp", [
                "data/format/read.hpp", "foundation/io/file.hpp",
                "render/device/buffer.hpp", "data/tile/tile.hpp",
            ])
            source("cli/main.cpp", [
                "config/config.hpp", "pipeline/stages/task.hpp", "render/device/gpu.hpp",
            ])
            source("session/view/view.cpp", ["config/config.hpp", "abi/api.h"])
            source("foundation/io/file.cpp", ["unknown/missing.hpp"])
            relative = root / "engine/data/format/relative.cpp"
            relative.write_text('#include "../../render/device/buffer.hpp"\n'
                                '#include "engine/data/format/../../render/device/buffer.hpp"\n'
                                '#include "render/device/buffer.hpp"\n')
            violations = checker.collect_engine_violations(root)
            self.assertEqual(len(violations), 8, violations)
            self.assertTrue(any("data/format/read.cpp:3" in item for item in violations))
            self.assertTrue(any("data/format/read.cpp:4" in item for item in violations))
            self.assertTrue(any("cli/main.cpp:3" in item for item in violations))
            self.assertTrue(any("session/view/view.cpp:2" in item for item in violations))
            self.assertTrue(any("foundation/io/file.cpp:1" in item for item in violations))
            self.assertEqual(sum("relative.cpp:" in item for item in violations), 3)


if __name__ == "__main__":
    unittest.main()
