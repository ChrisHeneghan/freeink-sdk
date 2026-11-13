#!/usr/bin/env python3
from pathlib import Path
import subprocess
import tempfile

here = Path(__file__).resolve().parent
sdk = here.parents[4]
with tempfile.TemporaryDirectory(prefix="freeink-opds-") as directory:
    binary = str(Path(directory) / "test_feed_windows")
    includes = [here / "stubs", sdk / "libs/network/SecureNet/test/host/stubs",
                sdk / "libs/network/Opds/include", sdk / "libs/network/JsonSax/include"]
    sources = [sdk / "libs/network/Opds/src" / name for name in
               ("OpdsParser.cpp", "Opds2Parser.cpp", "OpdsFeedParser.cpp", "OpdsFeedWindow.cpp")]
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                    *["-I" + str(path) for path in includes], str(here / "test_feed_windows.cpp"),
                    *map(str, sources), str(sdk / "libs/network/JsonSax/src/StreamingJsonParser.cpp"),
                    "-lexpat", "-o", binary], check=True)
    subprocess.run([binary], check=True)
    client_binary = str(Path(directory) / "test_client")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-fsanitize=address,undefined",
                    *["-I" + str(path) for path in includes], str(here / "test_client.cpp"),
                    *map(str, sorted((sdk / "libs/network/Opds/src").glob("*.cpp"))),
                    str(sdk / "libs/network/JsonSax/src/StreamingJsonParser.cpp"),
                    "-lexpat", "-o", client_binary], check=True)
    subprocess.run([client_binary], check=True)
