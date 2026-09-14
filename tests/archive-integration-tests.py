"""Offline application integration: real CLI, real FFmpeg, deterministic yt-dlp boundary."""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

parser = argparse.ArgumentParser()
parser.add_argument('--cli', required=True, type=Path)
parser.add_argument('--fake-tool', required=True, type=Path)
opts, test_args = parser.parse_known_args()
CLI, FAKE = opts.cli.resolve(), opts.fake_tool.resolve()
FFMPEG = os.environ.get('ARCHIVE_TEST_FFMPEG') or shutil.which('ffmpeg')
FFPROBE = os.environ.get('ARCHIVE_TEST_FFPROBE') or shutil.which('ffprobe')
if not FFMPEG or not FFPROBE:
    raise SystemExit('Integration tests require real FFmpeg and FFprobe on PATH or ARCHIVE_TEST_FFMPEG/FFPROBE')
VIDEO_ID = 'abc123DEF45'
VIDEO_URL = 'https://www.youtube.com/watch?v=' + VIDEO_ID
SOURCE_URL = 'https://www.youtube.com/playlist?list=PLAUDIT'

def sha(path: Path) -> str:
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def run(command, *, env=None, timeout=45):
    return subprocess.run([str(x) for x in command], capture_output=True, text=True, env=env, timeout=timeout)

class ArchiveIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.fixture_temp = tempfile.TemporaryDirectory(prefix='archive-media-fixtures-')
        cls.fixture = Path(cls.fixture_temp.name)
        cls.video = cls.fixture / 'good.mp4'
        cls.audio = cls.fixture / 'good.m4a'
        cls.incompatible = cls.fixture / 'normalize-me.mkv'
        commands = [
            [FFMPEG, '-nostdin', '-v', 'error', '-y', '-f', 'lavfi', '-i', 'testsrc2=size=160x90:rate=8', '-f', 'lavfi', '-i', 'sine=sample_rate=48000', '-t', '1', '-c:v', 'libx264', '-threads', '1', '-pix_fmt', 'yuv420p', '-c:a', 'aac', cls.video],
            [FFMPEG, '-nostdin', '-v', 'error', '-y', '-i', cls.video, '-vn', '-c:a', 'copy', cls.audio],
            [FFMPEG, '-nostdin', '-v', 'error', '-y', '-i', cls.video, '-c:v', 'ffv1', '-threads', '1', '-c:a', 'pcm_s16le', cls.incompatible],
        ]
        for command in commands:
            result = run(command)
            if result.returncode:
                raise RuntimeError(result.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.fixture_temp.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='archive integration ')
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / 'archive root with spaces'
        self.package = self.base / 'portable package'
        self.package.mkdir()
        self.cli = self.package / CLI.name
        shutil.copy2(CLI, self.cli)
        (self.package / 'bin').mkdir()
        shutil.copy2(FAKE, self.package / 'bin' / ('yt-dlp.exe' if os.name == 'nt' else 'yt-dlp'))
        # Resolve actual media tools through PATH; Windows DLL search retains the CI Qt bin directory.
        self.env = dict(os.environ, ARCHIVE_TEST_PLAN=str(self.base / 'plan.json'))
        self.env['PATH'] = str(Path(FFMPEG).parent) + os.pathsep + str(Path(FFPROBE).parent) + os.pathsep + self.env.get('PATH', '')
        self.plan = {
            'video_id': VIDEO_ID, 'video_file': str(self.video), 'audio_file': str(self.audio),
            'calls_path': str(self.base / 'calls.jsonl'),
            'discovery': {'id': 'PLAUDIT', 'entries': [
                {'id': VIDEO_ID, 'title': 'Known historical title', 'playlist_index': 1, 'availability': 'public'},
                {'id': 'xyz987QWE65', 'title': 'Second historical title', 'playlist_index': 2, 'availability': 'public'},
            ]},
        }
        self.write_plan()

    def write_plan(self):
        Path(self.env['ARCHIVE_TEST_PLAN']).write_text(json.dumps(self.plan), encoding='utf-8')

    def command(self, command, *args, expect=0):
        result = run([self.cli, command, self.root, *args], env=self.env)
        self.assertEqual(result.returncode, expect, result.stdout + '\n' + result.stderr)
        return result

    def scan(self):
        return self.command('scan', SOURCE_URL, 'Test playlist')

    def canonical(self):
        return json.loads((self.root / 'State/ArchiveMode/items.json').read_text(encoding='utf-8'))

    def first(self):
        return next(x for x in self.canonical() if x['key'] == 'youtube:' + VIDEO_ID)

    def media_hashes(self):
        return {str(p.relative_to(self.root)): sha(p) for directory in ('Video', 'Audio') for p in (self.root / directory).rglob('*') if p.is_file()}

    def make_package(self, *, name='recovery-package', bad_audio=False, metadata_only=False, id_only=False):
        directory = self.root / 'State/ArchiveMode/Imports/Pending' / name
        (directory / 'files').mkdir(parents=True)
        manifest = {
            'schema_version': 1, 'package_id': name,
            'target': {'youtube_id': VIDEO_ID} if id_only else {'item_key': 'youtube:' + VIDEO_ID, 'youtube_id': VIDEO_ID},
            'provenance': {'method': 'old_local_backup', 'confidence': 'high'},
            'metadata': {'canonical_title': 'Recovered title', 'user_tags': ['recovered']},
        }
        if not metadata_only:
            shutil.copy2(self.incompatible, directory / 'files/video.mkv')
            if bad_audio:
                (directory / 'files/audio.m4a').write_bytes(b'not-media')
            else:
                shutil.copy2(self.audio, directory / 'files/audio.m4a')
            manifest['representations'] = {'video': {'file': 'files/video.mkv'}, 'audio': {'file': 'files/audio.m4a'}}
        (directory / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
        return directory

    def test_scan_sync_verify_and_idempotent_rerun(self):
        self.scan()
        self.command('sync-item', VIDEO_URL)
        self.command('verify-item', VIDEO_URL)
        before = self.media_hashes()
        canonical = self.canonical()
        calls = (self.base / 'calls.jsonl').read_bytes()
        self.command('sync-item', VIDEO_URL)
        self.assertEqual(before, self.media_hashes())
        self.assertEqual(canonical, self.canonical())
        self.assertEqual(calls, (self.base / 'calls.jsonl').read_bytes())
        for path in before:
            result = run([FFMPEG, '-nostdin', '-v', 'error', '-i', self.root / path, '-f', 'null', '-'])
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_partial_scan_preserves_membership(self):
        self.scan()
        self.plan['discovery'] = {}
        self.write_plan()
        self.command('scan', SOURCE_URL, expect=3)
        playlist = json.loads((self.root / 'Playlists/PLAUDIT/items.json').read_text())
        self.assertEqual(len(playlist), 2)
        self.assertTrue(all(x['membership'] == 'active' for x in playlist))

    def test_normalization_preserves_original_bytes(self):
        self.plan['video_file'] = str(self.incompatible)
        self.write_plan()
        self.scan()
        self.command('sync-item', VIDEO_URL)
        originals = list((self.root / 'Video').glob('*.mkv'))
        self.assertEqual(len(originals), 1)
        self.assertEqual(sha(originals[0]), sha(self.incompatible))
        self.assertTrue(self.first()['video']['path'].endswith('.mp4'))
        self.command('verify-item', VIDEO_URL)

    def test_missing_complete_media_cannot_report_pass(self):
        self.scan()
        self.command('sync-item', VIDEO_URL)
        (self.root / self.first()['video']['path']).unlink()
        self.plan['download_exit'] = 7
        self.write_plan()
        self.command('sync-item', VIDEO_URL, expect=1)
        self.assertNotEqual(self.first()['video']['state'], 'complete')
        self.command('verify-item', VIDEO_URL, expect=1)
        self.plan['download_exit'] = 0
        self.write_plan()
        self.command('sync-item', VIDEO_URL)
        self.command('verify-item', VIDEO_URL)

    def test_real_import_and_immutable_accepted_evidence(self):
        self.scan()
        directory = self.make_package()
        self.command('validate', directory)
        self.command('ingest-pending')
        self.command('verify-item', VIDEO_URL)
        accepted = self.root / 'State/ArchiveMode/Imports/Accepted/recovery-package'
        receipt = json.loads((accepted / 'receipt.json').read_text())
        self.assertEqual(receipt['result'], 'accepted')
        self.assertEqual(len(receipt['promoted_paths']), 2)
        self.assertEqual(receipt['file_sha256']['manifest.json'], sha(accepted / 'manifest.json'))
        self.assertEqual(self.first()['video']['origin'], 'external_recovery')
        self.assertIn('recovered', self.first()['user_tags'])
        self.assertIn('external_recovery_accepted', (self.root / 'Playlists/PLAUDIT/history.jsonl').read_text())
        before = {str(p.relative_to(accepted)): sha(p) for p in accepted.rglob('*') if p.is_file()}
        self.make_package()
        self.command('ingest-pending', expect=1)
        self.assertEqual(before, {str(p.relative_to(accepted)): sha(p) for p in accepted.rglob('*') if p.is_file()})
        self.assertFalse((self.root / 'State/video-archive.txt').exists())
        self.assertFalse((self.root / 'State/audio-archive.txt').exists())

    def test_failed_second_representation_is_atomic_and_retryable(self):
        self.scan()
        directory = self.make_package(bad_audio=True)
        before = self.canonical()
        self.command('ingest-pending', expect=1)
        self.assertEqual(before, self.canonical())
        self.assertEqual({}, self.media_hashes())
        self.assertTrue(directory.exists())
        shutil.copy2(self.audio, directory / 'files/audio.m4a')
        self.command('ingest-pending')
        self.command('verify-item', VIDEO_URL)

    def test_metadata_only_id_only_import(self):
        self.scan()
        directory = self.make_package(metadata_only=True, id_only=True)
        self.command('validate', directory)
        self.command('ingest-pending')
        self.assertEqual(self.first()['title'], 'Recovered title')
        self.assertEqual(self.first()['video']['state'], 'missing')
        self.assertEqual({}, self.media_hashes())

    def test_archive_skip_retry_argument_order(self):
        self.plan['archive_skip'] = True
        self.write_plan()
        self.scan()
        self.command('sync-item', VIDEO_URL)
        calls = [json.loads(x) for x in (self.base / 'calls.jsonl').read_text().splitlines()]
        retries = [x for x in calls if '--no-download-archive' in x]
        self.assertEqual(len(retries), 2)
        for args in retries:
            self.assertLess(args.index('--no-download-archive'), args.index('--'))
            self.assertNotIn('--download-archive', args)

    def test_invalid_cli_arguments_have_no_filesystem_side_effects(self):
        self.command('sync-item', 'https://notyoutube.com/watch?v=' + VIDEO_ID, expect=2)
        self.assertFalse(self.root.exists())
        self.command('scan', '--exec=bad', expect=2)
        self.assertFalse(self.root.exists())
        self.command('unknown-command', expect=2)
        self.assertFalse(self.root.exists())

    def test_concurrent_writer_and_old_live_lock_are_rejected(self):
        self.plan['delay_ms'] = 1500
        self.write_plan()
        first = subprocess.Popen([str(self.cli), 'scan', str(self.root), SOURCE_URL], env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(lambda: first.kill() if first.poll() is None else None)
        lock = self.root / 'State/ArchiveMode/sync.lock'
        deadline = time.monotonic() + 5
        while not (self.base / 'calls.jsonl').exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        self.assertTrue(lock.exists())
        old = time.time() - 3600
        os.utime(lock, (old, old))
        result = self.command('ingest-pending', expect=1)
        self.assertIn('Another Archive operation', result.stderr)
        stdout, stderr = first.communicate(timeout=10)
        self.assertEqual(first.returncode, 0, stdout + stderr)

    def test_crashed_writer_lock_is_recovered(self):
        self.plan['delay_ms'] = 3000
        self.write_plan()
        kwargs = {'start_new_session': True} if os.name != 'nt' else {}
        first = subprocess.Popen([str(self.cli), 'scan', str(self.root), SOURCE_URL], env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kwargs)
        self.addCleanup(lambda: first.kill() if first.poll() is None else None)
        deadline = time.monotonic() + 5
        while not (self.base / 'calls.jsonl').exists() and time.monotonic() < deadline:
            time.sleep(0.01)
        if os.name == 'nt':
            subprocess.run(['taskkill', '/PID', str(first.pid), '/T', '/F'], capture_output=True, timeout=10)
        else:
            os.killpg(first.pid, signal.SIGKILL)
        first.communicate(timeout=10)
        self.plan['delay_ms'] = 0
        self.write_plan()
        self.scan()
        self.assertEqual(len(self.canonical()), 2)

    def stage_transaction(self, *, conflict=False):
        self.scan()
        state = self.root / 'State/ArchiveMode'
        operations = []
        for filename in ('items.json', 'sources.json'):
            path = state / filename
            before = path.read_bytes()
            value = json.loads(before)
            value[0]['title'] = 'Journal replay title'
            after = json.dumps(value).encode()
            import base64
            operations.append({'path': 'State/ArchiveMode/' + filename, 'before_exists': True,
                               'before_sha256': hashlib.sha256(before).hexdigest(),
                               'after_base64': base64.b64encode(after).decode(),
                               'after_sha256': hashlib.sha256(after).hexdigest()})
            if filename == 'items.json':
                path.write_bytes(after)  # Simulate a crash after the first committed target.
            elif conflict:
                value[0]['title'] = 'External conflicting edit'
                path.write_text(json.dumps(value))
        journal = state / 'transaction.json'
        journal.write_text(json.dumps({'schema_version': 1, 'operations': operations}))
        return journal

    def test_mid_commit_restart_rolls_forward_idempotently(self):
        journal = self.stage_transaction()
        self.command('ingest-pending')
        self.assertFalse(journal.exists())
        self.assertEqual(self.canonical()[0]['title'], 'Journal replay title')
        source = json.loads((self.root / 'State/ArchiveMode/sources.json').read_text())
        self.assertEqual(source[0]['title'], 'Journal replay title')
        self.command('ingest-pending')

    def test_transaction_conflict_preserves_all_bytes(self):
        journal = self.stage_transaction(conflict=True)
        before = {p: p.read_bytes() for p in (journal, self.root / 'State/ArchiveMode/items.json', self.root / 'State/ArchiveMode/sources.json')}
        self.command('ingest-pending', expect=1)
        self.assertEqual(before, {p: p.read_bytes() for p in before})

    def test_corrupt_state_is_not_silently_replaced(self):
        self.scan()
        path = self.root / 'State/ArchiveMode/items.json'
        path.write_text('{}')
        self.command('sync-item', VIDEO_URL, expect=1)
        self.assertEqual(path.read_text(), '{}')

    def test_duplicate_playlist_occurrences_survive_removal_and_reappearance(self):
        entry = self.plan['discovery']['entries'][0]
        self.plan['discovery']['entries'] = [entry, dict(entry, playlist_index=2)]
        self.write_plan(); self.scan()
        path = self.root / 'Playlists/PLAUDIT/items.json'
        self.assertEqual(len(self.canonical()), 1)
        rows = json.loads(path.read_text())
        self.assertEqual(len({x['entry_key'] for x in rows}), 2)
        self.plan['discovery']['entries'] = [entry]
        self.write_plan(); self.scan()
        rows = json.loads(path.read_text())
        self.assertEqual(sum(x['membership'] == 'removed' for x in rows), 1)
        self.plan['discovery']['entries'] = [entry, dict(entry, playlist_index=2)]
        self.write_plan(); self.scan()
        rows = json.loads(path.read_text())
        self.assertTrue(all(x['membership'] == 'active' for x in rows))
        self.assertEqual(len(self.canonical()), 1)

    def test_resource_upgrade_preserves_prior_contract(self):
        self.scan()
        contract = self.root / 'ARCHIVE_AGENT.md'
        original = contract.read_bytes()
        contract.write_text('locally customized legacy contract')
        self.command('ingest-pending')
        self.assertEqual(contract.read_bytes(), original)
        backups = list(self.root.glob('ARCHIVE_AGENT.md.previous-*'))
        self.assertEqual(len(backups), 1)
        self.assertEqual(backups[0].read_text(), 'locally customized legacy contract')

    def test_csv_formula_and_playlist_line_injection_are_neutralized(self):
        self.plan['discovery']['entries'][0]['title'] = '=HYPERLINK("bad")\n#EXTINF:injected'
        self.write_plan(); self.scan()
        path = self.root / 'Playlists/PLAUDIT/catalog.csv'
        with path.open(newline='', encoding='utf-8') as stream:
            rows = list(csv.DictReader(stream))
        self.assertTrue(any(str(value).startswith("'=HYPERLINK") for value in rows[0].values()))
        self.command('sync-item', VIDEO_URL)
        playlist = (self.root / 'Playlists/PLAUDIT/video.m3u8').read_text()
        self.assertEqual(sum(line.startswith('#EXTINF:') for line in playlist.splitlines()), 1)

    def test_valid_existing_media_is_adopted_when_source_unavailable(self):
        self.scan()
        shutil.copy2(self.video, self.root / 'Video' / ('backup [' + VIDEO_ID + '].mp4'))
        shutil.copy2(self.audio, self.root / 'Audio' / ('backup [' + VIDEO_ID + '].m4a'))
        self.plan['discovery']['entries'][0]['availability'] = 'private'
        self.write_plan(); self.scan()
        self.command('sync-item', VIDEO_URL)
        self.command('verify-item', VIDEO_URL)

    @unittest.skipUnless(os.name == 'nt', 'Windows PowerShell acceptance wrapper')
    def test_windows_harness_positive_and_tampered_package_negative(self):
        powershell = shutil.which('pwsh') or shutil.which('powershell')
        self.assertIsNotNone(powershell)
        script = self.package / 'archive-local-harness.ps1'
        shutil.copy2(Path(__file__).resolve().parents[1] / 'scripts/archive-local-harness.ps1', script)
        shutil.copy2(FAKE, self.package / 'bin/deno.exe')
        ffbin = self.package / '3rdParty/ffmpeg/bin'
        shutil.copytree(Path(FFMPEG).parent, ffbin)
        commit = 'a' * 40
        (self.package / 'build-identity.json').write_text(json.dumps({'commit': commit, 'qualification': 'windows-ci-qualified-for-local-harness', 'run_id': 'fixture'}))
        (self.package / 'PORTABLE_MANIFEST.txt').write_text('deterministic fixture package')
        manifest = '\n'.join(sha(p) + '  ' + p.relative_to(self.package).as_posix() for p in self.package.rglob('*') if p.is_file()) + '\n'
        (self.package / 'SHA256SUMS.txt').write_text(manifest, encoding='ascii')
        args = [powershell, '-NoProfile', '-File', script, '-ArchiveRoot', self.root, '-PlaylistUrl', SOURCE_URL, '-VideoUrl', VIDEO_URL, '-ExpectedCommit', commit]
        result = run(args, env=self.env, timeout=120)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        receipt = list(self.root.glob('local-harness-evidence-*.json'))
        self.assertEqual(len(receipt), 1)
        evidence = json.loads(receipt[0].read_text(encoding='utf-8-sig'))
        self.assertEqual(evidence['item_key'], 'youtube:' + VIDEO_ID)
        self.assertEqual(evidence['result'], 'PASS')
        (self.package / 'PORTABLE_MANIFEST.txt').write_text('tampered')
        result = run(args + ['-AllowExistingArchive'], env=self.env, timeout=120)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(len(list(self.root.glob('local-harness-evidence-*.json'))), 1)

    def test_malformed_journal_payload_is_rejected_before_any_write(self):
        journal = self.stage_transaction()
        data = json.loads(journal.read_text())
        import base64
        bad = b'{}'
        data['operations'][1]['after_base64'] = base64.b64encode(bad).decode()
        data['operations'][1]['after_sha256'] = hashlib.sha256(bad).hexdigest()
        journal.write_text(json.dumps(data))
        sources = self.root / 'State/ArchiveMode/sources.json'
        before = sources.read_bytes()
        self.command('ingest-pending', expect=1)
        self.assertEqual(sources.read_bytes(), before)
        self.assertTrue(journal.exists())

    def test_corrupt_history_cannot_be_extended_or_replaced(self):
        self.scan()
        history = self.root / 'Playlists/PLAUDIT/history.jsonl'
        history.write_bytes(b'not-json\n')
        before = self.canonical()
        self.command('scan', SOURCE_URL, expect=1)
        self.assertEqual(history.read_bytes(), b'not-json\n')
        self.assertEqual(self.canonical(), before)

    def test_linked_state_directory_is_refused(self):
        outside = self.base / 'outside'
        outside.mkdir()
        self.root.mkdir()
        link = self.root / 'State'
        if os.name == 'nt':
            result = subprocess.run(['cmd', '/c', 'mklink', '/J', str(link), str(outside)], capture_output=True)
            if result.returncode:
                self.skipTest('Host does not permit junction creation')
            self.addCleanup(lambda: os.rmdir(link) if link.exists() else None)
        else:
            link.symlink_to(outside, target_is_directory=True)
        self.command('scan', SOURCE_URL, expect=1)
        self.assertEqual(list(outside.iterdir()), [])

if __name__ == '__main__':
    unittest.main(argv=[sys.argv[0], *test_args], verbosity=2)
