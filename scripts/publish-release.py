#!/usr/bin/env python3
"""Stream a temporary APKS release with a bounded upload/reconciliation budget.

No cache, module compilation, native audits or server-side repackaging are involved.
A lost upload response is reconciled before retrying; drafts are never advertised.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time
from urllib.error import HTTPError
from urllib.parse import quote, urlsplit
from urllib.request import Request, urlopen


def ready_asset(assets, name, size, digest):
    return next((a for a in assets if a.get('name') == name and a.get('state') == 'uploaded'
                 and a.get('size') == size and a.get('digest') == digest), None)


class Publisher:
    def __init__(self, repo, token, budget=28):
        self.repo, self.token = repo, token
        self.deadline = time.monotonic() + budget

    def remaining(self):
        left = self.deadline - time.monotonic()
        if left <= 0:
            raise TimeoutError('GitHub publishing exceeded its time budget; please retry the build.')
        return left

    def api(self, method, path, data=None):
        request = Request('https://api.github.com/repos/' + self.repo + path,
                          data=None if data is None else json.dumps(data).encode(), method=method,
                          headers={'Authorization': 'Bearer ' + self.token,
                                   'Accept': 'application/vnd.github+json',
                                   'X-GitHub-Api-Version': '2022-11-28',
                                   'Content-Type': 'application/json', 'User-Agent': 'AE-APKS-Publisher'})
        with urlopen(request, timeout=min(5, self.remaining())) as response:
            raw = response.read()
            return json.loads(raw) if raw else None

    def upload(self, url, path):
        # HTTP/1.1 provides a separate streaming connection for each attempt. A
        # slow/stalled connection is bounded instead of blocking gh for minutes.
        with tempfile.TemporaryDirectory(prefix='ae-upload-') as directory:
            directory = Path(directory)
            headers, response = directory / 'headers', directory / 'response.json'
            headers.write_text('Authorization: Bearer ' + self.token + '\n'
                               'Accept: application/vnd.github+json\n'
                               'Content-Type: application/octet-stream\n'
                               'X-GitHub-Api-Version: 2022-11-28\nExpect:\n')
            headers.chmod(0o600)
            limit = min(24, max(0.5, self.remaining() - 1))
            result = subprocess.run([
                'curl', '--http1.1', '--silent', '--show-error', '--fail-with-body',
                '--connect-timeout', '4', '--max-time', str(limit),
                '--speed-time', '4', '--speed-limit', '8388608',
                '--request', 'POST', '--upload-file', str(path), '--header', '@' + str(headers),
                '--output', str(response), '--write-out', '%{http_code} %{time_total} %{speed_upload}', url
            ], capture_output=True, text=True, timeout=self.remaining() + 1)
            print('[publish] upload status/seconds/bytes-per-second:', result.stdout, flush=True)
            try:
                asset = json.loads(response.read_text())
            except (OSError, ValueError):
                asset = None
            return asset if result.returncode == 0 and isinstance(asset, dict) else None

    def publish(self, path, tag, target, title, body):
        size = path.stat().st_size
        with path.open('rb') as stream:
            digest = 'sha256:' + hashlib.file_digest(stream, 'sha256').hexdigest()
        try:
            release = self.api('GET', '/releases/tags/' + quote(tag, safe=''))
        except HTTPError as error:
            if error.code != 404:
                raise
            # GET-by-tag can omit drafts. A rerun must reuse its draft instead
            # of creating another release or overwriting a completed upload.
            releases = self.api('GET', '/releases?per_page=100')
            release = next((r for r in releases if r.get('tag_name') == tag), None)
            if release is None:
                release = self.api('POST', '/releases', dict(tag_name=tag, target_commitish=target,
                                   name=title, body=body, draft=True, make_latest='false'))
        release_path = '/releases/' + str(release['id'])
        url = release['upload_url'].split('{', 1)[0]
        parsed = urlsplit(url)
        if parsed.scheme != 'https' or parsed.hostname != 'uploads.github.com':
            raise RuntimeError('Unexpected GitHub upload host')
        url += '?name=' + quote(path.name, safe='')
        assets = release.get('assets', [])
        for attempt in range(2):
            match = ready_asset(assets, path.name, size, digest)
            if match:
                break
            if not release.get('draft'):
                raise RuntimeError('A published release with different content already exists')
            for asset in assets:
                if asset.get('name') == path.name:
                    self.api('DELETE', '/releases/assets/' + str(asset['id']))
            print(f'[publish] stream attempt {attempt + 1}; {size} bytes', flush=True)
            asset = self.upload(url, path)
            # A timeout can happen after the server commits the complete asset.
            assets = [asset] if asset else self.api('GET', release_path + '/assets')
        match = ready_asset(assets, path.name, size, digest)
        if not match:
            raise RuntimeError('GitHub upload did not complete within the publishing budget; retry the build.')
        if release.get('draft'):
            try:
                release = self.api('PATCH', release_path, dict(draft=False, make_latest='false'))
            except (HTTPError, TimeoutError, OSError):
                # Reconcile a lost publish acknowledgement; never delete a release
                # whose publication might already have succeeded.
                release = self.api('GET', release_path)
                if release.get('draft'):
                    raise
        print('[publish] ready:', release['html_url'], flush=True)
        return release


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--file', type=Path, required=True)
    parser.add_argument('--tag', required=True)
    parser.add_argument('--title', required=True)
    parser.add_argument('--notes', required=True)
    args = parser.parse_args()
    repo, token, target = (os.environ[k] for k in ['GITHUB_REPOSITORY', 'GH_TOKEN', 'GITHUB_SHA'])
    Publisher(repo, token).publish(args.file, args.tag, target, args.title, args.notes)


if __name__ == '__main__':
    main()
