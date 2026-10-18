"""Generate deterministic fixtures and test the actual FFmpeg pipeline.
Usage: python scripts/integration.py --build build --ffmpeg /path/to/ffmpeg
Pass --network to additionally verify HTTP, UDP and SRT on loopback.
"""
import argparse
import functools
import http.server
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import threading
import time

def run(args, timeout=40):
    print('+', ' '.join(map(str, args)), flush=True)
    result = subprocess.run(list(map(str, args)), timeout=timeout, text=True, capture_output=True, encoding='utf-8', errors='replace')
    print(result.stdout + result.stderr, end='', flush=True)
    if result.returncode:
        raise RuntimeError(f'command failed ({result.returncode})')
    return result.stdout

def free_port(kind=socket.SOCK_STREAM):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]

def terminate(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', default='build')
    parser.add_argument('--ffmpeg', default='ffmpeg')
    parser.add_argument('--ffprobe', default='ffprobe')
    parser.add_argument('--network', action='store_true')
    parser.add_argument('--network-only', action='store_true', help='Reuse fixtures and run only loopback network checks')
    parser.add_argument('--audio', action='store_true')
    args = parser.parse_args()
    args.network = args.network or args.network_only
    root = Path(__file__).resolve().parent.parent
    media = root / 'test-media'
    media.mkdir(exist_ok=True)
    build = (root / args.build).resolve()
    suffix = '.exe' if os.name == 'nt' else ''
    binary = lambda name: build / (name + suffix)
    sample = media / 'sample.mp4'
    ff = [args.ffmpeg, '-hide_banner', '-loglevel', 'error', '-y']
    if not args.network_only:
        run(ff + ['-f', 'lavfi', '-i', 'testsrc2=size=640x360:rate=30', '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=44100', '-t', '6', '-c:v', 'libx264', '-pix_fmt', 'yuv420p', '-bf', '2', '-g', '30', '-c:a', 'aac', '-movflags', '+faststart', sample])
        run(ff + ['-i', sample, '-vn', '-c:a', 'pcm_s16le', media / 'audio.wav'])
        run(ff + ['-i', sample, '-an', '-c:v', 'copy', media / 'video.mp4'])
        run([binary('queue_tests')])
        for extra in [['--expect-video', '180'], ['--seek', '2.25', '--expect-video', '113'], *[['--filter', str(i), '--expect-video', '180'] for i in range(1, 5)]]:
            run([binary('media_smoke'), sample, *extra])
        run([binary('media_smoke'), media / 'audio.wav'])
        run([binary('media_smoke'), media / 'video.mp4'])
        unicode_path = media / '\u4e2d\u6587 \u97f3\u89c6\u9891.mp4'
        shutil.copyfile(sample, unicode_path)
        run([binary('media_smoke'), unicode_path])
        run([binary('media_smoke'), media / 'missing.mp4', '--error'])
        run([binary('media_smoke'), sample, '--cancel'])
        run([binary('controller_smoke'), sample, *(['--audio'] if args.audio else [])])
        run([binary('controller_smoke'), media / 'audio.wav', '--audio-only', *(['--audio'] if args.audio else [])])
        run([binary('controller_smoke'), media / 'video.mp4'])
    if args.network:
        handler = functools.partial(http.server.SimpleHTTPRequestHandler, directory=str(media))
        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            run([binary('media_smoke'), f'http://127.0.0.1:{server.server_port}/sample.mp4'])
        finally:
            server.shutdown()
            server.server_close()
        udp = free_port(socket.SOCK_DGRAM)
        run([binary('media_smoke'), f'udp://127.0.0.1:{udp}', '--cancel'])
        sender = subprocess.Popen(ff + ['-re', '-stream_loop', '-1', '-i', str(sample), '-c', 'copy', '-f', 'mpegts', f'udp://127.0.0.1:{udp}?pkt_size=1316'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            # Live inputs have no EOF; cancellation must interrupt both IO and queues.
            run([binary('media_smoke'), f'udp://127.0.0.1:{udp}?fifo_size=100000&overrun_nonfatal=1', '--cancel', '--run-for', '6000', '--require-frames'])
        finally:
            terminate(sender)
        protocols = run([args.ffmpeg, '-hide_banner', '-protocols'])
        if 'srt' in protocols:
            port = free_port(socket.SOCK_DGRAM)
            captured = media / 'srt-capture.ts'
            with (media / 'srt-receiver.log').open('w') as log:
                receiver = subprocess.Popen(ff + ['-i', f'srt://127.0.0.1:{port}?mode=listener&latency=120000', '-c', 'copy', '-f', 'mpegts', str(captured)], stdout=log, stderr=log)
                try:
                    time.sleep(0.5)
                    run([binary('media_smoke'), sample, '--publish', f'srt://127.0.0.1:{port}?mode=caller&latency=120000'])
                    try:
                        receiver.wait(timeout=8)
                    except subprocess.TimeoutExpired:
                        terminate(receiver)
                finally:
                    terminate(receiver)
            data = json.loads(run([args.ffprobe, '-v', 'error', '-show_entries', 'stream=codec_type,codec_name', '-of', 'json', captured]))
            assert {s['codec_type'] for s in data['streams']} == {'audio', 'video'}, data
        else:
            print('SKIP SRT: ffmpeg executable lacks the srt protocol')
        port = free_port()
        captured = media / 'rtmp-capture.flv'
        with (media / 'rtmp-receiver.log').open('w') as log:
            receiver = subprocess.Popen(ff + ['-listen', '1', '-i', f'rtmp://127.0.0.1:{port}/live/test', '-c', 'copy', '-f', 'flv', str(captured)], stdout=log, stderr=log)
            try:
                time.sleep(0.5)
                run([binary('media_smoke'), sample, '--publish', f'rtmp://127.0.0.1:{port}/live/test'])
                try:
                    receiver.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    terminate(receiver)
            finally:
                terminate(receiver)
        data = json.loads(run([args.ffprobe, '-v', 'error', '-show_entries', 'stream=codec_type,codec_name', '-of', 'json', captured]))
        assert {s['codec_type'] for s in data['streams']} == {'audio', 'video'}, data
    print('ALL REQUESTED CHECKS PASSED')

if __name__ == '__main__':
    main()
