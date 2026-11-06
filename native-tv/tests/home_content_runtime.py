"""Exercise native Home metadata routes with TMDB and Netflix disabled."""
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


def unused_port_pair(excluded=()):
    excluded = set(excluded)
    first_port = 20000 + ((os.getpid() * 7919) % 39000)
    for offset in range(39000):
        port = 20000 + ((first_port - 20000 + offset) % 39000)
        if any(port + index in excluded for index in range(3)):
            continue
        first = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        second = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            first.bind(("127.0.0.1", port))
            second.bind(("127.0.0.1", port + 1))
            return port, port + 1
        except OSError:
            pass
        finally:
            first.close()
            second.close()
    raise RuntimeError("could not reserve consecutive test ports")


def get(port, path, token):
    req = Request("http://127.0.0.1:%d%s" % (port, path),
                  headers={"Authorization": "Bearer " + token})
    try:
        with urlopen(req, timeout=5) as response:
            return response.status, response.read()
    except HTTPError as error:
        return error.code, error.read()


def post(port, path, token, payload):
    req = Request("http://127.0.0.1:%d%s" % (port, path), data=json.dumps(payload).encode(),
                  headers={"Authorization": "Bearer " + token,
                           "Content-Type": "application/json"})
    try:
        with urlopen(req, timeout=5) as response:
            return response.status, response.read()
    except HTTPError as error:
        return error.code, error.read()


def main():
    binary = Path(sys.argv[1]).resolve()
    fake_luna = Path(sys.argv[2]).resolve()
    token = "home-content-runtime-token"
    port, _ = unused_port_pair()
    node_port, _ = unused_port_pair((port, port + 1))
    with tempfile.TemporaryDirectory(prefix="nativetv-home-content-") as folder:
        root = Path(folder)
        assets = root / "assets"
        assets.mkdir()
        config = root / "config.json"
        config.write_text(json.dumps({
            "host": "127.0.0.1", "port": port, "token": token,
            "allowControl": True
        }))
        environment = os.environ.copy()
        environment["TVWEB_LUNA_SEND"] = str(fake_luna)
        environment["TVWEB_DATA_DIR"] = str(root)
        environment["TVWEB_PROXY_SECRET"] = "home-content-proxy-secret-0123456789"
        process = subprocess.Popen(
            [str(binary), "--config", str(config), "--heartbeat",
             str(root / "tvweb.beat"), "--workers", "4", "--listen",
             "--node-port", str(node_port), "--assets-root", str(assets)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
            env=environment)
        try:
            deadline = time.monotonic() + 8
            ready = False
            while time.monotonic() < deadline and process.poll() is None:
                try:
                    status, _ = get(port, "/api/caps", token)
                    if status == 200:
                        ready = True
                        break
                except (OSError, URLError):
                    pass
                time.sleep(0.03)
            if not ready:
                output = process.communicate(timeout=5)[0] if process.poll() is not None else ""
                raise AssertionError("native daemon did not start:\n" + output[-4000:])

            paths = [
                "/api/home/details?type=movie&id=7",
                "/api/home/search?q=film",
                "/api/home/netflix?type=movie&id=7",
            ]
            for path in paths:
                status, body = get(port, path, token)
                assert status == 200
                assert json.loads(body) == {"ok": True, "enabled": False}

            status, body = get(port, "/api/home/discover", token)
            assert status == 200
            assert json.loads(body) == {
                "ok": True, "enabled": False, "rows": []
            }

            # A title from the TV app's details goes to the watchlist of the profile in use, cut to
            # the fields Home shows; anything but a film or series is refused.
            title = {"id": 7, "type": "movie", "title": "Coast", "year": "2026",
                     "poster": "/p.jpg", "service": "prime"}
            status, body = post(port, "/api/control", token,
                                {"action": "setHomeTitle",
                                 "value": {"op": "watch", "item": title}})
            assert status == 200 and json.loads(body)["ok"] is True, body
            saved = json.loads((root / "home-titles.json").read_text())
            assert saved["main"]["watchlist"][0]["title"] == "Coast"
            assert "service" not in saved["main"]["watchlist"][0]
            assert oct((root / "home-titles.json").stat().st_mode & 0o777) == "0o600"
            status, body = post(port, "/api/control", token,
                                {"action": "setHomeTitle",
                                 "value": {"op": "watch", "item": {"id": 7, "type": "person"}}})
            assert json.loads(body)["ok"] is False

            # The Photos screen saver: a JPEG goes up, is listed, read back by number (with the
            # token in k, as the saver asks) and removed; with no town, there is no weather.
            jpeg = (b"\xff\xd8\xff\xdb\x00\x43" + b"\0" * 65 + b"\xff\xc4\x00\x14" + b"\0" * 18 +
                    b"\xff\xc0\x00\x0b\x08\x00\x01\x00\x01\x01\x01\x11\x00" +
                    b"\xff\xda\x00\x08\x01\x01\x00\x00\x3f\x00\x01\xff\xd9")
            req = Request("http://127.0.0.1:%d/api/screensaver/photos/upload" % port, data=jpeg,
                          headers={"Authorization": "Bearer " + token,
                                   "Content-Type": "image/jpeg"})
            with urlopen(req, timeout=5) as response:
                listed = json.loads(response.read())
            assert listed["ok"] and listed["photos"]["ids"] == [1], listed
            with urlopen("http://127.0.0.1:%d/api/screensaver/photo?id=1&k=%s" % (port, token),
                         timeout=5) as response:
                assert response.read() == jpeg
                assert response.headers["Content-Type"] == "image/jpeg"
            status, body = post(port, "/api/screensaver/photos/upload", token, {"id": 1})
            assert status == 415
            status, body = get(port, "/api/screensaver/weather", token)
            assert status == 200 and json.loads(body)["ok"] is False
            status, body = post(port, "/api/screensaver/photos/remove", token, {"id": 0})
            assert status == 200 and json.loads(body)["photos"]["count"] == 0
            status, body = get(port, "/api/screensaver/photo?id=1", token)
            assert status == 404

            status, body = get(
                port,
                "/api/home/image?u=https%3A%2F%2Fevil.example%2Fimage.jpg",
                token)
            assert status == 404
            assert json.loads(body) == {"ok": False, "error": "no picture"}
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
                try:
                    process.communicate(timeout=6)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=3)


if __name__ == "__main__":
    main()
