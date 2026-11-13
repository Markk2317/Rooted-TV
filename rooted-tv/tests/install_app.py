"""assets/dashboard-app/install-app.sh against a stand-in luna-send and app tree.

The dashboard tile and the splash are the native client, installed whole from a
folder staged by tools/runtime_payload.py; the old Home app is a web page. The
stand-in install service unpacks the package the script builds, so the tests
look at what the TV would hold, and at what a refresh then leaves alone.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "assets/dashboard-app/install-app.sh"

# Stands in for luna-send: the install service reads the package the script
# built (an ar of control, debian-binary and data tarballs) into the stand-in
# apps tree; applicationManager/running lists what $RUNNING_APPS names, or the
# file $RUNNING_FILE, which closeByAppId edits; getForegroundAppInfo answers
# $FOREGROUND_APP; closing and launching are written to $CALLS_LOG. All answer
# in luna-send's compact JSON, which the script greps.
LUNA_SEND = r"""#!/usr/bin/env python3
import json, os, sys, tarfile, io
args = sys.argv[1:]
uri = next((a for a in args if a.startswith("luna://")), "")
running_file = os.environ.get("RUNNING_FILE")
def running():
    if running_file:
        return open(running_file).read().split() if os.path.exists(running_file) else []
    return os.environ.get("RUNNING_APPS", "").split()
def called(line):
    with open(os.environ["CALLS_LOG"], "a") as log:
        log.write(line + "\n")
if uri.endswith("applicationManager/running"):
    ids = running()
    print(json.dumps({"returnValue": True, "running": [{"id": i} for i in ids]}, separators=(",", ":")))
elif uri.endswith("applicationManager/getForegroundAppInfo"):
    print(json.dumps({"returnValue": True, "appId": os.environ.get("FOREGROUND_APP", "")}, separators=(",", ":")))
elif uri.endswith("applicationManager/closeByAppId"):
    closed = json.loads(args[-1])["id"]
    open(running_file, "w").write(" ".join(i for i in running() if i != closed))
    called("close " + closed)
    print(json.dumps({"returnValue": True}, separators=(",", ":")))
elif uri.endswith("applicationManager/launch"):
    called("launch " + args[-1])
    print(json.dumps({"returnValue": True}, separators=(",", ":")))
elif uri.endswith("appInstallService/dev/install"):
    package = json.loads(args[-1])["ipkUrl"]
    data = open(package, "rb").read()
    offset, members = 8, {}
    while offset < len(data):
        name = data[offset:offset + 16].decode().strip()
        size = int(data[offset + 48:offset + 58])
        members[name] = data[offset + 60:offset + 60 + size]
        offset += 60 + size + size % 2
    base = os.environ["TVWEB_APPS_ROOT"]
    with tarfile.open(fileobj=io.BytesIO(members["data.tar.gz"]), mode="r:gz") as archive:
        archive.extractall(os.path.dirname(os.path.dirname(base)))
    with open(os.environ["INSTALL_LOG"], "a") as log:
        log.write(os.path.basename(package) + "\n")
    print(json.dumps({"returnValue": True, "details": {"state": "installed"}}, separators=(",", ":")))
"""


class InstallAppTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.mkdtemp(prefix="rootedtv-install-app-")
        self.addCleanup(shutil.rmtree, self.folder, True)
        root = Path(self.folder)
        self.apps = root / "media/developer/apps/usr/palm"
        (root / "bin").mkdir()
        (root / "bin/luna-send").write_text(LUNA_SEND)
        (root / "bin/luna-send").chmod(0o755)
        self.environment = dict(
            os.environ, PATH=str(root / "bin") + os.pathsep + os.environ["PATH"],
            TVWEB_APPS_ROOT=str(self.apps), INSTALL_LOG=str(root / "installs"),
            RUNNING_APPS="", COPYFILE_DISABLE="1")  # no AppleDouble files from a Mac's tar
        self.dashboard = root / "assets/dashboard-app"
        self.splash = root / "assets/splash-app"
        self.home = root / "assets/home-app"
        for app, identifier in ((self.dashboard, "com.tvweb.dashboard"),
                                (self.splash, "com.tvweb.splash")):
            self.put(app / "appinfo.json", json.dumps({
                "id": identifier, "type": "native", "main": "rootedtv-tv"}, indent=2))
            self.put(app / "packageinfo.json", json.dumps({"id": identifier}))
            self.put(app / "rootedtv-tv", "client v1", 0o755)
            self.put(app / "runtime.json", "{}")
            self.put(app / "libcurl.so.4", "curl")
            self.put(app / "LICENSE-json-c.txt", "json-c")
            self.put(app / "assets/icon80.png", "icon")
            self.put(app / "assets/fonts/GoogleSansFlex-Regular.ttf", "font")
            self.put(app / "assets/icons/material/home.svg", "<svg/>")
        self.put(self.dashboard / "install-app.sh", SCRIPT.read_text(), 0o755)
        self.put(self.home / "appinfo.json", json.dumps({
            "id": "com.tvweb.home", "type": "web", "main": "index.html"}, indent=2))
        self.put(self.home / "packageinfo.json", json.dumps({"id": "com.tvweb.home"}))
        self.put(self.home / "index.html", "<script>var PORT = 9999; // tvweb:port</script>")

    def put(self, path, text, mode=0o644):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        path.chmod(mode)

    def run_script(self, action, source=None):
        command = ["sh", str(self.dashboard / "install-app.sh"), action,
                   str(source or self.dashboard)]
        return subprocess.run(command, env=self.environment, capture_output=True,
                              text=True, timeout=60).stdout.strip()

    def installed(self, identifier):
        return self.apps / "applications" / identifier

    def test_status_needs_the_client_executable(self):
        self.assertEqual(json.loads(self.run_script("status")),
                         {"ok": True, "supported": True, "installed": False})
        (self.dashboard / "rootedtv-tv").unlink()
        self.assertEqual(json.loads(self.run_script("status"))["supported"], False)
        self.assertIn("app files not found", self.run_script("install"))
        self.assertFalse(self.installed("com.tvweb.dashboard").exists())

    def test_install_puts_the_whole_client_in_the_app(self):
        self.assertEqual(self.run_script("install"), "dashboard added to the home screen")
        app = self.installed("com.tvweb.dashboard")
        files = sorted(str(path.relative_to(app)) for path in app.rglob("*") if path.is_file())
        self.assertEqual(files, [
            "LICENSE-json-c.txt", "appinfo.json", "assets/fonts/GoogleSansFlex-Regular.ttf",
            "assets/icon80.png", "assets/icons/material/home.svg", "libcurl.so.4",
            "rootedtv-tv", "runtime.json"])
        self.assertTrue(os.access(app / "rootedtv-tv", os.X_OK))
        self.assertTrue((self.apps / "packages/com.tvweb.dashboard/packageinfo.json").is_file())
        self.assertEqual(json.loads(self.run_script("status")),
                         {"ok": True, "supported": True, "installed": True})

    def test_splash_is_installed_from_its_own_folder(self):
        self.assertEqual(self.run_script("install", self.splash),
                         "native startup splash added to the home screen")
        self.assertTrue((self.installed("com.tvweb.splash") / "rootedtv-tv").is_file())
        self.assertFalse(self.installed("com.tvweb.dashboard").exists())

    def test_refresh_leaves_a_current_app_alone_and_replaces_a_stale_one(self):
        self.run_script("install")
        installs = Path(self.environment["INSTALL_LOG"])
        self.assertEqual(len(installs.read_text().splitlines()), 1)
        self.assertEqual(json.loads(self.run_script("refresh")),
                         {"ok": True, "installed": True, "refreshed": False})
        self.assertEqual(len(installs.read_text().splitlines()), 1, "no reinstall")

        self.put(self.dashboard / "assets/fonts/GoogleSansFlex-Regular.ttf", "font v2")
        self.environment["RUNNING_APPS"] = "com.tvweb.dashboard"
        self.assertEqual(json.loads(self.run_script("refresh"))["reason"], "running")
        self.assertEqual((self.installed("com.tvweb.dashboard") /
                          "assets/fonts/GoogleSansFlex-Regular.ttf").read_text(), "font")

        self.environment["RUNNING_APPS"] = ""
        self.assertEqual(self.run_script("refresh"), "dashboard added to the home screen")
        self.assertEqual((self.installed("com.tvweb.dashboard") /
                          "assets/fonts/GoogleSansFlex-Regular.ttf").read_text(), "font v2")
        self.assertEqual(json.loads(self.run_script("refresh"))["refreshed"], False)

    def test_refresh_open_closes_and_reopens_a_stale_open_tile_on_webos_3_only(self):
        self.run_script("install")
        root = Path(self.folder)
        running, calls, issue = root / "running", root / "calls", root / "issue"
        self.environment.update(RUNNING_FILE=str(running), CALLS_LOG=str(calls),
                                FOREGROUND_APP="com.tvweb.dashboard", TVWEB_ISSUE=str(issue))
        running.write_text("com.tvweb.dashboard")
        self.put(self.dashboard / "rootedtv-tv", "client v2", 0o755)
        client = self.installed("com.tvweb.dashboard") / "rootedtv-tv"

        self.put(issue, "webOS TV 4.4.0 \\n \\l\n")
        self.assertEqual(json.loads(self.run_script("refresh-open"))["reason"], "running")
        self.assertFalse(calls.exists(), "never closed from outside on webOS 4")
        self.assertEqual(client.read_text(), "client v1")

        self.put(issue, "webOS TV 3.4.3 \\n \\l\n")
        self.assertEqual(self.run_script("refresh-open"), "dashboard added to the home screen")
        self.assertEqual(client.read_text(), "client v2")
        self.assertEqual(calls.read_text().splitlines(), [
            "close com.tvweb.dashboard",
            'launch {"id":"com.tvweb.dashboard","params":{"screen":"home"}}'])

        calls.unlink()
        running.write_text("com.tvweb.dashboard")
        self.assertEqual(json.loads(self.run_script("refresh-open"))["reason"], "running")
        self.assertFalse(calls.exists(), "a current app is left open")

        self.put(self.dashboard / "rootedtv-tv", "client v3", 0o755)
        self.environment["FOREGROUND_APP"] = "netflix"
        self.assertEqual(self.run_script("refresh-open"), "dashboard added to the home screen")
        self.assertEqual(calls.read_text().splitlines(), ["close com.tvweb.dashboard"],
                         "not opened again when something else was in front")

    def test_refresh_does_not_install_a_removed_app(self):
        self.assertEqual(json.loads(self.run_script("refresh")),
                         {"ok": True, "installed": False, "refreshed": False})
        self.assertFalse(self.installed("com.tvweb.dashboard").exists())

    def test_web_app_keeps_its_port_and_is_not_touched_by_refresh(self):
        self.run_script("install", self.home)
        page = self.installed("com.tvweb.home") / "index.html"
        self.assertIn("var PORT = 8080; // tvweb:port", page.read_text())
        self.assertEqual(json.loads(self.run_script("refresh", self.home))["refreshed"], False)


if __name__ == "__main__":
    unittest.main()
