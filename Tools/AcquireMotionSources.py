"""Fetch the free CC0 Standard archives; reject silently changed uploads.

Run with Blender's bundled Python. No third-party Python packages are required.
Downloaded vendor files stay in .cache; saved Blender sources and derived assets
contain the selected motions, with provenance. Existing submodules are untouched.
"""
import hashlib
import http.cookiejar
import json
from pathlib import Path
import re
import urllib.parse
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PACKS = [
    ('universal-animation-library', '17958403', 'cc73fc4e495b82958207316596317a3f40b9fa38065bde1027937452da537724'),
]

def main():
    directory = ROOT / '.cache/motion-matching'
    directory.mkdir(parents=True, exist_ok=True)
    opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(http.cookiejar.CookieJar()))
    for index, (slug, upload, digest) in enumerate(PACKS, 1):
        archive = directory / f'UAL{index}-Standard.zip'
        if not archive.exists():
            url = 'https://quaternius.itch.io/' + slug
            html = opener.open(url).read().decode()
            csrf = re.search(r'name="csrf_token" value="([^"]+)"', html).group(1)
            def post(path):
                request = urllib.request.Request(url + path, data=urllib.parse.urlencode({'csrf_token': csrf}).encode(), headers={'Referer': url})
                return json.load(opener.open(request))
            # The free download flow, without buying or accessing paid uploads.
            opener.open(post('/download_url')['url']).close()
            archive.write_bytes(opener.open(post('/file/' + upload)['url']).read())
        if hashlib.sha256(archive.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'UAL{index}: archive hash changed; retain the pinned version')
        target = (directory / f'UAL{index}').resolve()
        with zipfile.ZipFile(archive) as zipped:
            for name in zipped.namelist():
                if not (target / name).resolve().is_relative_to(target):
                    raise RuntimeError('Unsafe archive path')
            zipped.extractall(target)
        print(f'UAL{index}: pinned Standard archive verified')

if __name__ == '__main__':
    main()
