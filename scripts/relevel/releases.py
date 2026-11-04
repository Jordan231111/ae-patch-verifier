#!/usr/bin/env python3
"""Maintain the verified ReLevel release index. Never build in a web request."""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import urllib.error
import urllib.request

PACKAGE = 'work.ponix.relevel'
CERT = '29b04c032802179a86374c8a13dba9ddbe8d9303f4909ac03766cb77de17c027'
INDEX_TAG = 'relevel-ready'
RECOVERY_SCHEMA = 1


def now(): return datetime.now(timezone.utc).isoformat()
def digest(value): return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
def file_digest(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest()


def api(path, *, method='GET', data=None, module=False, missing=False):
    token = os.environ.get('RELEVEL_MODULE_TOKEN' if module else 'GH_TOKEN')
    if not token: raise RuntimeError('GitHub authentication is unavailable')
    request = urllib.request.Request('https://api.github.com/' + path,
        data=None if data is None else json.dumps(data).encode(), method=method,
        headers={'Authorization': 'Bearer ' + token, 'Accept': 'application/vnd.github+json',
                 'User-Agent': 'ReLevel verified builder', 'X-GitHub-Api-Version': '2022-11-28',
                 'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            body = response.read()
            return json.loads(body) if body else None
    except urllib.error.HTTPError as error:
        if missing and error.code == 404: return None
        raise RuntimeError(f'GitHub {method} {path.split("?")[0]} returned {error.code}') from None


def repository():
    repo = os.environ['GITHUB_REPOSITORY']
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', repo): raise RuntimeError('invalid repository')
    return repo


def module_repo(): return os.environ.get('RELEVEL_MODULE_REPO', 'Jordan231111/ReLevel')


def latest_module():
    repo = module_repo()
    commits = api(f'repos/{repo}/commits?sha=main&per_page=30', module=True)
    releases = api(f'repos/{repo}/releases?per_page=100', module=True)
    available = {r['tag_name'][7:]: r for r in releases if not r['draft']
                 and re.fullmatch(r'module-[a-f0-9]{40}', r['tag_name'])
                 and all(any(a['name'] == name and a['state'] == 'uploaded' and a['size'] > 0 for a in r['assets'])
                         for name in ('app-release.apk', 'module.json', 'SHA256SUMS'))}
    for commit in commits:
        if commit['sha'] in available: return commit['sha']
    raise RuntimeError('No completed main module prebuilt is available')


def index():
    release = api(f'repos/{repository()}/releases/tags/{INDEX_TAG}', missing=True)
    if not release: return None, None
    try: state = json.loads(release['body'])
    except (TypeError, ValueError): raise RuntimeError('Ready index is malformed') from None
    if state.get('schema') != 1 or state.get('packageName') != PACKAGE: raise RuntimeError('Wrong ready index schema')
    return release, state


def write_index(state):
    release, _ = index()
    data = {'body': json.dumps(state, sort_keys=True, separators=(',', ':')), 'draft': False,
            'prerelease': True, 'name': 'ReLevel verified download status', 'make_latest': 'false'}
    if release: api(f'repos/{repository()}/releases/{release["id"]}', method='PATCH', data=data)
    else:
        data.update(tag_name=INDEX_TAG, target_commitish=os.environ['GITHUB_SHA'])
        api(f'repos/{repository()}/releases', method='POST', data=data)


def observed(version, module):
    code, name = version.get('version_code'), version.get('version_name')
    if version.get('package_name') != PACKAGE or not isinstance(code, int) or code < 1 \
            or not isinstance(name, str) or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._+-]*', name):
        raise RuntimeError('Play version identity is invalid')
    return {'versionCode': code, 'versionName': name, 'moduleSha': module}


def emit(values):
    output = os.environ.get('GITHUB_OUTPUT')
    if output:
        with open(output, 'a') as stream:
            for key, value in values.items(): stream.write(f'{key}={value}\n')
    print(json.dumps(values, sort_keys=True))


def inspect(args):
    # Standard public runners are free. Refuse to silently turn this into private paid polling.
    if api(f'repos/{repository()}')['private']: raise RuntimeError('Scheduled builds require the public builder repository')
    version = json.loads(args.version.read_text())
    current = observed(version, latest_module())
    key = digest({'observed': current, 'recoverySchema': RECOVERY_SCHEMA})
    _, previous = index()
    previous = previous or {'schema': 1, 'packageName': PACKAGE, 'architecture': 'arm64-v8a'}
    previous['checkedAt'] = now()
    old = previous.get('ready', {})
    same = old.get('validated') is True and old.get('cacheKey') == key and previous.get('state') == 'ready'
    if same:
        asset = api(f'repos/{repository()}/releases/assets/{old["releaseAssetId"]}', missing=True)
        same = bool(asset and asset['state'] == 'uploaded' and asset['size'] == old['sizeBytes'])
    blocked = previous.get('state') == 'requires_review' and previous.get('failedKey') == key and not args.force
    changed = not same and not blocked
    previous['observed'] = current
    if changed: previous.update(state='preparing', attemptRunId=int(os.environ['GITHUB_RUN_ID']))
    write_index(previous)
    recovered = ''
    if old.get('versionCode') == current['versionCode'] and old.get('versionName') == current['versionName']:
        tag = old.get('recoveredTag', '')
        if re.fullmatch(r'relevel-inputs-[a-f0-9]{64}', tag):
            cache = api(f'repos/{repository()}/releases/tags/{tag}', missing=True)
            if cache and any(a['name'] == 'recovered.tar' and a['state'] == 'uploaded' for a in cache['assets']): recovered = tag
    args.plan.write_text(json.dumps({'observed': current, 'cacheKey': key, 'recoveredTag': recovered}, indent=2))
    emit({'changed': str(changed).lower(), 'module_sha': current['moduleSha'], 'cache_key': key,
          'version_code': current['versionCode'], 'version_name': current['versionName'], 'recovered_tag': recovered})


def release_asset(tag, filename, *, description):
    repo = repository()
    release = api(f'repos/{repo}/releases/tags/{tag}', missing=True)
    if release is None:
        api(f'repos/{repo}/releases', method='POST', data={'tag_name': tag, 'target_commitish': os.environ['GITHUB_SHA'],
            'name': description, 'body': description, 'draft': False, 'prerelease': True, 'make_latest': 'false'})
    else:
        existing = [a for a in release['assets'] if a['name'] == filename.name]
        if existing:
            if (len(existing) == 1 and existing[0]['state'] == 'uploaded' and existing[0]['size'] == filename.stat().st_size
                    and existing[0].get('digest') == 'sha256:' + file_digest(filename)):
                return existing[0]
            raise RuntimeError('An immutable cache asset already exists with different contents')
    subprocess.run(['gh', 'release', 'upload', tag, str(filename), '--repo', repo], check=True)
    release = api(f'repos/{repo}/releases/tags/{tag}')
    assets = [a for a in release['assets'] if a['name'] == filename.name and a['state'] == 'uploaded' and a['size'] == filename.stat().st_size]
    if len(assets) != 1: raise RuntimeError('Release upload did not finalize exactly')
    return assets[0]


def publish(args):
    plan = json.loads(args.plan.read_text())
    metadata, report = json.loads(args.metadata.read_text()), json.loads(args.report.read_text())
    bundle_hash = file_digest(args.bundle)
    if (metadata['bundleSha256'] != bundle_hash or report.get('bundle_sha256') != bundle_hash
        or report.get('ok') is not True or report.get('home_resume') is not True or report.get('runtime') != 'redroid'
        or metadata['certificateSha256'] != CERT or metadata['packageName'] != PACKAGE
        or metadata['moduleSourceSha'] != plan['observed']['moduleSha']
        or metadata['versionCode'] != plan['observed']['versionCode'] or metadata['versionName'] != plan['observed']['versionName']):
        raise RuntimeError('Candidate metadata and runtime validation do not agree')
    if observed(json.loads(args.version.read_text()), latest_module()) != plan['observed']:
        raise RuntimeError('Play or the module changed during preparation; candidate cannot be advertised as current')
    _, state = index()
    if state.get('observed') != plan['observed'] or state.get('attemptRunId') != int(os.environ['GITHUB_RUN_ID']):
        raise RuntimeError('This run no longer owns the ready index')
    recovered_tag = plan.get('recoveredTag', '')
    if args.recovered:
        recovered_tag = 'relevel-inputs-' + digest({'source': metadata['sourceSha256'], 'recoverySchema': RECOVERY_SCHEMA})
        cached = api(f'repos/{repository()}/releases/tags/{recovered_tag}', missing=True)
        if not cached or not any(a['name'] == 'recovered.tar' and a['state'] == 'uploaded' for a in cached['assets']):
            release_asset(recovered_tag, args.recovered, description='Verified recovered inputs for ReLevel ' + metadata['versionName'])
    bundle_tag = 'relevel-bundle-' + plan['cacheKey']
    asset = release_asset(bundle_tag, args.bundle,
                          description='Verified ReLevel ' + metadata['versionName'] + ' / module ' + plan['observed']['moduleSha'][:7])
    ready = {**plan['observed'], 'validated': True, 'certificateSha256': CERT, 'bundleSha256': bundle_hash,
             'cacheKey': plan['cacheKey'], 'filename': args.bundle.name, 'sizeBytes': args.bundle.stat().st_size,
             'releaseAssetId': asset['id'], 'preparedAt': now(), 'recoveredTag': recovered_tag,
             'sourceSha256': metadata['sourceSha256'], 'runId': int(os.environ['GITHUB_RUN_ID'])}
    release_asset(bundle_tag, args.metadata, description='Verified ReLevel build metadata')
    release_asset(bundle_tag, args.report, description='Verified ReLevel runtime report')
    bundle_release = api(f'repos/{repository()}/releases/tags/{bundle_tag}')
    api(f'repos/{repository()}/releases/{bundle_release["id"]}', method='PATCH',
        data={'body':json.dumps({'schema':1,'ready':ready},sort_keys=True), 'make_latest':'false'})
    state.update(state='ready', ready=ready, checkedAt=now())
    state.pop('failedKey', None)
    write_index(state)
    print('Published verified bundle ' + plan['cacheKey'])
    prune(bundle_tag)


def remove_release(release):
    api(f'repos/{repository()}/releases/{release["id"]}', method='DELETE')
    api(f'repos/{repository()}/git/refs/tags/{release["tag_name"]}', method='DELETE', missing=True)


def prune(current):
    """Retain the current and previous bundle plus only their recovered inputs."""
    all_releases = api(f'repos/{repository()}/releases?per_page=100')
    bundles = [r for r in all_releases if re.fullmatch(r'relevel-bundle-[a-f0-9]{64}', r['tag_name'])]
    bundles.sort(key=lambda r: r.get('published_at') or '', reverse=True)
    keep = {current}
    for release in bundles:
        if release['tag_name'] != current:
            keep.add(release['tag_name'])
            break
    inputs = set()
    for release in bundles:
        if release['tag_name'] in keep:
            try: tag = json.loads(release['body'])['ready']['recoveredTag']
            except (ValueError,TypeError,KeyError): continue
            if re.fullmatch(r'relevel-inputs-[a-f0-9]{64}',tag): inputs.add(tag)
        else: remove_release(release)
    for release in all_releases:
        if re.fullmatch(r'relevel-inputs-[a-f0-9]{64}',release['tag_name']) and release['tag_name'] not in inputs:
            remove_release(release)


def failure(args):
    _, state = index()
    if state and state.get('attemptRunId') == int(os.environ['GITHUB_RUN_ID']):
        key = digest({'observed': state['observed'], 'recoverySchema': RECOVERY_SCHEMA})
        if state.get('state') == 'ready' and state.get('ready',{}).get('cacheKey') == key:
            return  # A completed atomic publication survives a later cleanup/network failure.
        state.update(state='requires_review', failedKey=key)
        write_index(state)
        partial = api(f'repos/{repository()}/releases/tags/relevel-bundle-{key}',missing=True)
        if partial and state.get('ready',{}).get('cacheKey') != key: remove_release(partial)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    check = commands.add_parser('inspect')
    check.add_argument('--version', type=Path, required=True)
    check.add_argument('--plan', type=Path, required=True)
    check.add_argument('--force', action='store_true')
    publish_parser = commands.add_parser('publish')
    for name in ('plan', 'metadata', 'report', 'bundle', 'version'): publish_parser.add_argument('--' + name, type=Path, required=True)
    publish_parser.add_argument('--recovered', type=Path)
    commands.add_parser('failure')
    args = parser.parse_args()
    {'inspect': inspect, 'publish': publish, 'failure': failure}[args.command](args)


if __name__ == '__main__': main()
