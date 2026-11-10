#!/usr/bin/env python3
"""Usage: make_manifest.py <qt-android-prefix> <output AndroidManifest.xml>

Takes the AndroidManifest.xml template shipped with the installed Qt (so it always matches that Qt version) and
adds the permissions Eden needs: internet for the AI providers and web search, microphone and camera for
recording comments. Nothing else in the template is changed.
"""
import pathlib
import re
import sys

qt_prefix, output = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
candidates = [
    qt_prefix / "src/android/templates/AndroidManifest.xml",
    qt_prefix / "src/android/java/AndroidManifest.xml",
]
template = next((c for c in candidates if c.exists()), None)
if template is None:
    found = [str(p) for p in qt_prefix.rglob("AndroidManifest.xml")][:10]
    sys.exit(f"Qt manifest template not found under {qt_prefix}; found: {found}")

text = template.read_text(encoding="utf-8")
extra = """
    <uses-permission android:name="android.permission.INTERNET"/>
    <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE"/>
    <uses-permission android:name="android.permission.RECORD_AUDIO"/>
    <uses-permission android:name="android.permission.CAMERA"/>
    <uses-feature android:name="android.hardware.camera" android:required="false"/>
    <uses-feature android:name="android.hardware.microphone" android:required="false"/>
"""
match = re.search(r"<application\b", text)
if match is None:
    sys.exit("template has no <application> element")
text = text[: match.start()] + extra.lstrip("\n") + "    " + text[match.start():]
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(text, encoding="utf-8")
print(f"wrote {output} from {template}")
